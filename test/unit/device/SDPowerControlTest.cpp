#include <Arduino.h>
#include <SDPowerControl.h>

#include "test_utils.h"

namespace {
size_t position(TestGpioEventType type, int pin, int value) {
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    const auto& e = testGpioEvents[i];
    if (e.type == type && e.pin == pin && e.value == value) return i;
  }
  return testGpioEventCount;
}
}

int main() {
  TestUtils::TestRunner runner("SD power control");
  testResetGpioEvents();
  papyrix::sd::prepareSdForDisplayProbe(-1, 12);
  papyrix::sd::disableSdPower(-1, 12, 8, 7, 10);
  runner.expectEq(size_t{0}, testGpioEventCount, "unassigned power remains untouched");
  for (const auto sticky : {false, true}) {
    const int8_t power = sticky ? 10 : 13;
    const int8_t cs = sticky ? 8 : 12;
    const int8_t clock = sticky ? 13 : 8;
    const int8_t miso = sticky ? 12 : 7;
    const int8_t mosi = sticky ? 14 : 10;
    testResetGpioEvents();
    papyrix::sd::prepareSdForDisplayProbe(power, cs);
    runner.expectTrue(position(TestGpioEventType::DigitalWrite, power, HIGH) <
                          position(TestGpioEventType::DigitalWrite, cs, HIGH), "power precedes deselection");
    runner.expectEq(uint32_t{10}, testDelayTotalMs(), "power settles before probe");
    testResetGpioEvents();
    papyrix::sd::disableSdPower(power, cs, clock, miso, mosi);
    const auto off = position(TestGpioEventType::DigitalWrite, power, LOW);
    runner.expectTrue(off < testGpioEventCount, "rail turns off");
    for (const auto pin : {cs, clock, miso, mosi}) {
      runner.expectTrue(position(TestGpioEventType::PinMode, pin, INPUT) < off,
                        "every shared bus pin is released before power off");
    }
  }
  return runner.allPassed() ? 0 : 1;
}
