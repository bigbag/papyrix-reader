#pragma once

#include <Wire.h>

namespace papyrix::board {
inline TwoWire& i2cBus(uint8_t busIndex) {
#if PAPYRIX_TARGET_STICKY
  if (busIndex == 1) return Wire1;
#else
  (void)busIndex;
#endif
  return Wire;
}
}  // namespace papyrix::board
