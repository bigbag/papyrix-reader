#pragma once
#include <BatteryMonitor.h>

#define BAT_GPIO0 0  // Battery voltage

inline BatteryMonitor& getBatteryMonitor() {
  static BatteryMonitor instance(BAT_GPIO0);
  return instance;
}

#define batteryMonitor getBatteryMonitor()

// Defined in main.cpp; reads UART0_RXD which floats high when USB is plugged in.
bool isUsbConnected();
