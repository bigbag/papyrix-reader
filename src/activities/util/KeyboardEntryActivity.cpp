#include "KeyboardEntryActivity.h"

#include "../../config.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

// Definition of static constexpr member (required for ODR-use)
constexpr char KeyboardEntryActivity::keyboard[NUM_ROWS][KEYS_PER_ROW];

void KeyboardEntryActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KeyboardEntryActivity*>(param);
  self->displayTaskLoop();
}

void KeyboardEntryActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&KeyboardEntryActivity::taskTrampoline, "KeyboardEntryActivity",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void KeyboardEntryActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

char KeyboardEntryActivity::getSelectedChar() const {
  if (selectedRow < 0 || selectedRow >= NUM_ROWS) return '\0';
  if (selectedCol < 0 || selectedCol >= KEYS_PER_ROW) return '\0';
  return keyboard[selectedRow][selectedCol];
}

void KeyboardEntryActivity::handleKeyPress() {
  // Handle control row (row 8: SPACE, BACKSPACE)
  if (selectedRow == CONTROL_ROW) {
    if (selectedCol >= SPACE_START && selectedCol <= SPACE_END) {
      // Space bar
      if (maxLength == 0 || text.length() < maxLength) {
        text += ' ';
      }
      return;
    }

    if (selectedCol >= BACKSPACE_START && selectedCol <= BACKSPACE_END) {
      // Backspace
      if (!text.empty()) {
        text.pop_back();
      }
      return;
    }
    return;
  }

  // Regular character
  const char c = getSelectedChar();
  if (c == '\0') {
    return;
  }

  if (maxLength == 0 || text.length() < maxLength) {
    text += c;
  }
}

void KeyboardEntryActivity::loop() {
  // Navigation - Up
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedRow > 0) {
      selectedRow--;
    }
    updateRequired = true;
  }

  // Navigation - Down
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedRow < NUM_ROWS - 1) {
      selectedRow++;
      // When entering control row, snap to nearest control key
      if (selectedRow == CONTROL_ROW) {
        if (selectedCol <= SPACE_END) {
          selectedCol = (SPACE_START + SPACE_END) / 2;  // Center of SPACE
        } else {
          selectedCol = (BACKSPACE_START + BACKSPACE_END) / 2;  // Center of BACKSPACE
        }
      }
    }
    updateRequired = true;
  }

  // Navigation - Left
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    // Control row: snap between SPACE and BACKSPACE
    if (selectedRow == CONTROL_ROW) {
      if (selectedCol >= BACKSPACE_START) {
        selectedCol = (SPACE_START + SPACE_END) / 2;
      }
      // If already on SPACE, stay there
    } else {
      // Regular rows: standard grid navigation with wrap
      if (selectedCol > 0) {
        selectedCol--;
      } else if (selectedRow > 0) {
        selectedRow--;
        selectedCol = KEYS_PER_ROW - 1;
      }
    }
    updateRequired = true;
  }

  // Navigation - Right
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    // Control row: snap between SPACE and BACKSPACE
    if (selectedRow == CONTROL_ROW) {
      if (selectedCol <= SPACE_END) {
        selectedCol = (BACKSPACE_START + BACKSPACE_END) / 2;
      }
      // If already on BACKSPACE, stay there
    } else {
      // Regular rows: standard grid navigation with wrap
      if (selectedCol < KEYS_PER_ROW - 1) {
        selectedCol++;
      } else if (selectedRow < NUM_ROWS - 1) {
        selectedRow++;
        selectedCol = 0;
        // When entering control row via wrap, start at SPACE
        if (selectedRow == CONTROL_ROW) {
          selectedCol = (SPACE_START + SPACE_END) / 2;
        }
      }
    }
    updateRequired = true;
  }

  // Selection
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleKeyPress();
    updateRequired = true;
  }

  // Cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onCancel) {
      onCancel();
    }
    updateRequired = true;
  }
}

