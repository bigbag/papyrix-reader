#pragma once

#include <BoardProfile.h>
#include <DisplayController.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstdint>
#include <type_traits>

#include "ClockSettings.h"

namespace papyrix::clock_app {

struct ClockAppearance {
  uint8_t invertedMode = 0;
  uint8_t selectionFillBlack = 1;
  uint8_t selectionTextBlack = 0;
  uint8_t primaryTextBlack = 1;
  uint8_t secondaryTextBlack = 1;
  uint8_t backgroundColor = 0xFF;
  uint8_t screenMarginTop = 9;
  uint8_t screenMarginSide = 3;
  uint8_t itemHeight = 30;
  uint8_t itemSpacing = 0;
  uint8_t itemPaddingX = 8;
  uint8_t itemValuePadding = 20;
  uint8_t orientation = 0;
  uint8_t frontButtonLayout = 0;
};

struct ClockFrame {
  int64_t utcSeconds = 0;
  clock_faces::Face face = clock_faces::Face::Big;
  int8_t utcOffset = 0;
  int8_t dateFormat = 0;
  uint8_t use24h = 1;
  ClockAppearance appearance{};
  int16_t batteryPercent = -1;
  uint8_t charging = 0;
  uint8_t usbConnected = 0;
  uint8_t interactive = 0;
};

struct ResumeIdentity {
  board::BoardId board = board::BoardId::X4;
  eink::DisplayController panel = eink::DisplayController::SSD1677;
  uint8_t panelVariant = 0;
  uint8_t imageSha256[32]{};
};

struct ResumeRecord {
  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t byteLength = 0;
  ResumeIdentity identity{};
  uint8_t active = 0;
  ClockSettings settings{};
  int64_t nextNtpUtc = 0;
  ClockFrame lastFrame{};
  uint8_t frameValid = 0;
  uint32_t crc32 = 0;
};

static_assert(std::is_trivially_copyable_v<ResumeRecord> && std::is_standard_layout_v<ResumeRecord>);
static_assert(sizeof(ResumeRecord) <= 2048);

enum class WakeRoute : uint8_t { NormalBoot, Timer, Interactive };
enum class StandbyResult : uint8_t { Interactive, TimeRequired, StorageError, DisplayError, PowerError };

void sealResume(ResumeRecord& record);
bool validateResume(const ResumeRecord& record, const ResumeIdentity& expected);
WakeRoute classifyWake(const ResumeRecord& record, const ResumeIdentity& expected, esp_reset_reason_t reset,
                       esp_sleep_wakeup_cause_t wake);
uint64_t nextMinuteDelayUs(int64_t utcUs);
int64_t nextSyncUtc(int64_t utcSeconds, uint32_t intervalSeconds);
bool interactionExpired(uint32_t nowMs, uint32_t lastInputMs, bool operationPending);

}  // namespace papyrix::clock_app
