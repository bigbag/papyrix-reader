#pragma once

#include <Arduino.h>

namespace TestUtils {

inline bool heldOutputRemains(int pin, int expected) {
  bool output = false, held = true;
  int level = LOW;
  for (size_t index = 0; index < testGpioEventCount; ++index) {
    const auto& event = testGpioEvents[index];
    if (event.pin != pin) continue;
    if (event.type == TestGpioEventType::PinMode) output = event.value == OUTPUT;
    if (event.type == TestGpioEventType::DigitalWrite) level = event.value;
    if (event.type == TestGpioEventType::HoldDisable) held = false;
    if (event.type == TestGpioEventType::HoldEnable) held = true;
    if (!held && (!output || level != expected)) return false;
  }
  return !held && output && level == expected;
}

}  // namespace TestUtils
