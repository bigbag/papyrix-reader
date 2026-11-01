#include "ClockStandby.h"

#include <Arduino.h>
#include <BootHardware.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <PowerPolicy.h>
#include <TouchBackend.h>
#include <esp_app_desc.h>
#include <esp_attr.h>
#include <sys/time.h>

#include <cstring>
#include <ctime>

#include "../config.h"
#include "../core/Core.h"
#include "../hal/Power.h"
#include "ClockApp.h"

extern GfxRenderer renderer;
extern EpdFontFamily uiFontFamily;
extern EpdFontFamily smallFontFamily;

namespace papyrix::clock_app {
namespace {
RTC_DATA_ATTR ResumeRecord retained{};
volatile bool powerRequested = false;

void IRAM_ATTR capturePower() { powerRequested = true; }

class PowerWatch {
 public:
  PowerWatch() {
    powerRequested = false;
    const auto& input = board::HardwareIdentity::instance().profile().input;
    if (input.power >= 0) attachInterrupt(input.power, capturePower, input.activeHigh ? RISING : FALLING);
  }
  ~PowerWatch() {
    const auto pin = board::HardwareIdentity::instance().profile().input.power;
    if (pin >= 0) detachInterrupt(pin);
    powerRequested = false;
  }
};

ResumeIdentity currentIdentity() {
  const auto& hardware = board::HardwareIdentity::instance();
  ResumeIdentity identity{};
  identity.board = hardware.board();
  identity.panel = hardware.panel();
  identity.panelVariant = hardware.panelVariant();
  std::memcpy(identity.imageSha256, esp_app_get_description()->app_elf_sha256, sizeof(identity.imageSha256));
  return identity;
}

bool validTime(Core& core) {
  std::tm time{};
  return core.clock.canRetainTime() && core.clock.localTime(time);
}

bool refreshAndSleep(Core& core, const ResumeRecord& record, ClockFrame& frame, bool cold) {
  bool fontsReady = !cold;
  for (int attempt = 0; attempt < 2; ++attempt) {
    auto mode = hal::Display::FAST_REFRESH;
    if (cold || attempt) {
      const auto result = attempt ? core.display.recover() : core.display.begin();
      if (result != hal::Display::InitResult::Ok) continue;
      renderer.begin();
      if (!fontsReady) {
        renderer.insertFont(UI_FONT_ID, uiFontFamily);
        renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
        fontsReady = true;
      }
      mode = hal::Display::FULL_REFRESH;
      if (!attempt) {
        renderFrame(renderer, record.lastFrame);
        const auto restored = core.display.restoreMonochromeBaseline();
        if (restored == hal::Display::BaselineRestoreResult::Error) continue;
        if (restored == hal::Display::BaselineRestoreResult::Ready) mode = hal::Display::FAST_REFRESH;
      }
    }
    bool currentMinute = false;
    for (int redraw = 0; redraw < 2; ++redraw) {
      frame.utcSeconds = std::time(nullptr);
      renderFrame(renderer, frame);
      renderer.displayBuffer(redraw ? hal::Display::FAST_REFRESH : mode, true);
      if (!core.display.lastRefreshSucceeded()) break;
      if (frame.utcSeconds / 60 == std::time(nullptr) / 60) {
        currentMinute = true;
        break;
      }
    }
    if (currentMinute && core.display.deepSleep()) return true;
    if (powerWakeRequested()) return false;
  }
  return false;
}

StandbyResult finishStandby(Core& core, ResumeRecord& record) {
  core.input.poll();
  if (powerWakeRequested() || core.input.hasPendingInteraction()) return StandbyResult::Interactive;
  core.wifi.shutdown();
#if PAPYRIX_CAP_TOUCH
  if (!board::stopTouchAcquisition()) return StandbyResult::PowerError;
#endif
  core.input.shutdown();
  core.frontLight.shutdown(false);
  core.storage.shutdown();
  if (powerWakeRequested()) return StandbyResult::Interactive;

  timeval now{};
  if (gettimeofday(&now, nullptr) != 0 || now.tv_sec < 946684800 || now.tv_sec > (INT64_MAX - 999999) / 1000000)
    return StandbyResult::TimeRequired;
  if (now.tv_sec / 60 != record.lastFrame.utcSeconds / 60) return StandbyResult::Interactive;
  const uint64_t duration = nextMinuteDelayUs(static_cast<int64_t>(now.tv_sec) * 1000000 + now.tv_usec);
  if (!duration) return StandbyResult::TimeRequired;
  record.active = record.frameValid = 1;
  sealResume(record);
  if (!validateResume(record, currentIdentity())) return StandbyResult::PowerError;
  std::memcpy(&retained, &record, sizeof(retained));
  if (powerWakeRequested()) {
    clearResume();
    return StandbyResult::Interactive;
  }
  const auto result = hal::enterClockDeepSleep(duration);
  clearResume();
  return powerWakeRequested() || result == ESP_ERR_SLEEP_REJECT ? StandbyResult::Interactive
                                                                : StandbyResult::PowerError;
}
}  // namespace

bool powerWakeRequested() {
  const auto& input = board::HardwareIdentity::instance().profile().input;
  return powerRequested || (input.power >= 0 && digitalRead(input.power) == (input.activeHigh ? HIGH : LOW));
}

bool readResume(ResumeRecord& out) {
  ResumeIdentity expected = retained.identity;
  std::memcpy(expected.imageSha256, esp_app_get_description()->app_elf_sha256, sizeof(expected.imageSha256));
  if (!validateResume(retained, expected)) return false;
  std::memcpy(&out, &retained, sizeof(out));
  return true;
}

void clearResume() { retained.active = 0; }

const char* standbyErrorMessage(StandbyResult result) {
  switch (result) {
    case StandbyResult::TimeRequired:
      return "Clock time is not valid. Set time with Sync Now.";
    case StandbyResult::StorageError:
      return "Clock settings or SD access failed.";
    case StandbyResult::DisplayError:
      return "Clock display update failed.";
    case StandbyResult::PowerError:
      return "Clock power preparation failed.";
    default:
      return "Clock standby failed.";
  }
}

StandbyResult enterStandby(Core& core) {
  clearResume();
  PowerWatch watch;
  if (powerWakeRequested()) return StandbyResult::Interactive;
  core.cpu.unthrottle();
  if (!validTime(core)) return StandbyResult::TimeRequired;
  ResumeRecord record;
  std::memset(static_cast<void*>(&record), 0, sizeof(record));
  if (!prepareStandby(core, record)) return StandbyResult::StorageError;
  if (record.settings.powerMode != PowerMode::PowerSaving) return StandbyResult::Interactive;
  record.identity = currentIdentity();
  if (!core.battery.finishDesignCapacity()) return StandbyResult::PowerError;
  if (!refreshAndSleep(core, record, record.lastFrame, false)) return StandbyResult::DisplayError;
  return finishStandby(core, record);
}

StandbyResult runTimerWake(Core& core, ResumeRecord& record) {
  clearResume();
  if (!validateResume(record, currentIdentity())) return StandbyResult::PowerError;
  PowerWatch watch;
  if (powerWakeRequested()) return StandbyResult::Interactive;
  core.cpu.unthrottle();
  core.clock.init();
  if (!validTime(core)) return StandbyResult::TimeRequired;
  core.battery.init(false);
  core.usb.init(core.battery);
  const uint32_t interval = NTP_SYNC_SECONDS[record.settings.ntpSyncSetting];
  if (!interval) record.nextNtpUtc = 0;
  if (interval && std::time(nullptr) >= record.nextNtpUtc) {
    board::restoreRecoveryStorage(board::HardwareIdentity::instance().profile());
    if (!core.storage.init().ok()) return StandbyResult::StorageError;
    const auto result = synchronize(core, record.settings, false);
    core.storage.shutdown();
    record.nextNtpUtc = nextSyncUtc(std::time(nullptr), interval);
    if (result == SyncResult::Cancelled || powerWakeRequested()) return StandbyResult::Interactive;
    if (result == SyncResult::TimeError || !validTime(core)) return StandbyResult::TimeRequired;
  }
  if (!core.battery.finishDesignCapacity()) return StandbyResult::PowerError;
  if (powerWakeRequested()) return StandbyResult::Interactive;

  board::preparePanelProbe(board::HardwareIdentity::instance().profile());
  ClockFrame frame = record.lastFrame;
  const auto battery = core.battery.readStatus();
  frame.batteryPercent = battery.percentageKnown ? static_cast<int16_t>(battery.percentage) : -1;
  frame.charging = battery.chargingKnown && battery.charging;
  frame.usbConnected = core.usb.isConnected();
  frame.interactive = 0;
  if (!refreshAndSleep(core, record, frame, true)) return StandbyResult::DisplayError;
  record.lastFrame = frame;
  return finishStandby(core, record);
}

}  // namespace papyrix::clock_app
