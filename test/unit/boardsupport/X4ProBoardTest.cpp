#include <Arduino.h>
#include <BoardProfiles.h>
#include <BootHardware.h>
#include <FrontLightBackend.h>
#include <PowerPolicy.h>
#include <X4ProBoard.h>

#include "test_utils.h"

namespace {

bool matches(size_t index, TestGpioEventType type, int pin, int value) {
  if (index >= testGpioEventCount) return false;
  const auto& event = testGpioEvents[index];
  return event.type == type && event.pin == pin && event.value == value;
}

int eventIndex(TestGpioEventType type, int pin, int value) {
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    if (matches(i, type, pin, value)) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("X4 Pro board backend");
  const auto& profile = papyrix::board::bootProfile();

  testResetGpioEvents();
  papyrix::board::earlyInit(profile);
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 1, HIGH) >= 0, "boot asserts the master latch");
  const int sdOffAtBoot = eventIndex(TestGpioEventType::DigitalWrite, 5, HIGH);
  runner.expectTrue(sdOffAtBoot > eventIndex(TestGpioEventType::DigitalWrite, 1, HIGH),
                    "boot disables the SD rail after asserting the master latch");
  runner.expectTrue(sdOffAtBoot < eventIndex(TestGpioEventType::DigitalWrite, 2, LOW),
                    "boot controls the SD rail before touch startup");
  for (const int pin : {0, 7, 3}) {
    runner.expectTrue(eventIndex(TestGpioEventType::PinMode, pin, INPUT_PULLUP) >= 0,
                      "boot configures active-low buttons with pull-ups");
  }
  runner.expectTrue(
      eventIndex(TestGpioEventType::DigitalWrite, 4, LOW) < eventIndex(TestGpioEventType::DigitalWrite, 4, HIGH),
      "boot releases touch reset after assertion");
  runner.expectTrue(
      eventIndex(TestGpioEventType::PinMode, 10, INPUT) > eventIndex(TestGpioEventType::DigitalWrite, 10, LOW),
      "boot releases the touch interrupt after address selection");

  testResetGpioEvents();
  papyrix::board::x4pro::prepareCharacterization(profile);
  runner.expectEq(21, static_cast<int>(testGpioEventCount), "characterization leaves the panel bus idle");
  runner.expectTrue(matches(1, TestGpioEventType::PinMode, 12, INPUT), "leaves panel clock as input");
  runner.expectTrue(matches(3, TestGpioEventType::PinMode, 11, INPUT), "leaves panel data as input");
  runner.expectTrue(matches(5, TestGpioEventType::PinMode, 13, INPUT), "leaves panel chip select as input");
  runner.expectTrue(matches(7, TestGpioEventType::PinMode, 18, INPUT), "leaves panel data command as input");
  runner.expectTrue(matches(9, TestGpioEventType::PinMode, 14, INPUT), "leaves panel reset as input");
  runner.expectTrue(matches(11, TestGpioEventType::PinMode, 6, INPUT), "leaves panel busy as input");
  runner.expectTrue(matches(14, TestGpioEventType::DigitalWrite, 5, HIGH), "keeps the active-low SD rail off");
  runner.expectTrue(matches(17, TestGpioEventType::DigitalWrite, 8, LOW), "keeps the cool front light off");
  runner.expectTrue(matches(20, TestGpioEventType::DigitalWrite, 9, LOW), "keeps the warm front light off");
  testResetGpioEvents();
  papyrix::board::prepareDeepSleepPins(profile, false);
  const int sdOff = eventIndex(TestGpioEventType::DigitalWrite, 5, HIGH);
  runner.expectTrue(sdOff >= 0, "sleep disables the active-low SD rail");
  for (const int pin : {40, 41, 42}) {
    const int floating = eventIndex(TestGpioEventType::PinMode, pin, INPUT);
    runner.expectTrue(floating >= 0 && floating < sdOff, "sleep floats each SD bus pin before rail-off");
  }
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 4, LOW) >= 0, "sleep holds GT911 reset low");
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 2, HIGH) >= 0, "sleep disables the touch rail");
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 14, HIGH) >= 0, "sleep keeps panel reset high");
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 1, HIGH) >= 0, "sleep keeps the master latch asserted");
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 8, LOW) >= 0, "sleep drives front-light cool off");
  runner.expectTrue(eventIndex(TestGpioEventType::DigitalWrite, 9, LOW) >= 0, "sleep drives front-light warm off");
  runner.expectTrue(eventIndex(TestGpioEventType::HoldEnable, 8, 0) >= 0, "sleep holds front-light cool off");
  runner.expectTrue(eventIndex(TestGpioEventType::HoldEnable, 9, 0) >= 0, "sleep holds front-light warm off");
  runner.expectTrue(
      eventIndex(TestGpioEventType::HoldEnable, 1, 0) > eventIndex(TestGpioEventType::DigitalWrite, 1, HIGH),
      "sleep holds the asserted master latch");

  testResetGpioEvents();
  papyrix::board::FrontLightBackend wakeLight;
  runner.expectTrue(wakeLight.begin(), "wake re-initializes the front light");
  runner.expectTrue(eventIndex(TestGpioEventType::HoldDisable, 8, 0) >= 0, "wake releases the cool hold");
  runner.expectTrue(eventIndex(TestGpioEventType::HoldDisable, 9, 0) >= 0, "wake releases the warm hold");

  testResetGpioEvents();
  papyrix::board::shutdownRecoveryRails(profile);
  const int recoveryOff = eventIndex(TestGpioEventType::DigitalWrite, 5, HIGH);
  runner.expectTrue(recoveryOff >= 0, "recovery disables SD power");
  for (const int pin : {40, 41, 42}) {
    const int floating = eventIndex(TestGpioEventType::PinMode, pin, INPUT);
    runner.expectTrue(floating >= 0 && floating < recoveryOff, "recovery floats the SD bus before rail-off");
  }

  return runner.allPassed() ? 0 : 1;
}
