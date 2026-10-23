#include <Arduino.h>
#include <BoardProfiles.h>
#include <BootHardware.h>
#include <PowerPolicy.h>

#include "test_utils.h"

using namespace papyrix::board;

namespace {
bool event(TestGpioEventType type, int pin, int value = 0) {
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    const auto& e = testGpioEvents[i];
    if (e.type == type && e.pin == pin && e.value == value) return true;
  }
  return false;
}
bool wrote(int pin, int value) { return event(TestGpioEventType::DigitalWrite, pin, value); }
bool held(int pin) { return event(TestGpioEventType::HoldEnable, pin); }
}

int main() {
  TestUtils::TestRunner runner("Sticky board power");
  const auto& profile = bootProfile();
  testResetGpioEvents();
  earlyInit(profile);
  runner.expectTrue(wrote(45, HIGH) && wrote(46, HIGH), "boot retains battery power");
  runner.expectTrue(wrote(39, LOW) && held(39), "boot preserves charging");
  runner.expectTrue(wrote(38, LOW), "unused microphone is off");
  testResetGpioEvents();
  prepareDeepSleepPins(profile, false);
  runner.expectTrue(wrote(45, HIGH) && held(45) && wrote(46, HIGH) && held(46), "sleep retains both latches");
  runner.expectTrue(wrote(10, LOW) && wrote(42, LOW) && wrote(47, LOW), "sleep removes peripheral power");
  runner.expectFalse(event(TestGpioEventType::PinMode, -1, INPUT), "unused SDMMC pins never reach GPIO");
  runner.expectTrue(wrote(39, LOW) && held(39), "sleep preserves charging");
  testResetGpioEvents();
  preparePanelProbe(profile);
  runner.expectTrue(wrote(8, HIGH) && wrote(10, HIGH), "probe powers SD and deselects actual CS");
  testResetGpioEvents();
  shutdownRecoveryRails(profile);
  runner.expectTrue(event(TestGpioEventType::PinMode, 12, INPUT), "recovery releases SD MISO");
  runner.expectTrue(event(TestGpioEventType::PinMode, 13, INPUT), "recovery releases shared clock");
  runner.expectTrue(wrote(10, LOW) && wrote(47, LOW), "recovery disables SD and display rails");
  testResetGpioEvents();
  restoreRecoveryStorage(profile);
  runner.expectTrue(wrote(8, HIGH) && wrote(10, HIGH), "recovery restores actual SD pins");
  runner.expectEq(uint64_t{1} << 4, powerButtonWakeMask(profile), "only Power wakes Sticky");
  return runner.allPassed() ? 0 : 1;
}