void KeyboardEntryActivity::render() const {
  renderer.clearScreen(THEME.backgroundColor);

  // Draw title (bold, same style as WiFi Networks screen)
  renderer.drawCenteredText(THEME.readerFontId, startY, title.c_str(), THEME.primaryTextBlack, BOLD);

  // Margins
  constexpr int marginAfterTitle = 15;
  constexpr int marginAfterInput = 35;

  // Draw input field
  const int inputY = startY + marginAfterTitle + 12;

  // Match button hints width (buttons span from x=25 to x=456, width=431)
  constexpr int buttonAreaLeft = 25;
  constexpr int buttonAreaRight = 456;
  constexpr int buttonAreaWidth = buttonAreaRight - buttonAreaLeft;

  renderer.drawText(THEME.uiFontId, buttonAreaLeft, inputY, "[", THEME.primaryTextBlack);

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  // Show cursor at end
  displayText += "_";

  // Truncate if too long for display
  int approxCharWidth = renderer.getSpaceWidth(THEME.uiFontId);
  if (approxCharWidth < 1) approxCharWidth = 8;
  const int maxDisplayLen = (buttonAreaWidth - 30) / approxCharWidth;
  if (displayText.length() > static_cast<size_t>(maxDisplayLen)) {
    displayText = "..." + displayText.substr(displayText.length() - maxDisplayLen + 3);
  }

  renderer.drawText(THEME.uiFontId, buttonAreaLeft + 10, inputY, displayText.c_str(), THEME.primaryTextBlack);
  renderer.drawText(THEME.uiFontId, buttonAreaRight - 10, inputY, "]", THEME.primaryTextBlack);

  // Keyboard layout constants - match button area width
  constexpr int borderPadding = 10;
  constexpr int separatorHeight = 18;  // Space between groups (lowercase, uppercase, numbers, controls)
  constexpr int borderWidth = buttonAreaWidth;
  constexpr int gridWidth = borderWidth - 2 * borderPadding;
  constexpr int keySpacingH = 2;  // Horizontal spacing between keys
  constexpr int keySpacingV = 6;  // Vertical spacing between rows
  constexpr int keyWidth = (gridWidth - (KEYS_PER_ROW - 1) * keySpacingH) / KEYS_PER_ROW;
  constexpr int keyHeight = 20;
  const int leftMargin = buttonAreaLeft;

  // Calculate total keyboard height
  // 8 regular rows + 1 control row + 3 separators (no zone labels)
  constexpr int regularRowsHeight = 8 * (keyHeight + keySpacingV);
  constexpr int controlRowHeight = keyHeight + keySpacingV;
  constexpr int separatorsHeight = 3 * separatorHeight;
  constexpr int totalKeyboardHeight = regularRowsHeight + controlRowHeight + separatorsHeight + 2 * borderPadding;

  const int keyboardStartY = inputY + marginAfterInput;

  // Draw keyboard border
  renderer.drawRect(leftMargin, keyboardStartY, borderWidth, totalKeyboardHeight, THEME.primaryTextBlack);

  // Current Y position for rendering
  int currentY = keyboardStartY + borderPadding;
  const int contentStartX = leftMargin + borderPadding;

  // Zone separator positions (draw after these rows)
  const int zoneSeparatorAfterRows[] = {2, 5, 7};
  int zoneIndex = 0;

  for (int row = 0; row < NUM_ROWS; row++) {
    const int rowY = currentY;

    // Handle control row (row 8) specially
    if (row == CONTROL_ROW) {
      // Draw 2 control buttons: SPACE, BACKSPACE (<-)
      int currentX = contentStartX;

      // SPACE button (cols 0-5, 6 keys wide)
      const int spaceWidth = 6 * keyWidth + 5 * keySpacingH;
      const bool spaceSelected = (selectedRow == CONTROL_ROW && selectedCol >= SPACE_START && selectedCol <= SPACE_END);
      const char* spaceLabel = "SPACE";
      const int spaceTextWidth = renderer.getTextWidth(THEME.uiFontId, spaceLabel);
      const int spaceTextX = currentX + (spaceWidth - spaceTextWidth) / 2;
      renderItemWithSelector(spaceTextX, rowY, spaceLabel, spaceSelected);
      currentX += spaceWidth + keySpacingH;

      // BACKSPACE button (cols 6-9, 4 keys wide)
      const int bsWidth = 4 * keyWidth + 3 * keySpacingH;
      const bool bsSelected = (selectedRow == CONTROL_ROW && selectedCol >= BACKSPACE_START && selectedCol <= BACKSPACE_END);
      const char* bsLabel = "<-";
      const int bsTextWidth = renderer.getTextWidth(THEME.uiFontId, bsLabel);
      const int bsTextX = currentX + (bsWidth - bsTextWidth) / 2;
      renderItemWithSelector(bsTextX, rowY, bsLabel, bsSelected);
    } else {
      // Regular rows: render each key
      for (int col = 0; col < KEYS_PER_ROW; col++) {
        const char c = keyboard[row][col];
        std::string keyLabel(1, c);
        const int charWidth = renderer.getTextWidth(THEME.uiFontId, keyLabel.c_str());

        const int keyX = contentStartX + col * (keyWidth + keySpacingH) + (keyWidth - charWidth) / 2;
        const bool isSelected = row == selectedRow && col == selectedCol;
        renderItemWithSelector(keyX, rowY, keyLabel.c_str(), isSelected);
      }
    }

    currentY += keyHeight + keySpacingV;

    // Draw zone separator after specific rows
    if (zoneIndex < 3 && row == zoneSeparatorAfterRows[zoneIndex]) {
      const int separatorY = currentY + separatorHeight / 2 - 1;
      renderer.drawLine(contentStartX, separatorY, contentStartX + gridWidth, separatorY, THEME.primaryTextBlack);
      currentY += separatorHeight;
      zoneIndex++;
    }
  }

  // Draw button hints at bottom of screen
  const auto labels = mappedInput.mapLabels("Back", "Confirm", "Left", "Right");
  renderer.drawButtonHints(THEME.uiFontId, labels.btn1, labels.btn2, labels.btn3, labels.btn4, THEME.primaryTextBlack);
  renderer.displayBuffer();
}

void KeyboardEntryActivity::renderItemWithSelector(const int x, const int y, const char* item,
                                                   const bool isSelected) const {
  if (isSelected) {
    const int itemWidth = renderer.getTextWidth(THEME.uiFontId, item);
    renderer.drawText(THEME.uiFontId, x - 6, y, "[", THEME.primaryTextBlack);
    renderer.drawText(THEME.uiFontId, x + itemWidth, y, "]", THEME.primaryTextBlack);
  }
  renderer.drawText(THEME.uiFontId, x, y, item, THEME.primaryTextBlack);
}
