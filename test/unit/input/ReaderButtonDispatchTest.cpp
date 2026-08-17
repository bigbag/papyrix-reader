#include "test_utils.h"

#include <cstdint>

// ============================================================================
// Reader-mode button dispatch through the PRODUCTION ReaderButtonDispatcher
// (src/core/ReaderButtonDispatcher.cpp). If ReaderState's update() rewiring
// diverges from this logic, these tests fail — no duplicated inline logic.
// ============================================================================

#include "core/EventQueue.h"
#include "core/ReaderButtonDispatcher.h"

using papyrix::Button;
using papyrix::Event;
using papyrix::ReaderButtonAction;
using papyrix::ReaderButtonConfig;
using papyrix::ReaderButtonDispatcher;

namespace {

struct DispatchCase {
  ReaderButtonDispatcher d;
  ReaderButtonConfig config;
  uint32_t nowMs = 1000;
  ReaderButtonAction last = ReaderButtonAction::None;

  ReaderButtonAction process(const Event& e) {
    last = d.processEvent(e, nowMs, config);
    return last;
  }
};

Event press(Button b) { return Event::buttonPress(b); }
Event repeat(Button b) { return Event::buttonRepeat(b); }
Event release(Button b) { return Event::buttonRelease(b); }

}  // namespace

int main() {
  TestUtils::TestRunner runner("ReaderButtonDispatch");

  // ============================================
  // Short press: Release without prior Repeat
  // ============================================
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::Next), int(c.process(release(Button::Right))),
                    "Short press Right -> Next");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::Next), int(c.process(release(Button::Down))),
                    "Short press Down -> Next");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::Prev), int(c.process(release(Button::Left))),
                    "Short press Left -> Prev");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::Prev), int(c.process(release(Button::Up))), "Short press Up -> Prev");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(press(Button::Right))),
                    "Press alone (Right) -> None");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(press(Button::Left))),
                    "Press alone (Left) -> None");
  }

  // ============================================
  // Long press: Repeat triggers chapter nav
  // ============================================
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::NextChapter), int(c.process(repeat(Button::Right))),
                    "Long press Right -> NextChapter");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::NextChapter), int(c.process(repeat(Button::Down))),
                    "Long press Down -> NextChapter");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::PrevChapter), int(c.process(repeat(Button::Left))),
                    "Long press Left -> PrevChapter");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::PrevChapter), int(c.process(repeat(Button::Up))),
                    "Long press Up -> PrevChapter");
  }

  // ============================================
  // Release after Repeat: hold tracking suppresses page nav
  // ============================================
  {
    DispatchCase c;
    c.process(repeat(Button::Right));
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(release(Button::Right))),
                    "Release after Repeat -> None (suppressed)");
  }
  {
    DispatchCase c;
    c.process(repeat(Button::Left));
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(release(Button::Left))),
                    "Release after Repeat (Left) -> None (suppressed)");
  }

  // ============================================
  // Multiple Repeats: only first triggers chapter nav
  // ============================================
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::NextChapter), int(c.process(repeat(Button::Right))),
                    "First repeat -> NextChapter");
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(repeat(Button::Right))),
                    "Second repeat -> None");
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(repeat(Button::Right))),
                    "Third repeat -> None");
  }

  // ============================================
  // Hold reset cycle: short press works again after a long press
  // ============================================
  {
    DispatchCase c;
    c.process(repeat(Button::Right));
    c.process(release(Button::Right));
    runner.expectEq(int(ReaderButtonAction::Next), int(c.process(release(Button::Right))),
                    "Short press after long-press cycle -> Next");
  }

  // ============================================
  // Two consecutive long presses both trigger chapter nav
  // ============================================
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::PrevChapter), int(c.process(repeat(Button::Left))),
                    "First long press -> PrevChapter");
    c.process(release(Button::Left));
    runner.expectEq(int(ReaderButtonAction::PrevChapter), int(c.process(repeat(Button::Left))),
                    "Second long press -> PrevChapter");
    c.process(release(Button::Left));
  }

  // ============================================
  // Center / Back buttons
  // ============================================
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::Menu), int(c.process(press(Button::Center))),
                    "Center press -> Menu");
  }
  {
    DispatchCase c;
    runner.expectEq(int(ReaderButtonAction::Exit), int(c.process(press(Button::Back))), "Back press -> Exit");
  }

  // ============================================
  // Short power button (page turn mapping)
  // ============================================
  {
    DispatchCase c;
    c.config.powerShortPageTurn = true;
    c.process(press(Button::Power));
    runner.expectEq(int(ReaderButtonAction::ShortPowerPageTurn), int(c.process(release(Button::Power))),
                    "Short power release -> ShortPowerPageTurn");
  }
  {
    DispatchCase c;
    c.config.powerShortPageTurn = true;
    c.process(press(Button::Power));
    c.nowMs += c.config.powerButtonDurationMs;  // held too long
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(release(Button::Power))),
                    "Long-held power release -> None");
  }
  {
    DispatchCase c;
    c.config.powerShortPageTurn = false;  // unmapped short power
    c.process(press(Button::Power));
    runner.expectEq(uint32_t(0), c.d.powerPressStartedMs(), "unmapped: press not recorded");
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(release(Button::Power))),
                    "Unmapped short power release -> None");
  }
  {
    DispatchCase c;
    c.config.powerShortPageTurn = true;
    c.process(press(Button::Power));
    c.process(repeat(Button::Power));
    // Power repeats do not set hold tracking (only nav buttons do), so a
    // short release still fires the mapped action — same as ReaderState.
    runner.expectEq(int(ReaderButtonAction::ShortPowerPageTurn), int(c.process(release(Button::Power))),
                    "Power release after repeat -> ShortPowerPageTurn");
  }

  // ============================================
  // Short power button (bookmark mapping)
  // ============================================
  {
    DispatchCase c;
    c.config.powerShortBookmark = true;
    c.process(press(Button::Power));
    runner.expectEq(int(ReaderButtonAction::ShortPowerBookmark), int(c.process(release(Button::Power))),
                    "Short power release -> ShortPowerBookmark");
  }

  // ============================================
  // Power press timing resets after release
  // ============================================
  {
    DispatchCase c;
    c.config.powerShortPageTurn = true;
    c.process(press(Button::Power));
    c.process(release(Button::Power));
    runner.expectEq(uint32_t(0), c.d.powerPressStartedMs(), "power timer reset after release");
    // A stray release without a press must not navigate
    runner.expectEq(int(ReaderButtonAction::None), int(c.process(release(Button::Power))),
                    "Release without press -> None");
  }

  return runner.allPassed() ? 0 : 1;
}
