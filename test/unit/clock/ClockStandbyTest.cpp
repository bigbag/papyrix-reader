#include <SPI.h>
#include <Arduino.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <esp_app_desc.h>
#include <sys/time.h>

#include <cassert>
#include <cstring>
#include <ctime>
#include <vector>

#include "apps/ClockApp.h"
#include "apps/ClockStandby.h"
#include "core/Core.h"
#include "test_utils.h"

using namespace papyrix;
using namespace papyrix::clock_app;
namespace {
int64_t utcUs = INT64_C(1798761540) * 1000000;
esp_app_desc_t image;
void (*powerInterrupt)() = nullptr;
bool powerHeld = false, powerDuringDraw = false;
bool inputDuringDraw = false, pendingInput = false;
bool gaugeOk = true, settingsOk = true, storageOk = true, panelSleepOk = true;
int refreshFailures = 0, initFailures = 0, resets = 0, mounts = 0, syncs = 0;
bool configuredGauge = false, sleeping = false;
uint64_t sleepDelay = 0;
int64_t correction = 0;
int64_t shutdownDelayUs = 0;
SyncResult syncResult = SyncResult::Success;
ClockSettings settings;
std::vector<ClockFrame> frames;
struct SleepEntered {};

int readPin(int) { return powerHeld ? LOW : HIGH; }
void resetScenario() {
  clearResume();
  powerHeld = powerDuringDraw = false;
  inputDuringDraw = pendingInput = false;
  SPI.end();
  gaugeOk = settingsOk = storageOk = panelSleepOk = true;
  refreshFailures = initFailures = resets = mounts = syncs = 0;
  configuredGauge = sleeping = false;
  correction = 0;
  shutdownDelayUs = 0;
  syncResult = SyncResult::Success;
  frames.clear();
  settings = ClockSettings{};
  settings.powerMode = PowerMode::PowerSaving;
  utcUs = INT64_C(1798761540) * 1000000;
  testSetManualMillis(0);
}
}

const esp_app_desc_t* esp_app_get_description() { return &image; }
void attachInterrupt(uint8_t, void (*handler)(), int) { powerInterrupt = handler; }
void detachInterrupt(uint8_t) { powerInterrupt = nullptr; }
extern "C" std::time_t time(std::time_t* out) noexcept {
  const auto seconds = static_cast<std::time_t>(utcUs / 1000000);
  if (out) *out = seconds;
  return seconds;
}
extern "C" int gettimeofday(timeval* out, void*) noexcept {
  out->tv_sec = utcUs / 1000000;
  out->tv_usec = utcUs % 1000000;
  return 0;
}

namespace papyrix {
ContentHandle::ContentHandle() {}
ContentHandle::~ContentHandle() { assert(type == ContentType::None); }
Core core;
namespace board {
void preparePanelProbe(const BoardProfile&) {}
}
namespace hal {
Display::Display() : frameBuffer(frameBuffer0) {}
Display::InitResult Display::begin() {
  if (initFailures > 0) { --initFailures; return InitResult::HardwareError; }
  return InitResult::Ok;
}
Display::InitResult Display::recover() { ++resets; return begin(); }
Display::BaselineRestoreResult Display::restoreMonochromeBaseline() { return BaselineRestoreResult::Ready; }
void Display::displayBuffer(RefreshMode, bool) {
  refreshSucceeded_ = refreshFailures == 0;
  if (refreshFailures > 0) --refreshFailures;
  utcUs += 200000;
}
bool Display::deepSleep() { return panelSleepOk; }
void Clock::init() {}
bool Clock::localTime(std::tm& out) const {
  const auto seconds = std::time(nullptr);
  return gmtime_r(&seconds, &out) && out.tm_year >= 100;
}
void Battery::init(bool configure) { configuredGauge = configure; }
bool Battery::finishDesignCapacity() { return gaugeOk; }
Battery::Status Battery::readStatus() const {
  Status result{};
  result.percentageKnown = true;
  result.percentage = 75;
  return result;
}
void Usb::init(const Battery&) {}
Usb::Status Usb::readStatus() const { return {}; }
void WifiRadio::shutdown() { initialized_ = connected_ = false; }
void Cpu::unthrottle() {}
void FrontLight::shutdown(bool) {}
void Input::shutdown() { utcUs += shutdownDelayUs; }
void Input::poll() {}
bool Input::hasPendingInteraction() const { return pendingInput; }
Result<void> Storage::init() {
  ++mounts;
  const auto& profile = board::HardwareIdentity::instance().profile();
  mounted_ = storageOk && SPI.active && SPI.sclkPin == profile.display.sclk &&
             SPI.misoPin == profile.storage.spiMiso && SPI.mosiPin == profile.display.mosi;
  return mounted_ ? Ok() : ErrVoid(Error::SdCardNotFound);
}
void Storage::shutdown() { mounted_ = false; }
esp_err_t enterClockDeepSleep(uint64_t duration) {
  sleepDelay = duration;
  sleeping = true;
  throw SleepEntered{};
}
}
namespace clock_app {
bool prepareStandby(Core&, ResumeRecord& record) {
  record.settings = settings;
  record.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[settings.ntpSyncSetting]);
  record.lastFrame.utcSeconds = std::time(nullptr);
  return settingsOk;
}
void renderFrame(GfxRenderer&, const ClockFrame& frame) {
  frames.push_back(frame);
  if (powerDuringDraw && powerInterrupt) powerInterrupt();
  if (inputDuringDraw) pendingInput = true;
}
SyncResult synchronize(Core&, const ClockSettings&, bool interactive) {
  assert(!interactive);
  ++syncs;
  utcUs += correction * 1000000;
  return syncResult;
}
}
}
GfxRenderer renderer(core.display);
EpdFontFamily uiFontFamily(nullptr), smallFontFamily(nullptr);
void GfxRenderer::begin() {}
void GfxRenderer::insertFont(int, EpdFontFamily) {}
void GfxRenderer::freeBitmapRowBuffers() {}
void GfxRenderer::displayBuffer(hal::Display::RefreshMode mode, bool powerOff) const {
  display_.displayBuffer(mode, powerOff);
}

