#!/usr/bin/env python3
"""Append reader serial output across USB disconnects."""

import argparse
import os
import time

import serial
from serial.tools import list_ports


def capture(serial_number, output):
    sleeping_device = None
    print("CAPTURE_READY: waiting for reader", flush=True)
    with open(output, "ab", buffering=0) as log:
        while True:
            device = next(
                (p.device for p in list_ports.comports() if (p.serial_number or "").upper() == serial_number.upper()),
                None,
            )
            if device is None:
                sleeping_device = None
                time.sleep(1)
                continue
            try:
                node = os.stat(device)
                identity = (device, node.st_dev, node.st_ino, node.st_rdev)
            except FileNotFoundError:
                time.sleep(1)
                continue
            if identity == sleeping_device:
                time.sleep(1)
                continue
            sleeping = False
            try:
                with serial.Serial(port=None, baudrate=115200, timeout=0.25, exclusive=True) as port:
                    port.dtr = False
                    port.rts = False
                    port.port = device
                    port.open()
                    print(f"Connected: {serial_number} at {device}", flush=True)
                    window = b""
                    while True:
                        data = port.read(4096)
                        if data:
                            log.write(data)
                            window += data
                            sleeping = sleeping or b"Entering deep sleep" in window
                            window = window[-64:]
            except serial.SerialException as error:
                if sleeping:
                    sleeping_device = identity
                    print("Reader sleeps; waiting for USB re-enumeration", flush=True)
                else:
                    print(f"Serial disconnected: {error}; waiting to reconnect", flush=True)
                time.sleep(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("serial_number", help="USB serial number shown by pio device list")
    parser.add_argument("output", help="Log file to append")
    args = parser.parse_args()
    try:
        capture(args.serial_number, args.output)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
