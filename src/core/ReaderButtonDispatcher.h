#pragma once

#include <cstdint>

#include "EventQueue.h"

namespace papyrix {

// Actions ReaderState must perform in response to a reader-mode button event.
// Pure decision output: ReaderState owns navigation, overlays and rendering.
enum class ReaderButtonAction : uint8_t {
  None,
  Next,
  Prev,
  NextChapter,
  PrevChapter,
  Menu,
  Exit,
  ShortPowerPageTurn,
  ShortPowerBookmark,
};

struct ReaderButtonConfig {
  bool powerShortPageTurn = false;
  bool powerShortBookmark = false;
  uint32_t powerButtonDurationMs = 400;
};

// Decision-only button dispatcher for ReaderState's non-overlay mode. Owns
// hold tracking and power-press timing so production and the input dispatch
// tests exercise the exact same logic.
class ReaderButtonDispatcher {
 public:
  ReaderButtonAction processEvent(const Event& e, uint32_t nowMs, const ReaderButtonConfig& config);

  // Power-press timing shared with overlay handlers (e.g. TOC) in ReaderState.
  uint32_t powerPressStartedMs() const { return powerPressStartedMs_; }
  void setPowerPressStartedMs(uint32_t ms) { powerPressStartedMs_ = ms; }

 private:
  bool holdNavigated_ = false;
  uint32_t powerPressStartedMs_ = 0;
};

// Actions for the TOC overlay.
enum class TocInputAction : uint8_t {
  None,
  MoveUp,
  MoveDown,
  PageUp,
  PageDown,
  Select,
  Exit,
};

// Decision-only dispatcher for ReaderState::handleTocInput. Power-press
// timing is owned by the reader dispatcher and passed by reference so both
// modes share one timer, mirroring ReaderState's member usage.
class TocInputDispatcher {
 public:
  TocInputAction processEvent(const Event& e, uint32_t nowMs, const ReaderButtonConfig& config,
                              uint32_t& powerPressStartedMs);
};

}  // namespace papyrix
