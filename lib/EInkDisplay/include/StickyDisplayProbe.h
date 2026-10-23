#pragma once

#include <BoardProfile.h>

namespace papyrix::eink {
struct StickyProbeResult {
  bool stable;
  uint8_t revision;
  bool busyHigh;
};
StickyProbeResult probeStickyDisplayController(const board::BoardProfile& profile);
}  // namespace papyrix::eink
