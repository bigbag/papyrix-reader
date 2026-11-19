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
  enum class Hit : uint8_t { None, Yes, No, Back, Select };
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

  Hit hitTest(touch::Point point, const touch::DialogLayout& layout, int16_t screenWidth, int16_t screenHeight,
              bool frontLrbc = false) const {
    const int choice = touch::dialogChoiceAt(point, layout);
    if (choice == 0) return Hit::Yes;
    if (choice == 1) return Hit::No;
    const int action = touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc);
    if (action == 0) return Hit::Back;
    if (action == 1) return Hit::Select;
    return Hit::None;
  }
};

void render(const GfxRenderer& r, const Theme& t, const ConfirmView& v);

// ============================================================================
// KeyboardView - Text input with on-screen keyboard
// ============================================================================

struct KeyboardView {
  struct Hit {
    enum class Type : uint8_t { None, Key, Back, Select, MoveLeft, MoveRight };
    Type type = Type::None;
    int row = -1;
    int column = -1;
  };
  static constexpr int KEYBOARD_Y = 110;
  static constexpr int MAX_INPUT_LEN = 65;
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

  touch::Rect keyBounds(int row, int column, int16_t screenWidth, int16_t screenMarginSide) const {
    if (row < 0 || row >= KeyboardState::NUM_ROWS || column < 0 || column >= KeyboardState::KEYS_PER_ROW) return {};
    constexpr int borderPadding = 10;
    constexpr int keySpacing = 2;
    constexpr int keyHeight = 20;
    constexpr int rowPitch = 26;
    const int gridWidth = screenWidth - 2 * screenMarginSide - 2 * borderPadding;
    const int keyWidth = (gridWidth - (KeyboardState::KEYS_PER_ROW - 1) * keySpacing) / KeyboardState::KEYS_PER_ROW;
    const int separators = (row > 0 ? 1 : 0) + (row > 3 ? 1 : 0) + (row > 6 ? 1 : 0);
    const int x = screenMarginSide + borderPadding + column * (keyWidth + keySpacing);
    const int y = KEYBOARD_Y + 10 + row * rowPitch + separators * 18;
    return {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(keyWidth), keyHeight};
  }

  Hit hitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight, int16_t screenMarginSide,
              bool frontLrbc = false) const {
    switch (touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc)) {
      case 0:
        return {Hit::Type::Back};
      case 1:
        return {Hit::Type::Select};
      case 2:
        return {Hit::Type::MoveLeft};
      case 3:
        return {Hit::Type::MoveRight};
    }
    for (int row = 0; row < KeyboardState::NUM_ROWS; ++row) {
      for (int column = 0; column < KeyboardState::KEYS_PER_ROW; ++column) {
        if (keyBounds(row, column, screenWidth, screenMarginSide).contains(point)) {
          return {Hit::Type::Key, row, column};
        }
      }
    }
    return {};
  }
};

void render(const GfxRenderer& r, const Theme& t, const KeyboardView& v);

}  // namespace ui
