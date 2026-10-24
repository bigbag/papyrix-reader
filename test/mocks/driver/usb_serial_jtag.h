#pragma once

inline bool testNativeUsbConnected = true;
inline bool usb_serial_jtag_is_connected() { return testNativeUsbConnected; }
