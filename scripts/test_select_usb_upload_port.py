#!/usr/bin/env python3
"""Check USB port selection for Classic and Sticky uploads."""

import tempfile
import unittest
from pathlib import Path

from select_usb_upload_port import select_port


class SelectUsbUploadPortTest(unittest.TestCase):
    def test_selects_only_matching_port(self):
        devices = [
            {"port": "/dev/ttyS0", "hwid": "n/a"},
            {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=303A:1001 SER=CLASSIC"},
            {"port": "/dev/ttyUSB0", "hwid": "USB VID:PID=10C4:EA60 SER=OTHER"},
        ]
        self.assertEqual(select_port(devices, "x4c"), "/dev/ttyACM0")

    def test_rejects_no_matching_port(self):
        with self.assertRaisesRegex(ValueError, "found 0"):
            select_port([{"port": "/dev/ttyS0", "hwid": "n/a"}], "x4c")

    def test_rejects_ambiguous_ports(self):
        devices = [
            {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=303A:1001 SER=ONE"},
            {"port": "/dev/ttyACM1", "hwid": "USB VID:PID=303A:1001 SER=TWO"},
        ]
        with self.assertRaisesRegex(ValueError, "found 2"):
            select_port(devices, "x4c")

    def test_selects_sticky_bridge_not_espressif_port(self):
        devices = [
            {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=303A:1001 SER=CLASSIC"},
            {"port": "/dev/ttyACM1", "hwid": "USB VID:PID=1a86:55d3 SER=STICKY"},
        ]
        self.assertEqual(select_port(devices, "sticky"), "/dev/ttyACM1")

    def test_rejects_no_sticky_bridge(self):
        with self.assertRaisesRegex(ValueError, "found 0"):
            select_port([{"port": "/dev/ttyACM0", "hwid": "USB VID:PID=303A:1001 SER=CLASSIC"}], "sticky")

    def test_rejects_ambiguous_sticky_bridges(self):
        devices = [
            {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=1A86:55D3 SER=ONE"},
            {"port": "/dev/ttyACM1", "hwid": "USB VID:PID=1A86:55D3 SER=TWO"},
        ]
        with self.assertRaisesRegex(ValueError, "found 2"):
            select_port(devices, "sticky")

    def test_sticky_override_matches_the_bridge_through_an_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            alias = Path(directory) / "sticky"
            alias.symlink_to("/dev/ttyACM1")
            devices = [
                {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=1A86:55D3 SER=OTHER"},
                {"port": "/dev/ttyACM1", "hwid": "USB VID:PID=1A86:55D3 SER=STICKY"},
            ]
            self.assertEqual(select_port(devices, "sticky", str(alias)), str(alias))

    def test_sticky_override_rejects_pro_and_unknown_ports(self):
        devices = [
            {"port": "COM5", "hwid": "USB VID:PID=303A:1001 SER=PRO"},
            {"port": "COM6", "hwid": "USB VID:PID=1A86:55D3 SER=STICKY"},
        ]
        for requested in ("COM5", "COM7", "COM*"):
            with self.subTest(requested=requested), self.assertRaises(ValueError):
                select_port(devices, "sticky", requested)


if __name__ == "__main__":
    unittest.main()
