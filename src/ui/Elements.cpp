#include "Elements.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "core/PapyrixSettings.h"

namespace ui {

static uint8_t frontButtonLayout_ = 0;

void setFrontButtonLayout(uint8_t layout) { frontButtonLayout_ = layout; }

void title(const GfxRenderer& r, const Theme& t, int y, const char* text) {
  r.drawCenteredText(t.readerFontId, y, text, t.primaryTextBlack, EpdFontFamily::BOLD);
}

void brandTitle(const GfxRenderer& r, const Theme& t, int y, const char* text) {
  r.drawText(t.readerFontId, 10, y, text, t.primaryTextBlack, EpdFontFamily::BOLD);
}

void menuItem(const GfxRenderer& r, const Theme& t, int y, const char* text, bool selected) {
  const int x = t.screenMarginSide;
  const int w = r.getScreenWidth() - 2 * t.screenMarginSide;
  const int h = t.itemHeight;
  const int textY = y + (h - r.getLineHeight(t.uiFontId)) / 2;

  if (selected) {
    r.fillRect(x, y, w, h, t.selectionFillBlack);
    r.drawText(t.uiFontId, x + t.itemPaddingX, textY, text, t.selectionTextBlack);
  } else {
    r.drawText(t.uiFontId, x + t.itemPaddingX, textY, text, t.primaryTextBlack);
  }
}

void toggle(const GfxRenderer& r, const Theme& t, int y, const char* label, bool value, bool selected) {
  const int x = t.screenMarginSide;
  const int w = r.getScreenWidth() - 2 * t.screenMarginSide;
  const int h = t.itemHeight;
  const int valueX = r.getScreenWidth() - t.screenMarginSide - 50;
  const int textY = y + (h - r.getLineHeight(t.uiFontId)) / 2;

  if (selected) {
    r.fillRect(x, y, w, h, t.selectionFillBlack);
    r.drawText(t.uiFontId, x + t.itemPaddingX, textY, label, t.selectionTextBlack);
    r.drawText(t.uiFontId, valueX, textY, value ? "ON" : "OFF", t.selectionTextBlack);
  } else {
    r.drawText(t.uiFontId, x + t.itemPaddingX, textY, label, t.primaryTextBlack);
    r.drawText(t.uiFontId, valueX, textY, value ? "ON" : "OFF", t.secondaryTextBlack);
  }
}

void enumValue(const GfxRenderer& r, const Theme& t, int y, const char* label, const char* value, bool selected) {
  const int x = t.screenMarginSide;
  const int w = r.getScreenWidth() - 2 * t.screenMarginSide;
  const int h = t.itemHeight;
  const int textY = y + (h - r.getLineHeight(t.uiFontId)) / 2;

  const int valueWidth = r.getTextWidth(t.uiFontId, value);
  const int valueX = r.getScreenWidth() - t.screenMarginSide - valueWidth - t.itemValuePadding;

  if (selected) {
    r.fillRect(x, y, w, h, t.selectionFillBlack);
    r.drawText(t.uiFontId, x + t.itemPaddingX, textY, label, t.selectionTextBlack);
    r.drawText(t.uiFontId, valueX, textY, value, t.selectionTextBlack);
  } else {
    r.drawText(t.uiFontId, x + t.itemPaddingX, textY, label, t.primaryTextBlack);
    r.drawText(t.uiFontId, valueX, textY, value, t.secondaryTextBlack);
  }
}

void buttonBar(const GfxRenderer& r, const Theme& t, const char* b1, const char* b2, const char* b3, const char* b4) {
  if (frontButtonLayout_ == papyrix::Settings::FrontLRBC) {
    r.drawButtonHints(t.uiFontId, b3, b4, b1, b2, t.primaryTextBlack);
  } else {
    r.drawButtonHints(t.uiFontId, b1, b2, b3, b4, t.primaryTextBlack);
  }
}

void buttonBar(const GfxRenderer& r, const Theme& t, const ButtonBar& buttons) {
  if (frontButtonLayout_ == papyrix::Settings::FrontLRBC) {
    r.drawButtonHints(t.uiFontId, buttons.labels[2], buttons.labels[3], buttons.labels[0], buttons.labels[1],
                      t.primaryTextBlack);
  } else {
    r.drawButtonHints(t.uiFontId, buttons.labels[0], buttons.labels[1], buttons.labels[2], buttons.labels[3],
                      t.primaryTextBlack);
  }
}

void progress(const GfxRenderer& r, const Theme& t, int y, int current, int total) {
  const int x = t.screenMarginSide + 20;
  const int w = r.getScreenWidth() - 2 * (t.screenMarginSide + 20);
  const int h = 16;
  const int barY = y + 2;

  // Draw border
  r.drawRect(x, barY, w, h, t.primaryTextBlack);

  // Calculate fill width
  if (total > 0) {
    const int fillW = (w - 4) * current / total;
    if (fillW > 0) {
      r.fillRect(x + 2, barY + 2, fillW, h - 4, t.primaryTextBlack);
    }
  }

  // Draw percentage text centered below
  char buf[16];
  if (total > 0) {
    snprintf(buf, sizeof(buf), "%d%%", (current * 100) / total);
  } else {
    snprintf(buf, sizeof(buf), "0%%");
  }
  r.drawCenteredText(t.smallFontId, y + h + 5, buf, t.primaryTextBlack);
}

void text(const GfxRenderer& r, const Theme& t, int y, const char* str) {
  r.drawText(t.uiFontId, t.screenMarginSide + t.itemPaddingX, y, str, t.primaryTextBlack);
}

int textWrapped(const GfxRenderer& r, const Theme& t, int y, const char* str, int maxLines) {
  const int maxWidth = r.getScreenWidth() - 2 * (t.screenMarginSide + t.itemPaddingX);
  const auto lines = r.wrapTextWithHyphenation(t.uiFontId, str, maxWidth, maxLines);
  const int lineHeight = r.getLineHeight(t.uiFontId);

  int currentY = y;
  for (const auto& line : lines) {
    r.drawText(t.uiFontId, t.screenMarginSide + t.itemPaddingX, currentY, line.c_str(), t.primaryTextBlack);
    currentY += lineHeight;
  }
  return static_cast<int>(lines.size());
}

void image(const GfxRenderer& r, int x, int y, const uint8_t* data, int w, int h) {
  if (data != nullptr) {
    r.drawImage(data, x, y, w, h);
  }
}

void dialog(const GfxRenderer& r, const Theme& t, const char* titleText, const char* msg, int selected) {
  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();

  // Dialog box dimensions
  const int dialogW = screenW - 60;
  const int dialogH = 160;
  const int dialogX = 30;
  const int dialogY = (screenH - dialogH) / 2;

  // Draw dialog background (clear area)
  r.clearArea(dialogX, dialogY, dialogW, dialogH, t.backgroundColor);
  r.drawRect(dialogX, dialogY, dialogW, dialogH, t.primaryTextBlack);

  // Draw title
  r.drawCenteredText(t.readerFontId, dialogY + 20, titleText, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Draw message
  r.drawCenteredText(t.uiFontId, dialogY + 60, msg, t.primaryTextBlack);

  // Draw buttons (Yes/No)
  const int btnW = 80;
  const int btnH = 30;
  const int btnY = dialogY + dialogH - 50;
  const int btnTextY = btnY + (btnH - r.getLineHeight(t.uiFontId)) / 2;
  const int yesX = dialogX + (dialogW / 2) - btnW - 20;
  const int noX = dialogX + (dialogW / 2) + 20;

  // Yes button
  if (selected == 0) {
    r.fillRect(yesX, btnY, btnW, btnH, t.selectionFillBlack);
    r.drawCenteredText(t.uiFontId, btnTextY, "Yes", t.selectionTextBlack);
  } else {
    r.drawRect(yesX, btnY, btnW, btnH, t.primaryTextBlack);
  }
  r.drawText(t.uiFontId, yesX + (btnW - r.getTextWidth(t.uiFontId, "Yes")) / 2, btnTextY, "Yes",
             selected == 0 ? t.selectionTextBlack : t.primaryTextBlack);

  // No button
  if (selected == 1) {
    r.fillRect(noX, btnY, btnW, btnH, t.selectionFillBlack);
  } else {
    r.drawRect(noX, btnY, btnW, btnH, t.primaryTextBlack);
  }
  r.drawText(t.uiFontId, noX + (btnW - r.getTextWidth(t.uiFontId, "No")) / 2, btnTextY, "No",
             selected == 1 ? t.selectionTextBlack : t.primaryTextBlack);
}

// Keyboard layout - 10x10 grid
// Row 0: Control row (Backspace, Space, Confirm)
// Rows 1-3: lowercase letters + symbols
// Rows 4-6: uppercase letters + symbols
// Rows 7-9: numbers + symbols
// Control chars: \x01 = SPACE, \x02 = BACKSPACE, \x03 = CONFIRM
static constexpr char KEYBOARD_GRID[10][10] = {
    {'\x02', '\x02', '\x02', '\x01', '\x01', '\x01', '\x01', '\x03', '\x03', '\x03'},  // row 0: controls
    {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'},                                // row 1: lowercase
    {'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't'},                                // row 2: lowercase
    {'u', 'v', 'w', 'x', 'y', 'z', '.', '-', '_', '@'},                                // row 3: lowercase + symbols
    {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'},                                // row 4: uppercase
    {'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T'},                                // row 5: uppercase
    {'U', 'V', 'W', 'X', 'Y', 'Z', '!', '#', '$', '%'},                                // row 6: uppercase + symbols
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},                                // row 7: numbers
    {'^', '&', '*', '(', ')', '+', ' ', '[', ']', '\\'},                               // row 8: symbols
    {'/', ':', ';', '~', '?', '=', '\'', '"', ',', '<'}                                // row 9: URL/extra symbols
};

// Zone separator after these rows
static constexpr int ZONE_SEPARATORS[] = {0, 3, 6};
static constexpr int NUM_ZONE_SEPARATORS = 3;

void KeyboardState::moveUp() {
  if (cursorY > 0) {
    cursorY--;
    // When entering control row, snap to nearest control key
    if (cursorY == CONTROL_ROW) {
      if (cursorX <= BACKSPACE_END) {
        cursorX = (BACKSPACE_START + BACKSPACE_END) / 2;
      } else if (cursorX <= SPACE_END) {
        cursorX = (SPACE_START + SPACE_END) / 2;
      } else {
        cursorX = (CONFIRM_START + CONFIRM_END) / 2;
      }
    }
  }
}

void KeyboardState::moveDown() {
  if (cursorY < NUM_ROWS - 1) {
    cursorY++;
  }
}

void KeyboardState::moveLeft() {
  if (cursorY == CONTROL_ROW) {
    // Snap between control buttons
    if (cursorX >= CONFIRM_START) {
      cursorX = (SPACE_START + SPACE_END) / 2;
    } else if (cursorX >= SPACE_START) {
      cursorX = (BACKSPACE_START + BACKSPACE_END) / 2;
    }
  } else {
    if (cursorX > 0) {
      cursorX--;
    } else if (cursorY > 1) {
      cursorY--;
      cursorX = KEYS_PER_ROW - 1;
    }
  }
}

void KeyboardState::moveRight() {
  if (cursorY == CONTROL_ROW) {
    // Snap between control buttons
    if (cursorX <= BACKSPACE_END) {
      cursorX = (SPACE_START + SPACE_END) / 2;
    } else if (cursorX <= SPACE_END) {
      cursorX = (CONFIRM_START + CONFIRM_END) / 2;
    }
  } else {
    if (cursorX < KEYS_PER_ROW - 1) {
      cursorX++;
    } else if (cursorY < NUM_ROWS - 1) {
      cursorY++;
      cursorX = 0;
    }
  }
}

void keyboard(const GfxRenderer& r, const Theme& t, int y, const KeyboardState& state) {
  const int screenW = r.getScreenWidth();
  const int borderPadding = 10;
  const int gridWidth = screenW - 2 * t.screenMarginSide - 2 * borderPadding;
  const int keySpacingH = 2;
  const int keySpacingV = 6;
  const int keyW = (gridWidth - (KeyboardState::KEYS_PER_ROW - 1) * keySpacingH) / KeyboardState::KEYS_PER_ROW;
  const int keyH = 20;
  const int separatorHeight = 18;
  const int startX = t.screenMarginSide + borderPadding;

  int currentY = y + borderPadding;
  int zoneIdx = 0;

  for (int row = 0; row < KeyboardState::NUM_ROWS; row++) {
    if (row == KeyboardState::CONTROL_ROW) {
      // Control row: Backspace, Space, Confirm
      int currentX = startX;

      // Backspace (3 keys wide)
      const int bsWidth = 3 * keyW + 2 * keySpacingH;
      const bool bsSelected = state.isOnBackspace();
      if (bsSelected) {
        r.drawText(t.uiFontId, currentX, currentY, "[Backspace]", t.primaryTextBlack);
      } else {
        r.drawText(t.uiFontId, currentX + 5, currentY, "Backspace", t.primaryTextBlack);
      }
      currentX += bsWidth + keySpacingH;

      // Space (4 keys wide)
      const int spWidth = 4 * keyW + 3 * keySpacingH;
      const bool spSelected = state.isOnSpace();
      const int spTextX = currentX + (spWidth - r.getTextWidth(t.uiFontId, "Space")) / 2;
      if (spSelected) {
        r.drawText(t.uiFontId, spTextX - 6, currentY, "[Space]", t.primaryTextBlack);
      } else {
        r.drawText(t.uiFontId, spTextX, currentY, "Space", t.primaryTextBlack);
      }
      currentX += spWidth + keySpacingH;

      // Confirm (3 keys wide)
      const bool cfSelected = state.isOnConfirm();
      if (cfSelected) {
        r.drawText(t.uiFontId, currentX, currentY, "[Confirm]", t.primaryTextBlack);
      } else {
        r.drawText(t.uiFontId, currentX + 5, currentY, "Confirm", t.primaryTextBlack);
      }
    } else {
      // Regular character rows
      for (int col = 0; col < KeyboardState::KEYS_PER_ROW; col++) {
        const char c = KEYBOARD_GRID[row][col];
        const char keyStr[2] = {c, '\0'};
        const int keyX = startX + col * (keyW + keySpacingH);
        const bool isSelected = (state.cursorY == row && state.cursorX == col);

        // Center character in key
        const int charW = r.getTextWidth(t.uiFontId, keyStr);
        const int charX = keyX + (keyW - charW) / 2;

        if (isSelected) {
          r.drawText(t.uiFontId, charX - 6, currentY, "[", t.primaryTextBlack);
          r.drawText(t.uiFontId, charX, currentY, keyStr, t.primaryTextBlack);
          r.drawText(t.uiFontId, charX + charW, currentY, "]", t.primaryTextBlack);
        } else {
          r.drawText(t.uiFontId, charX, currentY, keyStr, t.primaryTextBlack);
        }
      }
    }

    currentY += keyH + keySpacingV;

    // Draw zone separator after specific rows
    if (zoneIdx < NUM_ZONE_SEPARATORS && row == ZONE_SEPARATORS[zoneIdx]) {
      const int sepY = currentY + separatorHeight / 2 - 1;
      r.drawLine(startX, sepY, startX + gridWidth, sepY, t.primaryTextBlack);
      currentY += separatorHeight;
      zoneIdx++;
    }
  }
}

char getKeyboardChar(const KeyboardState& state) {
  if (state.cursorY == KeyboardState::CONTROL_ROW) {
    // Return special chars for control buttons
    if (state.isOnBackspace()) return '\x02';
    if (state.isOnSpace()) return ' ';
    if (state.isOnConfirm()) return '\x03';
    return '\0';
  }
  if (state.cursorY >= 0 && state.cursorY < KeyboardState::NUM_ROWS && state.cursorX >= 0 &&
      state.cursorX < KeyboardState::KEYS_PER_ROW) {
    return KEYBOARD_GRID[state.cursorY][state.cursorX];
  }
  return '\0';
}

void battery(const GfxRenderer& r, const Theme& t, int x, int y, int percent) {
  // Simple battery icon: [====]
  const int battW = 30;
  const int battH = 14;
  const int tipW = 3;
  const int tipH = 6;

  const int iconY = y + 3;  // shift icon down to align with text

  // Battery body outline
  r.drawRect(x, iconY, battW, battH, t.primaryTextBlack);

  // Battery tip (positive terminal)
  r.fillRect(x + battW, iconY + (battH - tipH) / 2, tipW, tipH, t.primaryTextBlack);

  // Fill level
  const int fillW = ((battW - 4) * percent) / 100;
  if (fillW > 0) {
    r.fillRect(x + 2, iconY + 2, fillW, battH - 4, t.primaryTextBlack);
  }

  // Percentage text
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  r.drawText(t.smallFontId, x + battW + tipW + 5, y, buf, t.primaryTextBlack);
}

void statusBar(const GfxRenderer& r, const Theme& t, int page, int total, int percent) {
  const int y = r.getScreenHeight() - 25;
  const int x = t.screenMarginSide;
  const int screenW = r.getScreenWidth();

  // Page numbers on left
  char pageStr[32];
  snprintf(pageStr, sizeof(pageStr), "%d / %d", page, total);
  r.drawText(t.smallFontId, x + 5, y, pageStr, t.primaryTextBlack);

  // Percentage on right
  char percentStr[8];
  snprintf(percentStr, sizeof(percentStr), "%d%%", percent);
  const int percentW = r.getTextWidth(t.smallFontId, percentStr);
  r.drawText(t.smallFontId, screenW - x - percentW - 5, y, percentStr, t.primaryTextBlack);
}

void bookCard(const GfxRenderer& r, const Theme& t, int y, const char* titleText, const char* author,
              const uint8_t* cover, int coverW, int coverH) {
  const int x = t.screenMarginSide + 10;
  const int screenW = r.getScreenWidth();

  // Draw cover if available
  int textX = x;
  if (cover != nullptr && coverW > 0 && coverH > 0) {
    // Scale cover to fit (max 100x150)
    const int maxCoverW = 100;
    const int maxCoverH = 150;
    int drawW = coverW;
    int drawH = coverH;

    if (drawW > maxCoverW || drawH > maxCoverH) {
      const float scaleW = static_cast<float>(maxCoverW) / drawW;
      const float scaleH = static_cast<float>(maxCoverH) / drawH;
      const float scale = (scaleW < scaleH) ? scaleW : scaleH;
      drawW = static_cast<int>(drawW * scale);
      drawH = static_cast<int>(drawH * scale);
    }

    r.drawImage(cover, x, y, drawW, drawH);
    textX = x + drawW + 15;
  }

  // Draw title (may wrap)
  const int maxTextW = screenW - textX - t.screenMarginSide - 10;
  const auto titleLines = r.wrapTextWithHyphenation(t.readerFontId, titleText, maxTextW, 2, EpdFontFamily::BOLD);
  int textY = y + 10;
  const int lineHeight = r.getLineHeight(t.readerFontId);

  for (const auto& line : titleLines) {
    r.drawText(t.readerFontId, textX, textY, line.c_str(), t.primaryTextBlack, EpdFontFamily::BOLD);
    textY += lineHeight;
  }

  // Draw author below title
  if (author != nullptr && author[0] != '\0') {
    textY += 5;
    r.drawText(t.uiFontId, textX, textY, author, t.secondaryTextBlack);
  }
}

void fileEntry(const GfxRenderer& r, const Theme& t, int y, const char* name, bool isDir, bool selected) {
  const int x = t.screenMarginSide;
  const int w = r.getScreenWidth() - 2 * t.screenMarginSide;
  const int h = t.itemHeight;
  const int textY = y + (h - r.getLineHeight(t.uiFontId)) / 2;

  if (selected) {
    r.fillRect(x, y, w, h, t.selectionFillBlack);
  }

  // Build display name with trailing "/" for directories
  char displayName[132];
  if (isDir) {
    snprintf(displayName, sizeof(displayName), "%s/", name);
  } else {
    strncpy(displayName, name, sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
  }

  // Truncate if too long
  const int maxTextW = w - 2 * t.itemPaddingX;
  const auto truncated = r.truncatedText(t.uiFontId, displayName, maxTextW);

  r.drawText(t.uiFontId, x + t.itemPaddingX, textY, truncated.c_str(),
             selected ? t.selectionTextBlack : t.primaryTextBlack);
}

void chapterItem(const GfxRenderer& r, const Theme& t, int fontId, int y, const char* title, uint8_t depth,
                 bool selected, bool isCurrent) {
  constexpr int depthIndent = 12;
  constexpr int minWidth = 50;
  const int x = t.screenMarginSide + depth * depthIndent;
  const int w = std::max(minWidth, r.getScreenWidth() - x - t.screenMarginSide);
  const int h = t.itemHeight;
  const int textY = y + (h - r.getLineHeight(fontId)) / 2;

  // Selection highlight
  if (selected) {
    r.fillRect(x, y, w, h, t.selectionFillBlack);
  }

  // Current chapter indicator
  if (isCurrent) {
    r.drawText(fontId, t.screenMarginSide, textY, ">", t.primaryTextBlack);
  }

  // Truncated title
  const int maxTitleW = w - t.itemPaddingX * 2;
  const auto truncTitle = r.truncatedText(fontId, title, maxTitleW);
  r.drawText(fontId, x + t.itemPaddingX, textY, truncTitle.c_str(),
             selected ? t.selectionTextBlack : t.primaryTextBlack);
}

void wifiEntry(const GfxRenderer& r, const Theme& t, int y, const char* ssid, int signal, bool locked, bool selected) {
  const int x = t.screenMarginSide;
  const int w = r.getScreenWidth() - 2 * t.screenMarginSide;
  const int h = t.itemHeight;
  const int textY = y + (h - r.getLineHeight(t.uiFontId)) / 2;

  if (selected) {
    r.fillRect(x, y, w, h, t.selectionFillBlack);
  }

  const bool textColor = selected ? t.selectionTextBlack : t.primaryTextBlack;

  // SSID name
  const int maxSsidW = w - 80;
  const auto truncatedSsid = r.truncatedText(t.uiFontId, ssid, maxSsidW);
  r.drawText(t.uiFontId, x + t.itemPaddingX, textY, truncatedSsid.c_str(), textColor);

  // Signal strength indicator (simple bars)
  const int signalX = w - 45;
  const int barW = 4;
  const int barSpacing = 2;
  const int barBaseY = y + h - 8;

  // Draw 4 bars based on signal strength (0-100)
  for (int i = 0; i < 4; i++) {
    const int barH = 4 + i * 4;
    const int barX = signalX + i * (barW + barSpacing);
    const int threshold = 25 * (i + 1);

    if (signal >= threshold) {
      r.fillRect(barX, barBaseY - barH, barW, barH, textColor);
    } else {
      r.drawRect(barX, barBaseY - barH, barW, barH, textColor);
    }
  }

  // Lock indicator
  if (locked) {
    r.drawText(t.smallFontId, w - 15, y + 8, "*", textColor);
  }
}

void centeredText(const GfxRenderer& r, const Theme& t, int y, const char* str) {
  r.drawCenteredText(t.uiFontId, y, str, t.primaryTextBlack);
}

void centeredMessage(const GfxRenderer& r, const Theme& t, int fontId, const char* message) {
  r.clearScreen(t.backgroundColor);
  const int y = r.getScreenHeight() / 2 - r.getLineHeight(fontId) / 2;
  r.drawCenteredText(fontId, y, message, t.primaryTextBlack, EpdFontFamily::BOLD);
  r.displayBuffer();
}

void bookPlaceholder(const GfxRenderer& r, const Theme& t, int x, int y, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }

  const bool bgColor = !t.primaryTextBlack;
  const bool fgColor = t.primaryTextBlack;

  r.fillRect(x, y, width, height, bgColor);

  constexpr int minSize = 50;
  if (width < minSize || height < minSize) {
    return;
  }

  // Scale factors from base design (400x500)
  const float scaleX = static_cast<float>(width) / 400.0f;
  const float scaleY = static_cast<float>(height) / 500.0f;
  const float scale = std::min(scaleX, scaleY);

  // Center the design within the area
  const int designW = static_cast<int>(400 * scale);
  const int designH = static_cast<int>(500 * scale);
  const int offsetX = x + (width - designW) / 2;
  const int offsetY = y + (height - designH) / 2;

  // Helper lambdas for scaled coordinates
  auto sx = [&](int v) { return offsetX + static_cast<int>(v * scale); };
  auto sy = [&](int v) { return offsetY + static_cast<int>(v * scale); };
  auto sw = [&](int v) { return std::max(1, static_cast<int>(v * scale)); };

  // Line thickness for outlines
  const int lineThick = std::max(2, sw(4));

  // Helper to draw thick rectangle outline
  auto drawThickRect = [&](int rx, int ry, int rw, int rh) {
    r.fillRect(rx, ry, rw, lineThick, fgColor);                   // top
    r.fillRect(rx, ry + rh - lineThick, rw, lineThick, fgColor);  // bottom
    r.fillRect(rx, ry, lineThick, rh, fgColor);                   // left
    r.fillRect(rx + rw - lineThick, ry, lineThick, rh, fgColor);  // right
  };

  // 1. Draw spine (left side, filled)
  r.fillRect(sx(20), sy(35), sw(20), sw(430), fgColor);

  // 2. Draw page block outline (right side)
  drawThickRect(sx(330), sy(35), sw(50), sw(430));
  // Page lines (5 horizontal lines, drawn as thin rectangles for thickness)
  const int pageLineYs[] = {65, 110, 155, 200, 245};
  for (int py : pageLineYs) {
    r.fillRect(sx(340), sy(py), sw(35), lineThick, fgColor);
  }

  // 3. Draw main cover outline (front)
  drawThickRect(sx(35), sy(35), sw(295), sw(430));

  // 4. Draw bookmark ribbon (filled rectangle + triangle)
  const int bmX = sx(280);
  const int bmY = sy(35);
  const int bmW = sw(40);
  const int bmH = sw(45);
  r.fillRect(bmX, bmY, bmW, bmH, fgColor);
  // Triangle point (draw as filled lines)
  const int triangleTop = bmY + bmH;
  const int triangleTip = sy(100);
  const int bmCenterX = bmX + bmW / 2;
  for (int ty = triangleTop; ty <= triangleTip; ty++) {
    int halfWidth = bmW / 2 * (triangleTip - ty) / (triangleTip - triangleTop);
    if (halfWidth > 0) {
      r.drawLine(bmCenterX - halfWidth, ty, bmCenterX + halfWidth, ty, fgColor);
    }
  }

  // 5. Draw "No Cover" text centered on front cover
  const int coverCenterX = sx(35) + sw(295) / 2;
  const int coverCenterY = sy(35) + sw(430) / 2;
  const char* noCoverText = "No Cover";
  const int textWidth = r.getTextWidth(t.uiFontId, noCoverText);
  const int textX = coverCenterX - textWidth / 2;
  const int textY = coverCenterY - r.getLineHeight(t.uiFontId) / 2;
  r.drawText(t.uiFontId, textX, textY, noCoverText, fgColor);
}

void overlayBox(const GfxRenderer& r, const Theme& t, int fontId, int y, const char* message) {
  constexpr int boxMargin = 20;
  const int textWidth = r.getTextWidth(fontId, message);
  const int boxWidth = textWidth + boxMargin * 2;
  const int boxHeight = r.getLineHeight(fontId) + boxMargin * 2;
  const int boxX = (r.getScreenWidth() - boxWidth) / 2;

  r.fillRect(boxX, y, boxWidth, boxHeight, !t.primaryTextBlack);
  r.drawText(fontId, boxX + boxMargin, y + boxMargin, message, t.primaryTextBlack);
  r.drawRect(boxX + 5, y + 5, boxWidth - 10, boxHeight - 10, t.primaryTextBlack);
}

void twoColumnRow(const GfxRenderer& r, const Theme& t, int y, const char* label, const char* value) {
  const int labelX = t.screenMarginSide + t.itemPaddingX;
  const int valueX = r.getScreenWidth() / 2;

  r.drawText(t.uiFontId, labelX, y, label, t.primaryTextBlack);
  r.drawText(t.uiFontId, valueX, y, value, t.secondaryTextBlack);
}

void readerStatusBar(const GfxRenderer& r, const Theme& t, int marginLeft, int marginRight, int marginBottom,
                     const ReaderStatusBarData& data) {
  if (data.mode == 0) return;  // StatusNone

  const auto screenHeight = r.getScreenHeight();
  const auto screenWidth = r.getScreenWidth();
  const int textY = screenHeight - marginBottom - 2;
  int percentageTextWidth = 0;

  // 1. Battery (left side)
  char percentageText[8];
  int percentage = data.batteryPercent;
  if (percentage < 0) {
    snprintf(percentageText, sizeof(percentageText), "--%%");
    percentage = 0;
  } else {
    snprintf(percentageText, sizeof(percentageText), "%d%%", percentage);
  }
  percentageTextWidth = r.getTextWidth(t.smallFontId, percentageText);
  r.drawText(t.smallFontId, 20 + marginLeft, textY, percentageText, t.primaryTextBlack);

  // Battery icon (15x10 px)
  constexpr int batteryWidth = 15;
  constexpr int batteryHeight = 10;
  const int x = marginLeft;
  const int y = textY + 5;

  // Draw battery outline
  r.drawLine(x, y, x + batteryWidth - 4, y, t.primaryTextBlack);
  r.drawLine(x, y + batteryHeight - 1, x + batteryWidth - 4, y + batteryHeight - 1, t.primaryTextBlack);
  r.drawLine(x, y, x, y + batteryHeight - 1, t.primaryTextBlack);
  r.drawLine(x + batteryWidth - 4, y, x + batteryWidth - 4, y + batteryHeight - 1, t.primaryTextBlack);
  // Battery terminal
  r.drawLine(x + batteryWidth - 3, y + 2, x + batteryWidth - 1, y + 2, t.primaryTextBlack);
  r.drawLine(x + batteryWidth - 3, y + batteryHeight - 3, x + batteryWidth - 1, y + batteryHeight - 3,
             t.primaryTextBlack);
  r.drawLine(x + batteryWidth - 1, y + 2, x + batteryWidth - 1, y + batteryHeight - 3, t.primaryTextBlack);

  // Fill level
  int filledWidth = percentage * (batteryWidth - 5) / 100 + 1;
  if (filledWidth > batteryWidth - 5) filledWidth = batteryWidth - 5;
  if (filledWidth > 0) {
    r.fillRect(x + 1, y + 1, filledWidth, batteryHeight - 2, t.primaryTextBlack);
  }

  // 2. Page numbers (right side)
  char pageStr[16];
  if (data.isPartial || data.totalPages == 0) {
    snprintf(pageStr, sizeof(pageStr), "%d/-", data.currentPage);
  } else {
    snprintf(pageStr, sizeof(pageStr), "%d/%d", data.currentPage, data.totalPages);
  }
  int pageTextWidth = r.getTextWidth(t.smallFontId, pageStr);
  r.drawText(t.smallFontId, screenWidth - marginRight - pageTextWidth, textY, pageStr, t.primaryTextBlack);

  // 3. Title (center)
  if (data.title && data.title[0] != '\0') {
    const int batteryAreaWidth = 20 + percentageTextWidth;
    const int titleMarginLeft = batteryAreaWidth + 30 + marginLeft;
    const int titleMarginRight = marginRight + pageTextWidth + 10;
    const int availableTextWidth = screenWidth - titleMarginLeft - titleMarginRight;

    if (availableTextWidth <= 0) return;

    std::string titleStr = data.title;
    int titleWidth = r.getTextWidth(t.smallFontId, titleStr.c_str());

    // Truncate title if too wide (using truncatedText for UTF-8 safety)
    if (titleWidth > availableTextWidth) {
      titleStr = r.truncatedText(t.smallFontId, titleStr.c_str(), availableTextWidth);
      titleWidth = r.getTextWidth(t.smallFontId, titleStr.c_str());
    }

    r.drawText(t.smallFontId, titleMarginLeft + (availableTextWidth - titleWidth) / 2, textY, titleStr.c_str(),
               t.primaryTextBlack);
  }
}

}  // namespace ui
