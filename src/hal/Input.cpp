#include "Input.h"

#include <Arduino.h>
#include <HardwareIdentity.h>
#include <InputManager.h>
#include <MappedInputManager.h>
#include <TargetConfig.h>

// Global input managers (defined in main.cpp)
extern InputManager inputManager;
extern MappedInputManager mappedInputManager;

namespace papyrix::hal {

Result<void> Input::init(EventQueue& eventQueue) {
  if (initialized_) {
    return Ok();
  }
  (void)inputManager.startSampling();

  queue_ = &eventQueue;
  lastActivityMs_ = millis();
  prevButtonState_ = 0;
  currButtonState_ = 0;
  const auto& profile = board::HardwareIdentity::instance().profile();
  touchBackend_.begin(profile);
  touchSuppressionRevision_ = board::touchSuppressionRevision();
  orientation_ = board::currentDisplayOrientation();
  orientationRevision_ = board::displayOrientationRevision();
  tapClassifier_.resetAfterWake();
  homeDown_ = false;
  homeReady_ = false;
  initialized_ = true;

  return Ok();
}

void Input::shutdown() {
  inputManager.stopSampling();
  touchBackend_.shutdown();
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

  checkButton(Button::Up, 1 << 0);
  checkButton(Button::Down, 1 << 1);
  checkButton(Button::Left, 1 << 2);
  checkButton(Button::Right, 1 << 3);
  checkButton(Button::Center, 1 << 4);
  checkButton(Button::Back, 1 << 5);
  checkButton(Button::Power, 1 << 6);
  pollTouch();
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
    case Button::Count:
      return;
  }

  isDown = mappedInputManager.isPressed(mappedBtn);
  // Clear on any observed up, not only on a release edge: a release between
  // polls never produces the edge, and the latch must not eat the next press.
  if (btn == Button::Power && !isDown) powerSuppressed_ = false;

  if (isDown) {
    currButtonState_ |= mask;
  }

  const size_t idx = static_cast<size_t>(btn);

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
  if (isDown && wasDown && !inputManager.isDebouncePending()) {
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
      // A press still held from setup must not emit a long press: the event
      // routes Home to Sleep. Consume it until release is observed.
      if (btn == Button::Power && powerSuppressed_) {
        longPressFired_[idx] = true;
      } else {
        queue_->push(Event::buttonLongPress(btn));
        longPressFired_[idx] = true;
      }
    }
  }

  // Button released
  if (!isDown && wasDown) {
    queue_->push(Event::buttonRelease(btn));
    lastActivityMs_ = millis();
  }
}

uint32_t Input::idleTimeMs() const { return millis() - lastActivityMs_; }

void Input::resetIdleTimer() { lastActivityMs_ = millis(); }

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
    case Button::Count:
      return false;
  }
  return mappedInputManager.isPressed(mappedBtn);
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
  tapClassifier_.resetAfterWake();
  homeDown_ = false;
  homeReady_ = false;
}

void Input::pollTouch() {
  if (!touchBackend_.available()) return;
  const auto& profile = board::HardwareIdentity::instance().profile();
  const uint32_t suppressionRevision = board::touchSuppressionRevision();
  if (suppressionRevision != touchSuppressionRevision_) {
    touchSuppressionRevision_ = suppressionRevision;
    tapClassifier_.resetAfterWake();
  }
  const uint8_t displayRevision = board::displayOrientationRevision();
  if (displayRevision != orientationRevision_) {
    orientation_ = board::currentDisplayOrientation();
    orientationRevision_ = displayRevision;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextTouchPollMs_) < 0) return;
  nextTouchPollMs_ = now + TOUCH_POLL_INTERVAL_MS;
  const auto raw = touchBackend_.poll();
  if (raw.controllerOk) {
    if (homeReady_ && homeDown_ && !raw.homeDown) {
      queue_->push(Event::buttonPress(Button::Back));
      lastActivityMs_ = now;
    }
    if (raw.homeDown || homeDown_) {
      tapClassifier_.resetAfterWake();
      lastActivityMs_ = now;
    }
    homeDown_ = raw.homeDown;
    if (!homeDown_) homeReady_ = true;
  }
  board::TouchSample sample;
  sample.fresh = raw.fresh;
  sample.controllerOk = raw.controllerOk;
  sample.contactCount = raw.contactCount;

  if (raw.fresh && raw.contactCount == 1) {
    board::PanelPoint logical{};
    if (board::rawToLogical(touchBackend_.config(), raw.rawX, raw.rawY, profile.display.width, profile.display.height,
                            orientation_, logical)) {
      sample.position = {logical.x, logical.y};
    } else {
      sample.contactCount = 2;
    }
  }

  const auto frame = tapClassifier_.update(sample, now, board::hasTouch(profile), false, orientationRevision_);
  if (frame.tap) {
    const TouchPoint point{frame.position.x, frame.position.y};
    queue_->push(Event::tap(point));
    lastActivityMs_ = now;
  }
}

}  // namespace papyrix::hal
