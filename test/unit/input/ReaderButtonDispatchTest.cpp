#include "test_utils.h"

#include <cstdint>

// ============================================================================
// Inline types and dispatch logic mirroring ReaderState::update() (lines 441-500)
// Tests the button-to-navigation event dispatch: short/long press distinction,
// holdNavigated_ flag, power button setting, and overlay mode guards.
// ============================================================================

enum class EventType : uint8_t {
  None = 0,
  ButtonPress,
  ButtonRepeat,
  ButtonRelease,
};

enum class Button : uint8_t {
  Up,
  Down,
  Left,
  Right,
  Center,
  Back,
  Power,
};

struct Event {
  EventType type;
  Button button;

  static Event press(Button b) { return {EventType::ButtonPress, b}; }
  static Event repeat(Button b) { return {EventType::ButtonRepeat, b}; }
  static Event release(Button b) { return {EventType::ButtonRelease, b}; }
};

enum class Action {
  None,
  Next,
  Prev,
  NextChapter,
  PrevChapter,
  ToggleOrientation,
  Exit,
};

struct ReaderButtonDispatcher {
  bool holdNavigated_ = false;
  bool menuMode_ = false;
  bool tocMode_ = false;
  bool bookmarkMode_ = false;

  enum PowerAction : uint8_t { PowerIgnore = 0, PowerSleep = 1, PowerPageTurn = 2 };
  uint8_t shortPwrBtn = PowerIgnore;
  uint32_t powerPressStartedMs_ = 0;
  uint16_t powerButtonDuration = 400;

  // Mock time for testing timing guard
  uint32_t currentTimeMs_ = 0;
  uint32_t millis() const { return currentTimeMs_; }

  Action lastAction = Action::None;

  void processEvent(const Event& e) {
    lastAction = Action::None;

    // Mode guards: overlays consume all events
    if (menuMode_ || bookmarkMode_ || tocMode_) {
      return;
    }

    switch (e.type) {
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Back:
            lastAction = Action::Exit;
            break;
          case Button::Power:
            if (shortPwrBtn == PowerPageTurn) {
              powerPressStartedMs_ = millis();
            }
            break;
          default:
            break;
        }
        break;

      case EventType::ButtonRepeat:
        if (!holdNavigated_) {
          switch (e.button) {
            case Button::Right:
            case Button::Down:
              lastAction = Action::NextChapter;
              holdNavigated_ = true;
              break;
            case Button::Left:
            case Button::Up:
              lastAction = Action::PrevChapter;
              holdNavigated_ = true;
              break;
            default:
              break;
          }
        }
        break;

      case EventType::ButtonRelease:
        switch (e.button) {
          case Button::Center:
            lastAction = Action::ToggleOrientation;
            break;
          default:
            if (!holdNavigated_) {
              switch (e.button) {
                case Button::Right:
                case Button::Down:
                  lastAction = Action::Next;
                  break;
                case Button::Left:
                case Button::Up:
                  lastAction = Action::Prev;
                  break;
                case Button::Power:
                  if (shortPwrBtn == PowerPageTurn && powerPressStartedMs_ != 0) {
                    const uint32_t heldMs = millis() - powerPressStartedMs_;
                    if (heldMs < powerButtonDuration) {
                      lastAction = Action::Next;
                    }
                  }
                  break;
                default:
                  break;
              }
            }
            break;
        }
        if (e.button == Button::Power) {
          powerPressStartedMs_ = 0;
        }
        holdNavigated_ = false;
        break;

      default:
        break;
    }
  }
};

