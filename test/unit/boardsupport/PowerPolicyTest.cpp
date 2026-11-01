#include <Arduino.h>
#include <BoardProfiles.h>
#include <BootHardware.h>
#include <PowerPolicy.h>
#include <SPI.h>

#include "test_utils.h"
#include "test_gpio_state.h"

using papyrix::board::BoardId;
using papyrix::board::bootProfile;
using papyrix::board::findProfile;


namespace {

bool matches(size_t index, TestGpioEventType type, int pin, int value) {
  if (index >= testGpioEventCount) return false;
  const auto& event = testGpioEvents[index];
  return event.type == type && event.pin == pin && event.value == value;
}

bool drivesHighDuringSleep(int pin) {
  bool output = false, high = false, held = false, sleepSelected = true;
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    const auto& event = testGpioEvents[i];
    if (event.pin != pin) continue;
    switch (event.type) {
      case TestGpioEventType::PinMode:
        output = event.value == OUTPUT;
        break;
      case TestGpioEventType::DigitalWrite:
        high = event.value == HIGH;
        break;
      case TestGpioEventType::HoldEnable:
        held = true;
        break;
      case TestGpioEventType::HoldDisable:
        held = false;
        break;
      case TestGpioEventType::SleepSelectDisable:
        sleepSelected = false;
        break;
      default:
        break;
    }
  }
  // The C3 SDK isolates sleep-selected pins unless they are held.
  return output && high && (held || !sleepSelected);
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("Board power policy");
  const auto& x3 = *findProfile(BoardId::X3);
  const auto& x4 = *findProfile(BoardId::X4);
  testResetGpioEvents();
  papyrix::board::releaseDeepSleepHolds();
  runner.expectEq(1, static_cast<int>(testGpioEventCount), "boot releases the global deep-sleep hold");
  runner.expectTrue(matches(0, TestGpioEventType::DeepSleepHoldDisable, -1, 0),
                    "USB and button reads use released GPIO state");


  testResetGpioEvents();
  papyrix::board::earlyInit(bootProfile(), papyrix::board::BootPurpose::Interactive);
  runner.expectTrue(TestUtils::heldOutputRemains(13, HIGH), "Clock wake never releases an unconfigured battery latch");
  runner.expectTrue(drivesHighDuringSleep(13), "X4 keeps battery power during SDK light-sleep isolation");
  papyrix::board::prepareDeepSleepPins(x4, false);
  runner.expectFalse(drivesHighDuringSleep(13), "battery shutdown still disconnects the retained latch");
  papyrix::board::prepareDeepSleepPins(x4, true);
  runner.expectTrue(drivesHighDuringSleep(13), "USB shutdown still retains power for charging");
  testResetGpioEvents();
  papyrix::board::prepareClockSleepPins(x4);
  runner.expectTrue(drivesHighDuringSleep(13), "Clock standby retains X4 battery power without USB");
  testResetGpioEvents();
  papyrix::board::preparePanelProbe(x3);
  const size_t clockSleepStart = testGpioEventCount;
  papyrix::board::prepareClockSleepPins(x3);
  runner.expectTrue(drivesHighDuringSleep(13), "X3 Clock standby retains power for timer wake");
  bool interruptedPower = false;
  for (size_t i = clockSleepStart; i < testGpioEventCount; ++i) {
    interruptedPower |= matches(i, TestGpioEventType::DigitalWrite, 13, LOW);
  }
  runner.expectFalse(interruptedPower, "X3 Clock sleep never pulses its power control LOW");
  testResetGpioEvents();
  papyrix::board::earlyInit(x3, papyrix::board::BootPurpose::ClockTimer);
  papyrix::board::releaseDeepSleepHolds();
  papyrix::board::preparePanelProbe(x3);
  runner.expectTrue(TestUtils::heldOutputRemains(13, HIGH), "X3 timer boot preserves the retained power level");

  for (const auto* profile : {&x3, &x4}) {
    testResetGpioEvents();
    papyrix::board::preparePanelProbe(*profile);
    bool deselected = false;
    bool powered = profile->storage.powerPin < 0;
    for (size_t i = 0; i < testGpioEventCount; ++i) {
      const auto& event = testGpioEvents[i];
      if (event.type != TestGpioEventType::DigitalWrite || event.value != HIGH) continue;
      if (event.pin == profile->storage.spiCs) deselected = true;
      if (event.pin == profile->storage.powerPin) powered = true;
    }
    runner.expectTrue(deselected && powered, "shared SPI keeps the SD card powered and deselected");
  }

  testResetGpioEvents();
  papyrix::board::prepareDeepSleepPins(x3, false);
  runner.expectTrue(matches(0, TestGpioEventType::PinMode, 12, INPUT), "X3 releases SD CS before rail shutdown");
  runner.expectTrue(matches(4, TestGpioEventType::HoldDisable, 13, 0), "X3 releases the SD rail hold");
  runner.expectTrue(matches(6, TestGpioEventType::DigitalWrite, 13, LOW), "X3 powers off the SD rail");
  runner.expectTrue(matches(7, TestGpioEventType::HoldEnable, 13, 0), "X3 holds the SD rail off");
  runner.expectTrue(matches(10, TestGpioEventType::DigitalWrite, 5, HIGH), "X3 keeps display reset high");
  runner.expectTrue(matches(11, TestGpioEventType::HoldEnable, 5, 0), "X3 holds display reset high");

  testResetGpioEvents();
  papyrix::board::prepareDeepSleepPins(x4, false);
  runner.expectTrue(matches(0, TestGpioEventType::HoldDisable, 13, 0), "X4 battery sleep releases the old hold");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 13, LOW), "X4 battery sleep disconnects the latch");
  runner.expectTrue(matches(3, TestGpioEventType::HoldEnable, 13, 0), "X4 battery sleep holds the latch off");

  testResetGpioEvents();
  papyrix::board::prepareDeepSleepPins(x4, true);
  runner.expectTrue(matches(0, TestGpioEventType::HoldDisable, 13, 0), "X4 USB sleep releases the old hold");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 13, HIGH), "X4 USB sleep keeps the latch connected");
  runner.expectTrue(matches(3, TestGpioEventType::HoldEnable, 13, 0), "X4 USB sleep holds the latch on");

  runner.expectEq(static_cast<uint64_t>(1) << 3, papyrix::board::powerButtonWakeMask(x3), "X3 wakes on GPIO3");
  runner.expectEq(static_cast<uint64_t>(1) << 3, papyrix::board::powerButtonWakeMask(x4), "X4 wakes on GPIO3");
  for (const auto* profile : {&x3, &x4}) {
    SPI.end();
    papyrix::board::shutdownRecoveryRails(*profile);
    papyrix::board::restoreRecoveryStorage(*profile);
    SPI.begin();
    runner.expectTrue(SPI.sclkPin == 8 && SPI.misoPin == 7 && SPI.mosiPin == 10 && SPI.selectPin == 12,
                      profile->id == BoardId::X3 ? "powered SPI storage remount keeps X3 pins"
                                                : "ungated SPI storage remount keeps X4 pins");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
