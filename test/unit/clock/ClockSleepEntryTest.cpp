#include <Arduino.h>
#include <BoardSelector.h>
#include <HardwareIdentity.h>
#include <InputManager.h>
#include <PowerPolicy.h>
#include <TargetConfig.h>
#include <TouchBackend.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "hal/Power.h"
#include "test_utils.h"

InputManager inputManager;
namespace {
bool powerPressed = false;
bool touchStops = true;
unsigned pressOnRead = 0, powerReads = 0;
struct SleepEntered {};
int readPin(int pin) {
  const auto& input = papyrix::board::HardwareIdentity::instance().profile().input;
  if (pin == input.power) {
    const bool pressed = powerPressed || (pressOnRead != 0 && ++powerReads >= pressOnRead);
    return pressed == input.activeHigh ? HIGH : LOW;
  }
  return HIGH;
}
void sleepEntry() { throw SleepEntered{}; }
bool latchNeverDrops(int pin) {
  for (size_t index = 0; index < testGpioEventCount; ++index) {
    const auto& event = testGpioEvents[index];
    if (event.pin == pin && event.type == TestGpioEventType::DigitalWrite && event.value == LOW) return false;
  }
  return true;
}
bool powerIdleIsBiased() {
  const auto& input = papyrix::board::HardwareIdentity::instance().profile().input;
  const uint64_t pin = uint64_t{1} << input.power;
  return input.activeHigh ? (testRtcPulldownMask & pin) && !(testRtcPullupMask & pin)
                          : (testRtcPullupMask & pin) && !(testRtcPulldownMask & pin);
}
bool pullsDisabled(int pin) {
  for (size_t index = 0; index < testGpioEventCount; ++index) {
    const auto& event = testGpioEvents[index];
    if (event.pin == pin &&
        (event.type == TestGpioEventType::PullUpDisable || event.type == TestGpioEventType::PullDownDisable))
      return true;
  }
  return false;
}
}
namespace papyrix::board {
void enableTouch(const TouchConfig&) {}
bool stopTouchAcquisition() { return touchStops; }
}

