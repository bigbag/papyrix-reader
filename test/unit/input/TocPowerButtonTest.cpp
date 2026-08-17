#include "test_utils.h"

#include <cstdint>

// ============================================================================
// TOC power-button timing through the PRODUCTION TocInputDispatcher
// (src/core/ReaderButtonDispatcher.cpp) — the same unit ReaderState::
// handleTocInput() uses. No duplicated inline logic.
// ============================================================================

#include "core/EventQueue.h"
#include "core/ReaderButtonDispatcher.h"

using papyrix::Button;
using papyrix::Event;
using papyrix::ReaderButtonConfig;
using papyrix::TocInputAction;
using papyrix::TocInputDispatcher;

namespace {

struct TocCase {
  TocInputDispatcher d;
  ReaderButtonConfig config;
  uint32_t powerPressStartedMs = 0;
  uint32_t nowMs = 1000;
  TocInputAction last = TocInputAction::None;

  TocInputAction process(const Event& e) {
    last = d.processEvent(e, nowMs, config, powerPressStartedMs);
    return last;
  }
};

Event press(Button b) { return Event::buttonPress(b); }
Event release(Button b) { return Event::buttonRelease(b); }

}  // namespace

int main() {
  TestUtils::TestRunner runner("TocPowerButton");

  // ============================================
  // Power button: press records time, short release triggers MoveDown
  // ============================================
  {
    TocCase c;
    c.config.powerShortPageTurn = true;
    c.nowMs = 100;
    runner.expectEq(int(TocInputAction::None), int(c.process(press(Button::Power))),
                    "power press -> None (deferred)");
    runner.expectEq(uint32_t(100), c.powerPressStartedMs, "power press records start time");
  }

  // Release without prior press -> None
  {
    TocCase c;
    c.config.powerShortPageTurn = true;
    runner.expectEq(int(TocInputAction::None), int(c.process(release(Button::Power))),
                    "release without press -> None");
  }

  // Unmapped / sleep-mapped short power -> None on release
  {
    TocCase c;
    runner.expectEq(int(TocInputAction::None), int(c.process(release(Button::Power))),
                    "unmapped power release -> None");
  }
  {
    TocCase c;
    c.config.powerShortBookmark = true;  // bookmark mapping: TOC uses page turn only
    c.process(press(Button::Power));
    runner.expectEq(int(TocInputAction::None), int(c.process(release(Button::Power))),
                    "bookmark-mapped power release in TOC -> None");
  }

  // ============================================
  // Short press -> release (under threshold) -> MoveDown
  // ============================================
  {
    TocCase c;
    c.config.powerShortPageTurn = true;
    c.nowMs = 1000;
    c.process(press(Button::Power));
    c.nowMs = 1100;  // 100ms held, under 400ms threshold
    runner.expectEq(int(TocInputAction::MoveDown), int(c.process(release(Button::Power))),
                    "short power release -> MoveDown");
    runner.expectEq(uint32_t(0), c.powerPressStartedMs, "power timer cleared after release");
  }

  // ============================================
  // Long hold (over threshold) -> None
  // ============================================
  {
    TocCase c;
    c.config.powerShortPageTurn = true;
    c.nowMs = 1000;
    c.process(press(Button::Power));
    c.nowMs = 1500;  // 500ms held, over 400ms threshold
    runner.expectEq(int(TocInputAction::None), int(c.process(release(Button::Power))),
                    "long-held power release -> None");
  }

  // Hold exactly at boundary -> None (boundary is exclusive)
  {
    TocCase c;
    c.config.powerShortPageTurn = true;
    c.nowMs = 1000;
    c.process(press(Button::Power));
    c.nowMs = 1400;  // exactly 400ms = threshold
    runner.expectEq(int(TocInputAction::None), int(c.process(release(Button::Power))),
                    "boundary-hold release -> None");
  }

  // ============================================
  // Bookmark mapping still records the press (shared timer with reader mode)
  // ============================================
  {
    TocCase c;
    c.config.powerShortBookmark = true;
    c.nowMs = 500;
    c.process(press(Button::Power));
    runner.expectEq(uint32_t(500), c.powerPressStartedMs, "bookmark mapping records press time");
  }

  // ============================================
  // Existing TOC buttons still work
  // ============================================
  {
    TocCase c;
    runner.expectEq(int(TocInputAction::MoveUp), int(c.process(press(Button::Up))), "Up -> MoveUp");
    runner.expectEq(int(TocInputAction::MoveDown), int(c.process(press(Button::Down))), "Down -> MoveDown");
    runner.expectEq(int(TocInputAction::PageUp), int(c.process(press(Button::Left))), "Left -> PageUp");
    runner.expectEq(int(TocInputAction::PageDown), int(c.process(press(Button::Right))), "Right -> PageDown");
    runner.expectEq(int(TocInputAction::Select), int(c.process(press(Button::Center))), "Center -> Select");
    runner.expectEq(int(TocInputAction::Exit), int(c.process(press(Button::Back))), "Back -> Exit");
  }

  // Release events for non-power buttons are ignored
  {
    TocCase c;
    runner.expectEq(int(TocInputAction::None), int(c.process(release(Button::Up))),
                    "non-power release -> None");
  }

  return runner.allPassed() ? 0 : 1;
}
