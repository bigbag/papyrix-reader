#include "CrashDebug.h"

#include <Arduino.h>
#include <Logging.h>

#define TAG "CRASHDBG"

namespace papyrix::crashdebug {

namespace {

constexpr uint32_t MAGIC = 0x43524442;  // "CRDB"

struct CrashMarker {
  uint32_t magic = 0;
  uint8_t phase = 0;
  int16_t spine = -1;
  uint8_t attempt = 0;
  uint8_t reserved = 0;
};

RTC_DATA_ATTR CrashMarker rtcCrashMarker;

const char* phaseName(const CrashPhase phase) {
  switch (phase) {
    case CrashPhase::None:
      return "none";
    case CrashPhase::EpubTocSelected:
      return "epub_toc_selected";
    case CrashPhase::EpubTocWorkerStart:
      return "epub_toc_worker_start";
    case CrashPhase::EpubTocExtract:
      return "epub_toc_extract";
    case CrashPhase::EpubTocNormalize:
      return "epub_toc_normalize";
    case CrashPhase::EpubTocParse:
      return "epub_toc_parse";
    case CrashPhase::EpubTocResolved:
      return "epub_toc_resolved";
    case CrashPhase::EpubTocRender:
      return "epub_toc_render";
  }
  return "unknown";
}

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "unknown";
    case ESP_RST_POWERON:
      return "poweron";
    case ESP_RST_EXT:
      return "ext";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "int_wdt";
    case ESP_RST_TASK_WDT:
      return "task_wdt";
    case ESP_RST_WDT:
      return "wdt";
    case ESP_RST_DEEPSLEEP:
      return "deepsleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    case ESP_RST_USB:
      return "usb";
    case ESP_RST_JTAG:
      return "jtag";
    case ESP_RST_EFUSE:
      return "efuse";
    case ESP_RST_PWR_GLITCH:
      return "power_glitch";
    case ESP_RST_CPU_LOCKUP:
      return "cpu_lockup";
    default:
      return "other";
  }
}

bool isAbnormalReset(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
    case ESP_RST_CPU_LOCKUP:
      return true;
    default:
      return false;
  }
}

}  // namespace

void mark(const CrashPhase phase, const int16_t spine, const uint8_t attempt) {
  rtcCrashMarker.magic = MAGIC;
  rtcCrashMarker.phase = static_cast<uint8_t>(phase);
  rtcCrashMarker.spine = spine;
  rtcCrashMarker.attempt = attempt;
}

void clear() { rtcCrashMarker = {}; }

void logBootInfo(const esp_reset_reason_t reason) {
  LOG_ERR(TAG, "Reset reason: %s (%d)", resetReasonName(reason), static_cast<int>(reason));

  if (rtcCrashMarker.magic != MAGIC || rtcCrashMarker.phase == static_cast<uint8_t>(CrashPhase::None)) {
    return;
  }

  LOG_ERR(TAG, "Crash marker present: phase=%s spine=%d attempt=%u",
          phaseName(static_cast<CrashPhase>(rtcCrashMarker.phase)), static_cast<int>(rtcCrashMarker.spine),
          static_cast<unsigned>(rtcCrashMarker.attempt));

  if (!isAbnormalReset(reason)) {
    LOG_ERR(TAG, "Clearing stale crash marker after non-abnormal reset");
  }

  clear();
}

}  // namespace papyrix::crashdebug