bool checkBoard(papyrix::board::BoardId boardId) {
  using namespace papyrix;
  TestUtils::TestRunner runner("Clock sleep entry");
  auto& identity = board::HardwareIdentity::instance();
  board::BoardSelection selected;
  selected.board = boardId;
  identity.applyBoardSelection(selected, {});
  const auto& profile = identity.profile();
  testSetDigitalReadHook(readPin);
  runner.expectEq(ESP_ERR_INVALID_ARG, hal::enterClockDeepSleep(0), "zero delay cannot start autonomous sleep");
  runner.expectEq(ESP_ERR_INVALID_ARG, hal::enterClockDeepSleep(60000001), "invalid minute deadline cannot sleep indefinitely");
  powerPressed = true;
  runner.expectEq(ESP_ERR_SLEEP_REJECT, hal::enterClockDeepSleep(12345678), "held Power retains interaction");
  powerPressed = false;
  const unsigned initialCalls = testDeepSleepCalls;
  testSleepTimerError = ESP_FAIL;
  runner.expectEq(ESP_FAIL, hal::enterClockDeepSleep(12345678), "timer setup failure remains visible");
  testSleepTimerError = ESP_OK;
  testSleepPowerError = ESP_FAIL;
  runner.expectEq(ESP_FAIL, hal::enterClockDeepSleep(12345678), "Power wake setup failure remains visible");
  runner.expectTrue(testSleepTimerUs == 0 && testSleepGpioMask == 0, "failed setup removes wake sources");
  testSleepPowerError = ESP_OK;
#if !PAPYRIX_TARGET_XTEINK_C3
  testRtcPullError = ESP_FAIL;
  runner.expectEq(ESP_FAIL, hal::enterClockDeepSleep(12345678), "RTC bias failure prevents unarmed standby");
  runner.expectTrue(testSleepTimerUs == 0 && testSleepGpioMask == 0, "RTC bias failure removes wake sources");
  testRtcPullError = ESP_OK;
#endif
#if PAPYRIX_CAP_TOUCH
  touchStops = false;
  runner.expectEq(ESP_ERR_INVALID_STATE, hal::enterClockDeepSleep(12345678), "live touch ownership prevents rail shutdown");
  touchStops = true;
#endif
  runner.expectEq(initialCalls, testDeepSleepCalls, "failed preparation cannot enter deep sleep");
  for (const unsigned read : {2U, 3U}) {
    testResetGpioEvents();
    pressOnRead = read;
    powerReads = 0;
    runner.expectEq(ESP_ERR_SLEEP_REJECT, hal::enterClockDeepSleep(12345678),
                    "Power during preparation keeps the device interactive");
    runner.expectEq(initialCalls, testDeepSleepCalls, "Power cancels entry before the SDK sleep call");
    runner.expectTrue(testSleepTimerUs == 0 && testSleepGpioMask == 0, "cancelled entry removes wake sources");
  }
  pressOnRead = 0;
  testResetGpioEvents();
  runner.expectEq(ESP_ERR_SLEEP_REJECT, hal::enterClockDeepSleep(12345678), "SDK rejection returns for recovery");
  runner.expectTrue(testSleepTimerUs == 0 && testSleepGpioMask == 0, "rejected entry removes all wake sources");
  if (profile.power.latchPin != board::kPinUnused) {
    runner.expectTrue(latchNeverDrops(profile.power.latchPin), "rejected Clock standby cannot cut battery power");
  }
  if (profile.power.latchPin2 != board::kPinUnused) {
    runner.expectTrue(latchNeverDrops(profile.power.latchPin2), "rejected Clock standby retains the second latch");
  }
  testDeepSleepHook = sleepEntry;
  testResetGpioEvents();
  testRtcPullupMask = testRtcPulldownMask = 0;
  bool entered = false;
  try {
    hal::enterClockDeepSleep(12345678);
  } catch (const SleepEntered&) {
    entered = true;
  }
  runner.expectTrue(entered && testSleepTimerUs == 12345678, "successful entry keeps the exact minute deadline armed");
  runner.expectEq(board::powerButtonWakeMask(profile), testSleepGpioMask,
                  "successful entry also arms the declared Power pin");
  runner.expectFalse(pullsDisabled(profile.input.power), "Clock sleep preserves the Power button bias");
  runner.expectTrue(pullsDisabled(2), "Clock sleep still disables unused input pulls");
#if !PAPYRIX_TARGET_XTEINK_C3
  runner.expectTrue(powerIdleIsBiased(), "Clock EXT1 input has a defined released level in the RTC domain");
#endif
  testResetGpioEvents();
  entered = false;
  testRtcPullupMask = testRtcPulldownMask = 0;
  try {
    hal::enterDeepSleepWithHardwareShutdown(false);
  } catch (const SleepEntered&) {
    entered = true;
  }
  runner.expectTrue(entered, "ordinary sleep reaches the SDK sleep entry");
  runner.expectFalse(pullsDisabled(profile.input.power), "ordinary sleep preserves the Power button bias");
  runner.expectTrue(pullsDisabled(2), "ordinary sleep still disables unused input pulls");
#if !PAPYRIX_TARGET_XTEINK_C3
  runner.expectTrue(powerIdleIsBiased(), "ordinary sleep EXT1 input has a defined released level in the RTC domain");
#else
  runner.expectTrue(testRtcPullupMask == 0 && testRtcPulldownMask == 0, "C3 keeps its GPIO wake path");
#endif
  testDeepSleepHook = nullptr;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  identity.clearPanelSelection();
  runner.expectFalse(board::restoreClockPanel(identity, static_cast<eink::DisplayController>(255), 0),
                     "unknown retained panel cannot enable display commands");
  runner.expectFalse(identity.panelResolved(), "rejected panel leaves display disabled");
  runner.expectFalse(board::restoreClockPanel(identity, eink::DisplayController::SSD1677, 255),
                     "unknown variant cannot restore a panel");
  const auto panel = profile.id == board::BoardId::X3 ? eink::DisplayController::UC8253
                    : profile.id == board::BoardId::X4Pro ? eink::DisplayController::UC8179_X4PRO
                                                         : eink::DisplayController::SSD1677;
  const uint8_t variant = profile.id == board::BoardId::X4Pro ? 1 : 0;
  runner.expectTrue(board::restoreClockPanel(identity, panel, variant), "matching retained panel restores without a hardware probe");
  runner.expectTrue(identity.panelResolved() && identity.panel() == panel && identity.panelVariant() == variant,
                    "restored identity selects the validated display driver");
  testSetDigitalReadHook(nullptr);
  return runner.allPassed();
}

int main() {
#if PAPYRIX_TARGET_XTEINK_C3
  const bool x3 = checkBoard(papyrix::board::BoardId::X3);
  const bool x4 = checkBoard(papyrix::board::BoardId::X4);
  return x3 && x4 ? 0 : 1;
#else
  return checkBoard(papyrix::board::bootProfile().id) ? 0 : 1;
#endif
}
