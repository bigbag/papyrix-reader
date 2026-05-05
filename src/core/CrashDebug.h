#pragma once

#include <esp_system.h>

#include <cstdint>

namespace papyrix::crashdebug {

enum class CrashPhase : uint8_t {
  None = 0,
  EpubTocSelected = 1,
  EpubTocWorkerStart = 2,
  EpubTocExtract = 3,
  EpubTocNormalize = 4,
  EpubTocParse = 5,
  EpubTocResolved = 6,
  EpubTocRender = 7,
};

void mark(CrashPhase phase, int16_t spine = -1, uint8_t attempt = 0);
void clear();
void logBootInfo(esp_reset_reason_t reason);

}  // namespace papyrix::crashdebug
