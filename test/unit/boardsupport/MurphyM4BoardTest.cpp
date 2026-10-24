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
}

int main() {
  TestUtils::TestRunner runner("M4 boot, sleep, and recovery");
  const auto& profile = bootProfile();
  testResetGpioEvents();
  earlyInit(profile);
  runner.expectTrue(wrote(10, HIGH), "boot without latch disables the active-low SD rail");
  for (int pin : {0, 1, 2})
    runner.expectTrue(event(TestGpioEventType::PinMode, pin, INPUT_PULLUP), "boot configures independent buttons");
  testResetGpioEvents();
  prepareDeepSleepPins(profile, false);
  size_t resetWrites = 0;
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    const auto& e = testGpioEvents[i];
    if (e.type == TestGpioEventType::DigitalWrite && e.pin == 7) ++resetWrites;
  }
  runner.expectTrue(resetWrites == 1 && wrote(7, HIGH) && !wrote(7, LOW),
                    "shared display/touch reset has one sleep level");
  runner.expectTrue(wrote(10, HIGH) && wrote(45, HIGH), "sleep disables both active-low rails");
  runner.expectTrue(wrote(47, LOW) && wrote(48, LOW), "sleep turns off both frontlight channels");
  runner.expectEq(uint64_t{1}, powerButtonWakeMask(profile), "only GPIO0 wakes M4");
  for (int pin : {16, 15, 17, 18, 11, 14})
    runner.expectTrue(event(TestGpioEventType::PinMode, pin, INPUT), "sleep releases every SD signal");
  runner.expectFalse(event(TestGpioEventType::PinMode, -1, INPUT), "unused pins never reach GPIO");
  testResetGpioEvents();
  shutdownRecoveryRails(profile);
  for (int pin : {16, 15, 17, 18, 11, 14})
    runner.expectTrue(event(TestGpioEventType::PinMode, pin, INPUT), "recovery releases every SD signal");
  runner.expectTrue(wrote(10, HIGH), "recovery disables SD power");
  testResetGpioEvents();
  restoreRecoveryStorage(profile);
  runner.expectTrue(wrote(10, LOW), "recovery restores SD power");
  return runner.allPassed() ? 0 : 1;
}
