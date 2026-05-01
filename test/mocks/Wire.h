#pragma once

#include <cstddef>
#include <cstdint>

// Minimal Wire (TwoWire) mock for host tests. Only declares the surface used
// by code-under-test; methods are inline no-ops since the unit tests we run
// against this don't exercise the I²C paths (they cover pure helpers like
// BatteryMonitor::percentageFromMillivolts).

class TwoWire {
 public:
  bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
  void end() {}
  void setTimeOut(uint16_t) {}
  void beginTransmission(uint8_t) {}
  size_t write(uint8_t) { return 1; }
  uint8_t endTransmission(bool = true) { return 0; }
  uint8_t requestFrom(int, int) { return 0; }
  int available() { return 0; }
  int read() { return 0; }
};

extern TwoWire Wire;
