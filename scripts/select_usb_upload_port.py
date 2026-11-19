#!/usr/bin/env python3
"""Select the only serial port with the target's USB ID."""

import argparse
import json
import os
import subprocess
import sys


USB_IDS = {
    "x4c": ("303A:1001", "Espressif USB-Serial/JTAG"),
    "sticky": ("1A86:55D3", "CH343P"),
}


def select_port(devices, target, requested_port=""):
    usb_id, bridge = USB_IDS[target]
    hwid = f"VID:PID={usb_id}"
    ports = [
        device["port"]
        for device in devices
        if hwid in device.get("hwid", "").upper().split()
    ]
    if requested_port:
        resolved = os.path.normcase(os.path.realpath(requested_port))
        if any(char in requested_port for char in "*?[]") or not any(
            os.path.normcase(os.path.realpath(port)) == resolved for port in ports
        ):
            raise ValueError(f"{requested_port} is not a connected {bridge} port ({usb_id}).")
        return requested_port
    if len(ports) != 1:
        raise ValueError(f"Expected one {bridge} port; found {len(ports)}. "
                         "Connect one reader or set PLATFORMIO_UPLOAD_PORT.")
    return ports[0]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=USB_IDS)
    args = parser.parse_args()
    try:
        devices = json.loads(subprocess.check_output(["pio", "device", "list", "--json-output"], text=True))
        print(select_port(devices, args.target))
    except (subprocess.CalledProcessError, json.JSONDecodeError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
