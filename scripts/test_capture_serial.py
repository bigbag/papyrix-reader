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

from capture_serial import capture, list_ports


def run_capture(devices, output, status):
    list_ports.comports = lambda: [SimpleNamespace(**p) for p in json.loads(devices.read_text())]
    with status.open("w", buffering=1) as stream:
        import sys

        sys.stdout = stream
        capture("CLASSIC", output)


class CaptureSerialTest(unittest.TestCase):
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
