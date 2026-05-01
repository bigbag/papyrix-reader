#pragma once
#include <BatteryMonitor.h>

#include "drivers/Device.h"

#define BAT_GPIO0 0  // Battery voltage (X4 ADC). Same pin is X3's I²C SCL.

// Lazily constructs the right BatteryMonitor for this device:
// - X4: ADC on BAT_GPIO0
// - X3: BQ27220 fuel gauge on I²C (SDA=20, SCL=0)
// First call must follow Device::probe() (called in earlyInit before any UI render).
inline BatteryMonitor& getBatteryMonitor() {
  static BatteryMonitor instance = papyrix::drivers::Device::instance().isX3()
                                       ? BatteryMonitor(BatteryMonitor::Bq27220Config{20, 0, 400000})
                                       : BatteryMonitor(BAT_GPIO0);
  return instance;
}

#define batteryMonitor getBatteryMonitor()

// Defined in main.cpp; reads UART0_RXD which floats high when USB is plugged in.
bool isUsbConnected();
