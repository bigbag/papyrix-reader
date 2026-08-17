#include "ReaderButtonDispatcher.h"

namespace papyrix {

ReaderButtonAction ReaderButtonDispatcher::processEvent(const Event& e, const uint32_t nowMs,
                                                        const ReaderButtonConfig& config) {
  switch (e.type) {
    case EventType::ButtonPress:
      switch (e.button) {
        case Button::Center:
          return ReaderButtonAction::Menu;
        case Button::Back:
          return ReaderButtonAction::Exit;
        case Button::Power:
          if (config.powerShortPageTurn || config.powerShortBookmark) {
            powerPressStartedMs_ = nowMs;
          }
          return ReaderButtonAction::None;
        default:
          return ReaderButtonAction::None;
      }

    case EventType::ButtonRepeat:
      if (!holdNavigated_) {
        switch (e.button) {
          case Button::Right:
          case Button::Down:
            holdNavigated_ = true;
            return ReaderButtonAction::NextChapter;
          case Button::Left:
          case Button::Up:
            holdNavigated_ = true;
            return ReaderButtonAction::PrevChapter;
          default:
            return ReaderButtonAction::None;
        }
      }
      return ReaderButtonAction::None;

    case EventType::ButtonRelease: {
      ReaderButtonAction action = ReaderButtonAction::None;
      if (!holdNavigated_) {
        switch (e.button) {
          case Button::Right:
          case Button::Down:
            action = ReaderButtonAction::Next;
            break;
          case Button::Left:
          case Button::Up:
            action = ReaderButtonAction::Prev;
            break;
          case Button::Power:
            if (powerPressStartedMs_ != 0) {
              const uint32_t heldMs = nowMs - powerPressStartedMs_;
              if (heldMs < config.powerButtonDurationMs) {
                if (config.powerShortPageTurn) {
                  action = ReaderButtonAction::ShortPowerPageTurn;
                } else if (config.powerShortBookmark) {
                  action = ReaderButtonAction::ShortPowerBookmark;
                }
              }
            }
            break;
          default:
            break;
        }
      }
      if (e.button == Button::Power) {
        powerPressStartedMs_ = 0;
      }
      holdNavigated_ = false;
      return action;
    }

    default:
      return ReaderButtonAction::None;
  }
}

TocInputAction TocInputDispatcher::processEvent(const Event& e, const uint32_t nowMs, const ReaderButtonConfig& config,
                                                uint32_t& powerPressStartedMs) {
  if (e.button == Button::Power && e.type == EventType::ButtonRelease) {
    if (powerPressStartedMs != 0) {
      const uint32_t heldMs = nowMs - powerPressStartedMs;
      if (heldMs < config.powerButtonDurationMs && config.powerShortPageTurn) {
        powerPressStartedMs = 0;
        return TocInputAction::MoveDown;
      }
    }
    powerPressStartedMs = 0;
    return TocInputAction::None;
  }

  if (e.type != EventType::ButtonPress && e.type != EventType::ButtonRepeat) return TocInputAction::None;

  switch (e.button) {
    case Button::Up:
      return TocInputAction::MoveUp;
    case Button::Down:
      return TocInputAction::MoveDown;
    case Button::Left:
      return TocInputAction::PageUp;
    case Button::Right:
      return TocInputAction::PageDown;
    case Button::Center:
      return TocInputAction::Select;
    case Button::Back:
      return TocInputAction::Exit;
    case Button::Power:
      if (e.type == EventType::ButtonPress && (config.powerShortPageTurn || config.powerShortBookmark)) {
        powerPressStartedMs = nowMs;
      }
      return TocInputAction::None;
    default:
      return TocInputAction::None;
  }
}

}  // namespace papyrix
