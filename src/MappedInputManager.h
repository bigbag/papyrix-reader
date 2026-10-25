#pragma once

#include <InputManager.h>

namespace papyrix {
struct Settings;
}

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  explicit MappedInputManager(InputManager& inputManager) : inputManager(inputManager), settings_(nullptr) {}

  void setSettings(papyrix::Settings* settings) { settings_ = settings; }

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool wasSuppressed(Button button) const;
  void setPageButtonsBlocked(bool blocked);
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

 private:
  InputManager& inputManager;
  papyrix::Settings* settings_;
  decltype(InputManager::BTN_BACK) mapButton(Button button) const;
};
