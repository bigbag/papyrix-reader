#!/usr/bin/env python3
"""Check USB-Serial/JTAG port selection for Classic uploads."""

import unittest

from select_usb_jtag_port import select_port


class SelectUsbJtagPortTest(unittest.TestCase):
    def test_selects_only_matching_port(self):
        devices = [
            {"port": "/dev/ttyS0", "hwid": "n/a"},
            {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=303A:1001 SER=CLASSIC"},
            {"port": "/dev/ttyUSB0", "hwid": "USB VID:PID=10C4:EA60 SER=OTHER"},
        ]
        self.assertEqual(select_port(devices), "/dev/ttyACM0")

    def test_rejects_no_matching_port(self):
        with self.assertRaisesRegex(ValueError, "found 0"):
            select_port([{"port": "/dev/ttyS0", "hwid": "n/a"}])

    def test_rejects_ambiguous_ports(self):
        devices = [
            {"port": "/dev/ttyACM0", "hwid": "USB VID:PID=303A:1001 SER=ONE"},
            {"port": "/dev/ttyACM1", "hwid": "USB VID:PID=303A:1001 SER=TWO"},
        ]
        with self.assertRaisesRegex(ValueError, "found 2"):
            select_port(devices)


if __name__ == "__main__":
    unittest.main()
