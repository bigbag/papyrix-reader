#pragma once

#include "../../../mocks/Arduino.h"

inline long map(long value, long fromLow, long fromHigh, long toLow, long toHigh) {
  return (value - fromLow) * (toHigh - toLow) / (fromHigh - fromLow) + toLow;
}
inline long constrain(long value, long low, long high) { return value < low ? low : value > high ? high : value; }
