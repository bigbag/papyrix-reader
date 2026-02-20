#pragma once

#include <cstdint>

#include "../core/EventQueue.h"
#include "../core/Result.h"
#include "../core/Types.h"

class InputManager;
class MappedInputManager;

namespace papyrix {
namespace drivers {

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

  // Direct state queries (for hold detection)
  bool isPressed(Button btn) const;

  // Re-read button state after input mapping change to prevent ghost events
  void resyncState();

  // Access underlying input manager (for legacy code during migration)
  MappedInputManager& raw();

 private:
  EventQueue* queue_ = nullptr;
  uint32_t lastActivityMs_ = 0;
  bool initialized_ = false;

  // Track button states for press/release detection
  uint8_t prevButtonState_ = 0;
  uint8_t currButtonState_ = 0;

  // Track press start time for long press
  uint32_t pressStartMs_[7] = {};  // One per Button enum value

  // Track repeat timing and long press state
  uint32_t lastRepeatMs_[7] = {};
  bool longPressFired_[7] = {};

  void checkButton(Button btn, uint8_t mask);
};

}  // namespace drivers
}  // namespace papyrix
