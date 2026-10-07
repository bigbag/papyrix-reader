#pragma once

#include <TouchBackend.h>
#include <TouchTransform.h>

#include <cstddef>
#include <cstdint>

#include "../core/EventQueue.h"
#include "../core/Result.h"
#include "../core/Types.h"

class InputManager;

namespace papyrix::hal {

class Input {
 public:
  // Threshold for long press detection (ms)
  static constexpr uint32_t LONG_PRESS_MS = 700;

  // Button repeat timing (ms)
  static constexpr uint32_t REPEAT_START_MS = 700;
  static constexpr uint32_t REPEAT_INTERVAL_MS = 350;

  // Only directional buttons repeat (Up=0x01, Down=0x02, Left=0x04, Right=0x08)
  static constexpr uint8_t REPEAT_BUTTON_MASK = 0x0F;

  Result<void> init(EventQueue& eventQueue);
  void shutdown();

  // Call each frame to check buttons and push events
  void poll();

  // Time since last input activity (ms)
  uint32_t idleTimeMs() const;

  // Reset idle timer (e.g., when WiFi activity should prevent auto-sleep)
  void resetIdleTimer();

  // Consume power-button presses until a release is observed: a press still
  // held from setup must not emit a long press (Home routes it to Sleep).
  void suppressPowerUntilRelease() { powerSuppressed_ = true; }
  bool powerSuppressed() const { return powerSuppressed_; }

  // Direct state queries (for hold detection)
  bool isPressed(Button btn) const;

  // Re-read button state after input mapping change to prevent ghost events
  void resyncState();

 private:
  EventQueue* queue_ = nullptr;
  uint32_t lastActivityMs_ = 0;
  bool initialized_ = false;
  board::TouchBackend touchBackend_;
  board::TapClassifier tapClassifier_;
  board::DisplayOrientation orientation_ = board::DisplayOrientation::Portrait;
  uint8_t orientationRevision_ = 0;
  uint32_t touchSuppressionRevision_ = 0;
  bool homeDown_ = false;
  bool powerSuppressed_ = false;
  bool homeReady_ = false;
  static constexpr uint32_t TOUCH_POLL_INTERVAL_MS = 10;
  uint32_t nextTouchPollMs_ = 0;

  // Track button states for press/release detection
  uint8_t prevButtonState_ = 0;
  uint8_t currButtonState_ = 0;

  static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::Count);
  static_assert(BUTTON_COUNT <= sizeof(uint8_t) * 8, "Button state no longer fits in uint8_t");

  // Track press start time for long press
  uint32_t pressStartMs_[BUTTON_COUNT] = {};

  // Track repeat timing and long press state
  uint32_t lastRepeatMs_[BUTTON_COUNT] = {};
  bool longPressFired_[BUTTON_COUNT] = {};

  void checkButton(Button btn, uint8_t mask);
  void pollTouch();
};

}  // namespace papyrix::hal
