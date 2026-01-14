#include "ClearCacheConfirmActivity.h"

#include <GfxRenderer.h>

#include "CacheManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"

void ClearCacheConfirmActivity::onEnter() {
  Activity::onEnter();
  render();
}

void ClearCacheConfirmActivity::loop() {
  // Handle selection change with Left/Right or Up/Down
  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selection > 0) {
      selection--;
      render();
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
             mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selection < 1) {
      selection++;
      render();
    }
  }

  // Handle confirm
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selection == 0) {
      performClear();
    } else {
      onComplete(true);  // Cancelled, but not an error
    }
    return;
  }

  // Handle back/cancel
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onComplete(true);  // Cancelled, but not an error
    return;
  }
}

void ClearCacheConfirmActivity::performClear() {
  // Show clearing message
  renderer.clearScreen(THEME.backgroundColor);
  renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Clearing cache...",
                            THEME.primaryTextBlack);
  renderer.displayBuffer();

  // Perform the clear
  const int result = CacheManager::clearAllBookCaches();

  // Show result briefly
  renderer.clearScreen(THEME.backgroundColor);
  if (result >= 0) {
    char msg[64];
    if (result == 0) {
      snprintf(msg, sizeof(msg), "No caches to clear");
    } else if (result == 1) {
      snprintf(msg, sizeof(msg), "Cleared 1 book cache");
    } else {
      snprintf(msg, sizeof(msg), "Cleared %d book caches", result);
    }
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, msg, THEME.primaryTextBlack);
  } else {
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Failed to clear cache",
                              THEME.primaryTextBlack);
  }
  renderer.displayBuffer();

  // Brief delay to show result
  vTaskDelay(1500 / portTICK_PERIOD_MS);

  onComplete(result >= 0);
}

void ClearCacheConfirmActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto lineHeight = renderer.getLineHeight(THEME.uiFontId);
  const auto top = (pageHeight - lineHeight * 3) / 2;

  renderer.clearScreen(THEME.backgroundColor);

  // Title
  renderer.drawCenteredText(THEME.readerFontId, top - 40, "Clear Cache?", THEME.primaryTextBlack, BOLD);

  // Description
  renderer.drawCenteredText(THEME.uiFontId, top, "This will delete all book caches", THEME.primaryTextBlack);
  renderer.drawCenteredText(THEME.uiFontId, top + lineHeight, "and reading progress.", THEME.primaryTextBlack);

  // Yes/No buttons
  const int buttonY = top + lineHeight * 3;
  constexpr int buttonWidth = 80;
  constexpr int buttonHeight = 36;
  constexpr int buttonSpacing = 20;
  constexpr int totalWidth = buttonWidth * 2 + buttonSpacing;
  const int startX = (pageWidth - totalWidth) / 2;

  const char* buttonLabels[] = {"Yes", "No"};
  const int buttonPositions[] = {startX, startX + buttonWidth + buttonSpacing};

  for (int i = 0; i < 2; i++) {
    const bool isSelected = (selection == i);
    const int btnX = buttonPositions[i];

    if (isSelected) {
      renderer.fillRect(btnX, buttonY, buttonWidth, buttonHeight, THEME.selectionFillBlack);
    } else {
      renderer.drawRect(btnX, buttonY, buttonWidth, buttonHeight, THEME.primaryTextBlack);
    }

    const bool textColor = isSelected ? THEME.selectionTextBlack : THEME.primaryTextBlack;
    const int textWidth = renderer.getTextWidth(THEME.uiFontId, buttonLabels[i]);
    const int textX = btnX + (buttonWidth - textWidth) / 2;
    const int textY = buttonY + (buttonHeight - renderer.getFontAscenderSize(THEME.uiFontId)) / 2;
    renderer.drawText(THEME.uiFontId, textX, textY, buttonLabels[i], textColor);
  }

  // Button hints at bottom
  const auto btnLabels = mappedInput.mapLabels("Back", "Confirm", "Left", "Right");
  renderer.drawButtonHints(THEME.uiFontId, btnLabels.btn1, btnLabels.btn2, btnLabels.btn3, btnLabels.btn4,
                           THEME.primaryTextBlack);

  renderer.displayBuffer();
}
