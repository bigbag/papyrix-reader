#include "ClockPower.h"

#include <uzlib.h>

#include <cstddef>
#include <cstring>
#include <limits>

namespace papyrix::clock_app {
namespace {
constexpr uint32_t kMagic = 0x434C4B31;
constexpr uint16_t kVersion = 1;
constexpr int64_t kValidTime = 946684800;
constexpr int64_t kMinuteUs = 60000000;

bool validFrame(const ClockFrame& frame) {
  const auto& appearance = frame.appearance;
  return frame.utcSeconds >= kValidTime && frame.utcSeconds <= INT64_MAX / 1000000 &&
         clock_faces::isSelectable(frame.face) && frame.utcOffset >= -12 && frame.utcOffset <= 14 &&
         frame.dateFormat >= 0 && frame.dateFormat <= 3 && frame.use24h <= 1 && frame.batteryPercent >= -1 &&
         frame.batteryPercent <= 100 && frame.charging <= 1 && frame.usbConnected <= 1 && frame.interactive <= 1 &&
         appearance.orientation <= 3 && appearance.frontButtonLayout <= 1 && appearance.invertedMode <= 1 &&
         appearance.selectionFillBlack <= 1 && appearance.selectionTextBlack <= 1 && appearance.primaryTextBlack <= 1 &&
         appearance.secondaryTextBlack <= 1 && (appearance.backgroundColor == 0 || appearance.backgroundColor == 0xFF);
}
}  // namespace

void sealResume(ResumeRecord& record) {
  record.magic = kMagic;
  record.version = kVersion;
  record.byteLength = sizeof(record);
  record.crc32 = ~uzlib_crc32(&record, offsetof(ResumeRecord, crc32), UINT32_MAX);
}

bool validateResume(const ResumeRecord& record, const ResumeIdentity& expected) {
  if (record.magic != kMagic || record.version != kVersion || record.byteLength != sizeof(record) ||
      record.crc32 != ~uzlib_crc32(&record, offsetof(ResumeRecord, crc32), UINT32_MAX))
    return false;
  if (record.active != 1 || record.frameValid != 1 || record.settings.powerMode != PowerMode::PowerSaving ||
      !validSettings(record.settings) || !validFrame(record.lastFrame) || record.nextNtpUtc < 0)
    return false;
  const auto& identity = record.identity;
  return identity.board <= board::BoardId::MurphyM4 && identity.board == expected.board &&
         identity.panel == expected.panel && identity.panelVariant == expected.panelVariant &&
         std::memcmp(identity.imageSha256, expected.imageSha256, sizeof(identity.imageSha256)) == 0;
}

WakeRoute classifyWake(const ResumeRecord& record, const ResumeIdentity& expected, esp_reset_reason_t reset,
                       esp_sleep_wakeup_cause_t wake) {
  if (reset != ESP_RST_DEEPSLEEP || !validateResume(record, expected)) return WakeRoute::NormalBoot;
  if (wake == ESP_SLEEP_WAKEUP_TIMER) return WakeRoute::Timer;
  if (wake == ESP_SLEEP_WAKEUP_GPIO || wake == ESP_SLEEP_WAKEUP_EXT1) return WakeRoute::Interactive;
  return WakeRoute::NormalBoot;
}

uint64_t nextMinuteDelayUs(int64_t utcUs) {
  if (utcUs < kValidTime * 1000000) return 0;
  const int64_t delay = kMinuteUs - utcUs % kMinuteUs;
  return utcUs <= INT64_MAX - delay ? static_cast<uint64_t>(delay) : 0;
}

int64_t nextSyncUtc(int64_t utcSeconds, uint32_t intervalSeconds) {
  // Unsynchronized wall time must still permit the next automatic sync attempt.
  if (!intervalSeconds || utcSeconds < 0 || utcSeconds > INT64_MAX - intervalSeconds) return 0;
  return utcSeconds + intervalSeconds;
}

bool interactionExpired(uint32_t nowMs, uint32_t lastInputMs, bool operationPending) {
  return !operationPending && static_cast<uint32_t>(nowMs - lastInputMs) >= 15000;
}

}  // namespace papyrix::clock_app
