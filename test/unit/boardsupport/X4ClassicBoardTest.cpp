#include <Arduino.h>
#include <BoardProfiles.h>
#include <BootHardware.h>
#include <PowerPolicy.h>
#include <cassert>
#include "test_gpio_state.h"

namespace {
bool event(TestGpioEventType type, int pin, int value) {
  for (size_t i = 0; i < testGpioEventCount; ++i)
    if (testGpioEvents[i].type == type && testGpioEvents[i].pin == pin && testGpioEvents[i].value == value) return true;
  return false;
}
void buttonsAreInputs() {
  for (int pin : {2, 5, 8, 9}) assert(!event(TestGpioEventType::PinMode, pin, OUTPUT));
}
}

int main() {
  using namespace papyrix::board;
  const auto& profile = bootProfile();
  testResetGpioEvents();
  earlyInit(profile, BootPurpose::ClockTimer);
  assert(TestUtils::heldOutputRemains(1, HIGH));
  assert(!event(TestGpioEventType::PinMode, 6, OUTPUT));
  prepareClockSleepPins(profile);
  assert(!event(TestGpioEventType::DigitalWrite, 1, LOW));
  testResetGpioEvents();
  earlyInit(profile, BootPurpose::Interactive);
  assert(event(TestGpioEventType::DigitalWrite, 1, HIGH));
  assert(event(TestGpioEventType::DigitalWrite, 6, HIGH));
  assert(event(TestGpioEventType::HoldDisable, 10, 0));
  buttonsAreInputs();
  for (bool externalPower : {false, true}) {
    testResetGpioEvents();
    prepareDeepSleepPins(profile, externalPower);
    buttonsAreInputs();
    for (int pin : {1, 6, 10}) {
      assert(event(TestGpioEventType::DigitalWrite, pin, HIGH));
      assert(event(TestGpioEventType::HoldEnable, pin, 0));
    }
    for (int pin : {40, 41, 42}) assert(event(TestGpioEventType::PinMode, pin, INPUT));
    assert(powerButtonWakeMask(profile) == (1ULL << 3));
  }
}
