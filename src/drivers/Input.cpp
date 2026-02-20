#include "Input.h"

#include <Arduino.h>
#include <InputManager.h>
#include <MappedInputManager.h>

// Global input managers (defined in main.cpp)
extern InputManager inputManager;
extern MappedInputManager& mappedInput;

namespace papyrix {
namespace drivers {

Result<void> Input::init(EventQueue& eventQueue) {
  if (initialized_) {
    return Ok();
  }

  queue_ = &eventQueue;
  lastActivityMs_ = millis();
  prevButtonState_ = 0;
  currButtonState_ = 0;
  initialized_ = true;

  return Ok();
}

void Input::shutdown() {
  queue_ = nullptr;
  initialized_ = false;
}

void Input::poll() {
  if (!initialized_ || !queue_) {
    return;
  }

  // Save previous state
  prevButtonState_ = currButtonState_;
  currButtonState_ = 0;

  // Check each button
  checkButton(Button::Up, 1 << 0);
  checkButton(Button::Down, 1 << 1);
  checkButton(Button::Left, 1 << 2);
  checkButton(Button::Right, 1 << 3);
  checkButton(Button::Center, 1 << 4);
  checkButton(Button::Back, 1 << 5);
  checkButton(Button::Power, 1 << 6);
}

void Input::checkButton(Button btn, uint8_t mask) {
  bool wasDown = (prevButtonState_ & mask) != 0;
  bool isDown = false;

  // Map our Button to MappedInputManager::Button
  MappedInputManager::Button mappedBtn;
  switch (btn) {
    case Button::Up:
      mappedBtn = MappedInputManager::Button::Up;
      break;
    case Button::Down:
      mappedBtn = MappedInputManager::Button::Down;
      break;
    case Button::Left:
      mappedBtn = MappedInputManager::Button::Left;
      break;
    case Button::Right:
      mappedBtn = MappedInputManager::Button::Right;
      break;
    case Button::Center:
      mappedBtn = MappedInputManager::Button::Confirm;
      break;
    case Button::Back:
      mappedBtn = MappedInputManager::Button::Back;
      break;
    case Button::Power:
      mappedBtn = MappedInputManager::Button::Power;
      break;
  }

  isDown = mappedInput.isPressed(mappedBtn);

  if (isDown) {
    currButtonState_ |= mask;
  }

  int idx = static_cast<int>(btn);

  // Button just pressed
  if (isDown && !wasDown) {
    uint32_t now = millis();
    pressStartMs_[idx] = now;
    lastRepeatMs_[idx] = now;
    longPressFired_[idx] = false;
    queue_->push(Event::buttonPress(btn));
    lastActivityMs_ = now;
  }

  // Button held - check for long press and repeat
  if (isDown && wasDown) {
    uint32_t now = millis();
    uint32_t heldMs = now - pressStartMs_[idx];

    // Directional buttons use repeat instead of long press
    if (mask & REPEAT_BUTTON_MASK) {
      uint32_t sinceLastRepeat = now - lastRepeatMs_[idx];
      uint32_t threshold = (lastRepeatMs_[idx] == pressStartMs_[idx]) ? REPEAT_START_MS : REPEAT_INTERVAL_MS;
      if (sinceLastRepeat >= threshold) {
        queue_->push(Event::buttonRepeat(btn));
        lastRepeatMs_[idx] = now;
        lastActivityMs_ = now;
      }
    } else if (!longPressFired_[idx] && heldMs >= LONG_PRESS_MS) {
      queue_->push(Event::buttonLongPress(btn));
      longPressFired_[idx] = true;
    }
  }

  // Button released
  if (!isDown && wasDown) {
    queue_->push(Event::buttonRelease(btn));
    lastActivityMs_ = millis();
  }
}

uint32_t Input::idleTimeMs() const { return millis() - lastActivityMs_; }

bool Input::isPressed(Button btn) const {
  MappedInputManager::Button mappedBtn;
  switch (btn) {
    case Button::Up:
      mappedBtn = MappedInputManager::Button::Up;
      break;
    case Button::Down:
      mappedBtn = MappedInputManager::Button::Down;
      break;
    case Button::Left:
      mappedBtn = MappedInputManager::Button::Left;
      break;
    case Button::Right:
      mappedBtn = MappedInputManager::Button::Right;
      break;
    case Button::Center:
      mappedBtn = MappedInputManager::Button::Confirm;
      break;
    case Button::Back:
      mappedBtn = MappedInputManager::Button::Back;
      break;
    case Button::Power:
      mappedBtn = MappedInputManager::Button::Power;
      break;
  }
  return mappedInput.isPressed(mappedBtn);
}

void Input::resyncState() {
  currButtonState_ = 0;
  if (isPressed(Button::Up)) currButtonState_ |= (1 << 0);
  if (isPressed(Button::Down)) currButtonState_ |= (1 << 1);
  if (isPressed(Button::Left)) currButtonState_ |= (1 << 2);
  if (isPressed(Button::Right)) currButtonState_ |= (1 << 3);
  if (isPressed(Button::Center)) currButtonState_ |= (1 << 4);
  if (isPressed(Button::Back)) currButtonState_ |= (1 << 5);
  if (isPressed(Button::Power)) currButtonState_ |= (1 << 6);
  prevButtonState_ = currButtonState_;
}

MappedInputManager& Input::raw() { return mappedInput; }

}  // namespace drivers
}  // namespace papyrix
