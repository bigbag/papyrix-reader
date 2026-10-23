#include <Arduino.h>
#include <BoardProfiles.h>
#include <BootHardware.h>
#include <PowerPolicy.h>
#include <SPI.h>

#include "test_utils.h"

using papyrix::board::BoardId;
using papyrix::board::bootProfile;
using papyrix::board::findProfile;


namespace {

bool matches(size_t index, TestGpioEventType type, int pin, int value) {
  if (index >= testGpioEventCount) return false;
  const auto& event = testGpioEvents[index];
  return event.type == type && event.pin == pin && event.value == value;
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
  papyrix::board::earlyInit(bootProfile());
  runner.expectTrue(matches(0, TestGpioEventType::HoldDisable, 13, 0), "X4 boot releases its GPIO13 hold");
  runner.expectTrue(matches(1, TestGpioEventType::PinMode, 13, OUTPUT), "X4 boot configures GPIO13 as output");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 13, HIGH), "X4 boot keeps battery power latched");

  testResetGpioEvents();
  papyrix::board::preparePanelProbe(x3);
  runner.expectTrue(matches(0, TestGpioEventType::HoldDisable, 13, 0), "X3 releases the SD rail hold");
  runner.expectTrue(matches(1, TestGpioEventType::PinMode, 13, OUTPUT), "X3 sets the SD rail output");
  runner.expectTrue(matches(2, TestGpioEventType::DigitalWrite, 13, HIGH), "X3 powers the SD rail");
  runner.expectTrue(matches(3, TestGpioEventType::Delay, -1, 10), "X3 waits for the SD rail");
  runner.expectTrue(matches(4, TestGpioEventType::PinMode, 12, OUTPUT), "X3 sets SD CS as an output");
  runner.expectTrue(matches(5, TestGpioEventType::DigitalWrite, 12, HIGH), "X3 drives SD CS high");

  testResetGpioEvents();
  papyrix::board::preparePanelProbe(x4);
  runner.expectEq(0, static_cast<int>(testGpioEventCount), "X4 panel probe does not change an SD rail");

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
