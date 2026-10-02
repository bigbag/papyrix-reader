#!/usr/bin/env python3
"""Select one connected Espressif USB-Serial/JTAG port."""

import json
import subprocess
import sys


def select_port(devices):
    ports = [
        device["port"]
        for device in devices
        if "VID:PID=303A:1001" in device.get("hwid", "").upper().split()
    ]
    if len(ports) != 1:
        raise ValueError(f"Expected one Espressif USB-Serial/JTAG port; found {len(ports)}. "
                         "Connect one reader or set PLATFORMIO_UPLOAD_PORT.")
    return ports[0]


if __name__ == "__main__":
    try:
        devices = json.loads(subprocess.check_output(["pio", "device", "list", "--json-output"], text=True))
        print(select_port(devices))
    except (subprocess.CalledProcessError, json.JSONDecodeError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
