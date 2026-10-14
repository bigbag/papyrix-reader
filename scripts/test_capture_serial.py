#!/usr/bin/env python3
"""Check serial log retention with real pseudo-terminals."""

import json
import multiprocessing
import os
import pty
import tempfile
import time
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from capture_serial import capture, list_ports, serial


def run_capture(devices, output, status):
    list_ports.comports = lambda: [SimpleNamespace(**p) for p in json.loads(devices.read_text())]
    with status.open("w", buffering=1) as stream:
        import sys

        sys.stdout = stream
        capture("CLASSIC", output)


def run_retains_before_disconnect(output):
    import sys

    sys.stdout = open(os.devnull, "w")
    master, slave = pty.openpty()
    device = os.ttyname(slave)
    payload = b"Entering deep sleep\n"
    received = bytearray()
    sleeps = []
    idle = True
    open_port = serial.Serial.open
    read_port_method = serial.Serial.read
    fd_read = os.read

    def open_and_send(port):
        open_port(port)
        os.write(master, payload)

    def read_port(port, size=1):
        nonlocal idle
        if idle:
            idle = False
            return b""
        return read_port_method(port, size)

    def read_and_disconnect(fd, size):
        nonlocal master
        data = fd_read(fd, size)
        received.extend(data)
        if received == payload and master is not None:
            os.close(master)
            master = None
        return data

    try:
        with patch.object(serial.Serial, "open", open_and_send), \
                patch.object(serial.Serial, "read", read_port), \
                patch("serial.serialposix.os.read", read_and_disconnect), \
                patch.object(list_ports, "comports", side_effect=[
                    [SimpleNamespace(serial_number="CLASSIC", device=device)], KeyboardInterrupt]), \
                patch("capture_serial.time.sleep", side_effect=sleeps.append):
            try:
                capture("CLASSIC", output)
            except KeyboardInterrupt:
                pass
        if 0.01 not in sleeps:
            raise AssertionError("Capture does not pause after an empty read")
    finally:
        if master is not None:
            os.close(master)
        os.close(slave)


class CaptureSerialTest(unittest.TestCase):
    def test_retains_bytes_received_before_disconnect(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "capture.log"
            process = multiprocessing.get_context("fork").Process(
                target=run_retains_before_disconnect, args=(output,))
            process.start()
            try:
                process.join(6)
                self.assertFalse(process.is_alive(), "Capture does not finish after disconnect")
                self.assertEqual(process.exitcode, 0)
                self.assertEqual(output.read_bytes(), b"Entering deep sleep\n")
            finally:
                process.terminate()
                process.join(5)

    def test_append_across_disconnect_and_sleep(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            devices, output, status = (root / name for name in ("devices.json", "capture.log", "status.log"))
            output.write_bytes(b"existing log\n")
            devices.write_text("[]")
            process = multiprocessing.get_context("fork").Process(target=run_capture, args=(devices, output, status))
            masters, slaves = [], []

            def endpoint():
                master, slave = pty.openpty()
                masters.append(master)
                slaves.append(slave)
                return master, os.ttyname(slave)

            def discover(entries):
                next_file = root / "next.json"
                next_file.write_text(json.dumps(entries))
                next_file.replace(devices)

            def wait_for(path, value):
                deadline = time.monotonic() + 6
                while time.monotonic() < deadline:
                    if path.exists() and value in path.read_bytes():
                        return
                    self.assertTrue(process.is_alive(), "Capture exits during reconnect")
                    time.sleep(0.02)
                self.fail(f"Capture does not produce {value!r}")

            process.start()
            try:
                wait_for(status, b"CAPTURE_READY")
                wrong, wrong_path = endpoint()
                first, first_path = endpoint()
                discover([{"serial_number": "OTHER", "device": wrong_path},
                          {"serial_number": "classic", "device": first_path}])
                wait_for(status, b"Connected:")
                os.write(wrong, b"wrong reader\n")
                os.write(first, b"before disconnect\n")
                wait_for(output, b"before disconnect\n")
                os.close(first)
                masters.remove(first)
                wait_for(status, b"Serial disconnected:")
                second, second_path = endpoint()
                discover([{"serial_number": "CLASSIC", "device": second_path}])
                wait_for(status, f"at {second_path}".encode())
                os.write(second, b"after disconnect\nEntering deep ")
                wait_for(output, b"Entering deep ")
                os.write(second, b"sleep (external power: yes)\n")
                wait_for(output, b"sleep (external power: yes)\n")
                os.close(second)
                masters.remove(second)
                wait_for(status, b"Reader sleeps;")
                third, third_path = endpoint()
                discover([{"serial_number": "CLASSIC", "device": third_path}])
                wait_for(status, f"at {third_path}".encode())
                os.write(third, b"after wake\n")
                wait_for(output, b"after wake\n")
                self.assertEqual(output.read_bytes(), b"existing log\nbefore disconnect\nafter disconnect\n"
                                 b"Entering deep sleep (external power: yes)\nafter wake\n")
            finally:
                process.terminate()
                process.join(5)
                for descriptor in masters + slaves:
                    os.close(descriptor)


if __name__ == "__main__":
    unittest.main()
