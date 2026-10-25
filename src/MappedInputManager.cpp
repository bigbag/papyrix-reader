#include "MappedInputManager.h"

#include "core/PapyrixSettings.h"

decltype(InputManager::BTN_BACK) MappedInputManager::mapButton(const Button button) const {
  const auto frontLayout = settings_ ? static_cast<papyrix::Settings::FrontButtonLayout>(settings_->frontButtonLayout)
                                     : papyrix::Settings::FrontBCLR;
  const auto sideLayout = settings_ ? static_cast<papyrix::Settings::SideButtonLayout>(settings_->sideButtonLayout)
                                    : papyrix::Settings::PrevNext;

  switch (button) {
    case Button::Back:
      switch (frontLayout) {
        case papyrix::Settings::FrontLRBC:
          return InputManager::BTN_LEFT;
        case papyrix::Settings::FrontBCLR:
        default:
          return InputManager::BTN_BACK;
      }
    case Button::Confirm:
      switch (frontLayout) {
        case papyrix::Settings::FrontLRBC:
          return InputManager::BTN_RIGHT;
        case papyrix::Settings::FrontBCLR:
        default:
          return InputManager::BTN_CONFIRM;
      }
    case Button::Left:
      switch (frontLayout) {
        case papyrix::Settings::FrontLRBC:
          return InputManager::BTN_BACK;
        case papyrix::Settings::FrontBCLR:
        default:
          return InputManager::BTN_LEFT;
      }
    case Button::Right:
      switch (frontLayout) {
        case papyrix::Settings::FrontLRBC:
          return InputManager::BTN_CONFIRM;
        case papyrix::Settings::FrontBCLR:
        default:
          return InputManager::BTN_RIGHT;
      }
    case Button::Up:
      switch (sideLayout) {
        case papyrix::Settings::NextPrev:
          return InputManager::BTN_DOWN;
        case papyrix::Settings::PrevNext:
        default:
          return InputManager::BTN_UP;
      }
    case Button::Down:
      switch (sideLayout) {
        case papyrix::Settings::NextPrev:
          return InputManager::BTN_UP;
        case papyrix::Settings::PrevNext:
        default:
          return InputManager::BTN_DOWN;
      }
    case Button::Power:
      return InputManager::BTN_POWER;
    case Button::PageBack:
      switch (sideLayout) {
        case papyrix::Settings::NextPrev:
          return InputManager::BTN_DOWN;
        case papyrix::Settings::PrevNext:
        default:
          return InputManager::BTN_UP;
      }
    case Button::PageForward:
      switch (sideLayout) {
        case papyrix::Settings::NextPrev:
          return InputManager::BTN_UP;
        case papyrix::Settings::PrevNext:
        default:
          return InputManager::BTN_DOWN;
      }
  }

  return InputManager::BTN_BACK;
}

void MappedInputManager::setPageButtonsBlocked(const bool blocked) {
  uint8_t mask = 0;
  if (blocked) {
    for (const Button button : {Button::Left, Button::Right, Button::Up, Button::Down}) {
      mask |= uint8_t{1} << mapButton(button);
    }
  }
  inputManager.setSuppressedButtons(mask);
}

bool MappedInputManager::wasSuppressed(const Button button) const {
  return inputManager.wasSuppressed(mapButton(button));
}

bool MappedInputManager::wasPressed(const Button button) const { return inputManager.wasPressed(mapButton(button)); }

bool MappedInputManager::wasReleased(const Button button) const { return inputManager.wasReleased(mapButton(button)); }

bool MappedInputManager::isPressed(const Button button) const { return inputManager.isPressed(mapButton(button)); }

bool MappedInputManager::wasAnyPressed() const { return inputManager.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return inputManager.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return inputManager.getHeldTime(); }