int main() {
  TestUtils::TestRunner runner("Clock standby lifecycle");
  auto& identity = board::HardwareIdentity::instance();
  board::BoardSelection selected{};
  selected.board = board::BoardId::X4;
  identity.applyBoardSelection(selected, {});
  identity.setPanel(eink::DisplayController::SSD1677, board::PanelSelectionSource::Fixed);
  testSetDigitalReadHook(readPin);
  ResumeRecord record;
  const auto enter = [&] {
    try { return enterStandby(core); } catch (const SleepEntered&) { return StandbyResult::Interactive; }
  };
  const auto minute = [&] {
    sleeping = false;
    SPI.end();
    try { return runTimerWake(core, record); } catch (const SleepEntered&) { return StandbyResult::Interactive; }
  };

  resetScenario();
  runner.expectFalse(readResume(record), "cold startup has no autonomous Clock record");
  enter();
  runner.expectTrue(sleeping && readResume(record), "successful standby publishes a complete retained frame");
  runner.expectEq(59800000ULL, static_cast<unsigned long long>(sleepDelay), "render time does not shift the next minute");
  const auto initialDeadline = record.nextNtpUtc;
  utcUs += sleepDelay;
  frames.clear();
  minute();
  runner.expectTrue(sleeping && readResume(record), "minute wake returns to autonomous standby");
  runner.expectEq(0, mounts, "ordinary minute wake does not mount storage");
  runner.expectFalse(configuredGauge, "minute wake cannot program the fuel gauge");
  runner.expectEq(initialDeadline, record.nextNtpUtc, "millis reset does not postpone automatic sync");
  runner.expectTrue(frames.size() == 2 && frames[1].utcSeconds - frames[0].utcSeconds == 60,
                    "minute wake reconstructs the old frame and draws current wall time");
  std::tm nextDate{};
  const std::time_t frameTime = record.lastFrame.utcSeconds;
  gmtime_r(&frameTime, &nextDate);
  runner.expectTrue(nextDate.tm_year == 127 && nextDate.tm_mon == 0 && nextDate.tm_mday == 1 && nextDate.tm_hour == 0,
                    "the minute cycle crosses midnight into the next year");

  resetScenario();
  utcUs += 59800000;
  enter();
  runner.expectTrue(readResume(record) && record.lastFrame.utcSeconds / 60 == std::time(nullptr) / 60,
                    "a refresh across the minute boundary cannot leave the previous minute asleep");

  for (const bool timerWake : {false, true}) {
    for (const int64_t delayUs : {4000, 5000, 10000}) {
      resetScenario();
      if (timerWake) {
        enter();
        assert(readResume(record));
      }
      utcUs = INT64_C(1798761540) * 1000000 + 59795000;
      shutdownDelayUs = delayUs;
      const auto result = timerWake ? minute() : enter();
      if (delayUs == 4000) {
        runner.expectTrue(sleeping && readResume(record) &&
                              record.lastFrame.utcSeconds / 60 == std::time(nullptr) / 60 && sleepDelay == 1000,
                          timerWake ? "timer shutdown before the boundary keeps the current minute asleep"
                                    : "entry shutdown before the boundary keeps the current minute asleep");
      } else {
        runner.expectTrue(result == StandbyResult::Interactive && !sleeping && !readResume(record),
                          timerWake ? "timer shutdown across the boundary returns Clock without stale resume"
                                    : "entry shutdown across the boundary returns Clock without stale resume");
      }
    }
  }

  for (const auto result : {SyncResult::Success, SyncResult::Failed, SyncResult::Unavailable}) {
    for (const int64_t shift : {-3600, 3600}) {
      resetScenario();
      enter();
      assert(readResume(record));
      utcUs = record.nextNtpUtc * 1000000;
      correction = shift;
      syncResult = result;
      const auto corrected = std::time(nullptr) + shift;
      minute();
      runner.expectTrue(sleeping && readResume(record), "a bounded sync attempt returns to standby");
      runner.expectEq(corrected + 10800, record.nextNtpUtc, "sync success or failure schedules from corrected UTC");
      runner.expectTrue(syncs == 1 && mounts == 1 && !core.storage.isMounted(), "only due sync mounts and closes storage");
    }
  }

  resetScenario();
  settings.ntpSyncSetting = 3;
  enter();
  assert(readResume(record));
  utcUs += INT64_C(86400) * 1000000;
  minute();
  runner.expectTrue(sleeping && syncs == 0 && mounts == 0, "Off keeps radio and credential storage idle");

  resetScenario();
  powerDuringDraw = true;
  runner.expectTrue(enter() == StandbyResult::Interactive && !sleeping && !readResume(record),
                    "a short Power press during rendering returns controls and clears resume");
  resetScenario();
  inputDuringDraw = true;
  runner.expectTrue(enter() == StandbyResult::Interactive && !sleeping && !readResume(record),
                    "input received during rendering prevents standby and keeps controls available");
  resetScenario();
  gaugeOk = false;
  runner.expectTrue(enter() == StandbyResult::PowerError && !sleeping && !readResume(record),
                    "failed gauge close-out prevents autonomous standby");
  resetScenario();
  settingsOk = false;
  runner.expectTrue(enter() == StandbyResult::StorageError && !sleeping && !readResume(record),
                    "failed settings publication preserves the interactive path");
  resetScenario();
  refreshFailures = 2;
  runner.expectTrue(enter() == StandbyResult::DisplayError && resets == 1 && !readResume(record),
                    "display failure gets one reset then clears autonomous resume");
  resetScenario();
  enter();
  assert(readResume(record));
  initFailures = 2;
  runner.expectTrue(minute() == StandbyResult::DisplayError && resets == 1 && !readResume(record),
                    "timer initialization failure gets one reset and no further wake");
  resetScenario();
  enter();
  assert(readResume(record));
  utcUs = record.nextNtpUtc * 1000000;
  storageOk = false;
  runner.expectTrue(minute() == StandbyResult::StorageError && syncs == 0 && !readResume(record),
                    "missing sync storage cannot leave an autonomous resume record");
  resetScenario();
  enter();
  assert(readResume(record));
  utcUs = record.nextNtpUtc * 1000000;
  syncResult = SyncResult::TimeError;
  runner.expectTrue(minute() == StandbyResult::TimeRequired && !core.storage.isMounted() && !readResume(record),
                    "failed RTC synchronization returns to time setup with storage closed");
  resetScenario();
  utcUs = 0;
  runner.expectTrue(enter() == StandbyResult::TimeRequired && !sleeping, "invalid entry time requests time setup");
  resetScenario();
  enter();
  assert(readResume(record));
  utcUs = 0;
  runner.expectTrue(minute() == StandbyResult::TimeRequired && !readResume(record),
                    "invalid restored time cannot publish another Clock wake");
  resetScenario();
  enter();
  ++image.app_elf_sha256[0];
  runner.expectFalse(readResume(record), "a different firmware image cannot resume retained Clock state");
  testSetDigitalReadHook(nullptr);
  testUseRealtimeMillis();
  return runner.allPassed() ? 0 : 1;
}
