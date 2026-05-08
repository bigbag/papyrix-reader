#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// MessageView - Full screen message display
// ============================================================================

struct MessageView {
  static constexpr int MAX_MSG_LEN = 128;

  ButtonBar buttons{"", "", "", ""};
  char message[MAX_MSG_LEN] = {0};
  bool needsRender = true;

  void setMessage(const char* msg) {
    strncpy(message, msg, MAX_MSG_LEN - 1);
    message[MAX_MSG_LEN - 1] = '\0';
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const MessageView& v);

// ============================================================================
// ConfirmView - Yes/No confirmation dialog
// ============================================================================

struct ConfirmView {
  static constexpr int MAX_TITLE_LEN = 48;
  static constexpr int MAX_MSG_LEN = 128;

  ButtonBar buttons;
  char title[MAX_TITLE_LEN] = "";
  char message[MAX_MSG_LEN] = {0};
  int8_t selected = 0;  // 0 = Yes, 1 = No
  bool needsRender = true;

  void setTitle(const char* t) {
    strncpy(title, t, MAX_TITLE_LEN - 1);
    title[MAX_TITLE_LEN - 1] = '\0';
  }

  void setMessage(const char* msg) {
    strncpy(message, msg, MAX_MSG_LEN - 1);
    message[MAX_MSG_LEN - 1] = '\0';
    needsRender = true;
  }

  void selectYes() {
    if (selected != 0) {
      selected = 0;
      needsRender = true;
    }
  }

  void selectNo() {
    if (selected != 1) {
      selected = 1;
      needsRender = true;
    }
  }

  bool isYesSelected() const { return selected == 0; }
};

void render(const GfxRenderer& r, const Theme& t, const ConfirmView& v);

// ============================================================================
// KeyboardView - Text input with on-screen keyboard
// ============================================================================

struct KeyboardView {
  static constexpr int MAX_INPUT_LEN = 64;
  static constexpr int MAX_TITLE_LEN = 48;

  // Special control characters from keyboard
  static constexpr char CTRL_BACKSPACE = '\x02';
  static constexpr char CTRL_CONFIRM = '\x03';

  ButtonBar buttons;
  char title[MAX_TITLE_LEN] = "";
  char input[MAX_INPUT_LEN] = {0};
  uint8_t inputLen = 0;
  KeyboardState keyboard;
  bool isPassword = false;
  bool needsRender = true;

  void setTitle(const char* t) {
    strncpy(title, t, MAX_TITLE_LEN - 1);
    title[MAX_TITLE_LEN - 1] = '\0';
  }

  void setPassword(bool pw) { isPassword = pw; }

  void appendChar(char c) {
    if (inputLen < MAX_INPUT_LEN - 1) {
      input[inputLen++] = c;
      input[inputLen] = '\0';
      needsRender = true;
    }
  }

  void backspace() {
    if (inputLen > 0) {
      input[--inputLen] = '\0';
      needsRender = true;
    }
  }

  void clear() {
    input[0] = '\0';
    inputLen = 0;
    needsRender = true;
  }

  void moveUp() {
    keyboard.moveUp();
    needsRender = true;
  }

  void moveDown() {
    keyboard.moveDown();
    needsRender = true;
  }

  void moveLeft() {
    keyboard.moveLeft();
    needsRender = true;
  }

  void moveRight() {
    keyboard.moveRight();
    needsRender = true;
  }

  // Returns true if confirm was pressed
  bool confirmKey() {
    char c = getKeyboardChar(keyboard);
    if (c == CTRL_BACKSPACE) {
      backspace();
      return false;
    }
    if (c == CTRL_CONFIRM) {
      return true;  // Signal that input is complete
    }
    if (c != '\0') {
      appendChar(c);
    }
    return false;
  }
};

void render(const GfxRenderer& r, const Theme& t, const KeyboardView& v);

}  // namespace ui