int main() {
  TestUtils::TestRunner runner("ReaderButtonDispatch");

  // ============================================
  // Short press: Release without prior Repeat
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::Next), static_cast<int>(d.lastAction),
                    "Short press Right -> Next");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::release(Button::Down));
    runner.expectEq(static_cast<int>(Action::Next), static_cast<int>(d.lastAction),
                    "Short press Down -> Next");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::release(Button::Left));
    runner.expectEq(static_cast<int>(Action::Prev), static_cast<int>(d.lastAction),
                    "Short press Left -> Prev");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::release(Button::Up));
    runner.expectEq(static_cast<int>(Action::Prev), static_cast<int>(d.lastAction),
                    "Short press Up -> Prev");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::press(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Press alone (Right) -> None");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::press(Button::Left));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Press alone (Left) -> None");
  }

  // ============================================
  // Long press: Repeat triggers chapter nav
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Right));
    runner.expectEq(static_cast<int>(Action::NextChapter), static_cast<int>(d.lastAction),
                    "Long press Right -> NextChapter");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Down));
    runner.expectEq(static_cast<int>(Action::NextChapter), static_cast<int>(d.lastAction),
                    "Long press Down -> NextChapter");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Left));
    runner.expectEq(static_cast<int>(Action::PrevChapter), static_cast<int>(d.lastAction),
                    "Long press Left -> PrevChapter");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Up));
    runner.expectEq(static_cast<int>(Action::PrevChapter), static_cast<int>(d.lastAction),
                    "Long press Up -> PrevChapter");
  }

  // ============================================
  // Release after Repeat: holdNavigated_ suppresses page nav
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Right));
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Release after Repeat (Right) -> None (suppressed)");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Left));
    d.processEvent(Event::release(Button::Left));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Release after Repeat (Left) -> None (suppressed)");
  }

  // ============================================
  // Multiple Repeats: only first triggers chapter nav
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Right));
    runner.expectEq(static_cast<int>(Action::NextChapter), static_cast<int>(d.lastAction),
                    "First Repeat Right -> NextChapter");

    d.processEvent(Event::repeat(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Second Repeat Right -> None (holdNavigated_ blocks)");

    d.processEvent(Event::repeat(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Third Repeat Right -> None (still blocked)");
  }

  // ============================================
  // holdNavigated_ reset cycle
  // ============================================

  {
    ReaderButtonDispatcher d;
    // Long press
    d.processEvent(Event::repeat(Button::Right));
    runner.expectTrue(d.holdNavigated_, "After Repeat: holdNavigated_ is true");

    // Release resets
    d.processEvent(Event::release(Button::Right));
    runner.expectFalse(d.holdNavigated_, "After Release: holdNavigated_ is false");

    // Short press now works
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::Next), static_cast<int>(d.lastAction),
                    "Short press after reset -> Next");
  }

  // ============================================
  // Two consecutive long presses both trigger chapter nav
  // ============================================

  {
    ReaderButtonDispatcher d;
    // First long press
    d.processEvent(Event::repeat(Button::Left));
    runner.expectEq(static_cast<int>(Action::PrevChapter), static_cast<int>(d.lastAction),
                    "First long press Left -> PrevChapter");
    d.processEvent(Event::release(Button::Left));

    // Second long press
    d.processEvent(Event::repeat(Button::Left));
    runner.expectEq(static_cast<int>(Action::PrevChapter), static_cast<int>(d.lastAction),
                    "Second long press Left -> PrevChapter (reset worked)");
    d.processEvent(Event::release(Button::Left));
  }

  // ============================================
  // Center button
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::press(Button::Center));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Center Press -> None");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::release(Button::Center));
    runner.expectEq(static_cast<int>(Action::ToggleOrientation), static_cast<int>(d.lastAction),
                    "Center Release -> ToggleOrientation");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Center));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Center Repeat -> None");
  }

  // ============================================
  // Back button
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::press(Button::Back));
    runner.expectEq(static_cast<int>(Action::Exit), static_cast<int>(d.lastAction),
                    "Back Press -> Exit");
  }

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::release(Button::Back));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Back Release -> None");
  }

  // ============================================
  // Power button
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerPageTurn;
    d.currentTimeMs_ = 100;
    d.processEvent(Event::press(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Press (PageTurn) -> None (deferred to release)");
    runner.expectEq(uint32_t(100), d.powerPressStartedMs_,
                    "Power Press records timestamp");
  }

  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerIgnore;
    d.processEvent(Event::press(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Press (Ignore) -> None");
    runner.expectEq(uint32_t(0), d.powerPressStartedMs_,
                    "Power Press (Ignore) does not record timestamp");
  }

  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerSleep;
    d.processEvent(Event::press(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Press (Sleep) -> None");
  }

  // Release without prior press -> None (powerPressStartedMs_ is 0)
  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerPageTurn;
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Release without press -> None (no timestamp)");
  }

  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerIgnore;
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Release (Ignore) -> None");
  }

  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerSleep;
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Release (Sleep) -> None");
  }

  // Short press+release (under duration threshold) -> Next
  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerPageTurn;
    d.currentTimeMs_ = 1000;
    d.processEvent(Event::press(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power press+release: press -> None");
    d.currentTimeMs_ = 1100;  // 100ms held, under 400ms threshold
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::Next), static_cast<int>(d.lastAction),
                    "Power press+release (short) -> Next");
    runner.expectEq(uint32_t(0), d.powerPressStartedMs_,
                    "Power release resets timestamp");
  }

  // Long hold (over duration threshold) -> None (sleep, not page turn)
  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerPageTurn;
    d.currentTimeMs_ = 1000;
    d.processEvent(Event::press(Button::Power));
    d.currentTimeMs_ = 1500;  // 500ms held, over 400ms threshold
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power long hold -> None (held past duration)");
  }

  // Hold exactly at boundary -> None (not strictly less than)
  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerPageTurn;
    d.currentTimeMs_ = 1000;
    d.processEvent(Event::press(Button::Power));
    d.currentTimeMs_ = 1400;  // exactly 400ms = threshold
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power hold at exact threshold -> None");
  }

  {
    ReaderButtonDispatcher d;
    d.shortPwrBtn = ReaderButtonDispatcher::PowerPageTurn;
    d.holdNavigated_ = true;
    d.powerPressStartedMs_ = 100;
    d.currentTimeMs_ = 150;
    d.processEvent(Event::release(Button::Power));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "Power Release suppressed by holdNavigated_");
  }

  // ============================================
  // Mode guards: overlays consume all events
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.menuMode_ = true;
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "menuMode: Right Release -> None (consumed)");

    d.processEvent(Event::repeat(Button::Left));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "menuMode: Left Repeat -> None (consumed)");

    d.processEvent(Event::press(Button::Center));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "menuMode: Center Press -> None (consumed)");

    d.processEvent(Event::press(Button::Back));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "menuMode: Back Press -> None (consumed)");
  }

  {
    ReaderButtonDispatcher d;
    d.tocMode_ = true;
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "tocMode: Right Release -> None (consumed)");

    d.processEvent(Event::repeat(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "tocMode: Right Repeat -> None (consumed)");
  }

  {
    ReaderButtonDispatcher d;
    d.bookmarkMode_ = true;
    d.processEvent(Event::release(Button::Left));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "bookmarkMode: Left Release -> None (consumed)");

    d.processEvent(Event::press(Button::Center));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "bookmarkMode: Center Press -> None (consumed)");
  }

  // ============================================
  // Mode off -> on -> off: navigation resumes
  // ============================================

  {
    ReaderButtonDispatcher d;

    // Normal navigation works
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::Next), static_cast<int>(d.lastAction),
                    "Before mode: Right Release -> Next");

    // Enter menu mode
    d.menuMode_ = true;
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::None), static_cast<int>(d.lastAction),
                    "During mode: Right Release -> None");

    // Exit menu mode
    d.menuMode_ = false;
    d.processEvent(Event::release(Button::Right));
    runner.expectEq(static_cast<int>(Action::Next), static_cast<int>(d.lastAction),
                    "After mode: Right Release -> Next (resumed)");
  }

  // ============================================
  // holdNavigated_ not affected by mode toggle
  // ============================================

  {
    ReaderButtonDispatcher d;
    d.processEvent(Event::repeat(Button::Right));
    runner.expectTrue(d.holdNavigated_, "holdNavigated_ set before mode toggle");

    // Mode on/off should not reset holdNavigated_
    d.menuMode_ = true;
    d.processEvent(Event::release(Button::Right));
    d.menuMode_ = false;
    runner.expectTrue(d.holdNavigated_,
                      "holdNavigated_ preserved through mode (release consumed by mode)");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
