#include "Elements.h"

#include <cstdio>
#include <cstring>

#include "core/PapyrixSettings.h"

namespace ui {

static uint8_t frontButtonLayout_ = 0;

void setFrontButtonLayout(uint8_t layout) { frontButtonLayout_ = layout; }

void title(const GfxRenderer& r, const Theme& t, int y, const char* text) {
  r.drawCenteredText(t.uiFontId, y, text, t.primaryTextBlack);
}

int titleBottomY(const GfxRenderer& r, const Theme& t) { return t.screenMarginTop + r.getLineHeight(t.uiFontId); }

int contentStartY(const GfxRenderer& r, const Theme& t, int gap) { return titleBottomY(r, t) + gap; }

void brandTitle(const GfxRenderer& r, const Theme& t, int y, const char* text) {
  r.drawText(t.uiFontId, t.screenMarginSide, y, text, t.primaryTextBlack);
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

void drawTextCenteredInRect(const GfxRenderer& r, const int fontId, const int x, const int y, const int width,
                            const int height, const char* text, const bool black, const EpdFontFamily::Style style) {
  if (text == nullptr || *text == '\0') {
    return;
  }

  int minX = 0;
  int minY = 0;
  int maxX = 0;
  int maxY = 0;
  r.getTextBounds(fontId, text, &minX, &minY, &maxX, &maxY, style);

  const int textWidth = maxX - minX;
  const int textHeight = maxY - minY;
  const int textX = x + (width - textWidth) / 2 - minX;
  const int ascender = r.getFontAscenderSize(fontId);
  const int screenTop = 2 * ascender - maxY;
  const int textY = y + (height - textHeight) / 2 - screenTop;
  r.drawText(fontId, textX, textY, text, black, style);
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
  r.drawCenteredText(t.uiFontId, dialogY + 20, titleText, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Draw message
  r.drawCenteredText(t.uiFontId, dialogY + 60, msg, t.primaryTextBlack);

  // Draw buttons (Yes/No)
  const int btnW = 80;
  const int btnH = 30;
  const int btnY = dialogY + dialogH - 50;
  const int yesX = dialogX + (dialogW / 2) - btnW - 20;
  const int noX = dialogX + (dialogW / 2) + 20;
  const char* yesLabel = "Yes";
  const char* noLabel = "No";

  auto drawButton = [&](int x, const char* label, bool isSelected) {
    if (isSelected) {
      r.fillRect(x, btnY, btnW, btnH, t.selectionFillBlack);
    } else {
      r.drawRect(x, btnY, btnW, btnH, t.primaryTextBlack);
    }

    drawTextCenteredInRect(r, t.uiFontId, x, btnY, btnW, btnH, label,
                           isSelected ? t.selectionTextBlack : t.primaryTextBlack);
  };

  drawButton(yesX, yesLabel, selected == 0);
  drawButton(noX, noLabel, selected == 1);
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

void battery(const GfxRenderer& r, const Theme& t, int x, int y, int percent, bool charging) {
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

  if (charging) {
    // Small zig-zag bolt centered in the body. Punch a background-color halo
    // first so the bolt stays readable both over the empty white area and
    // over the filled black area.
    const int boltCx = x + battW / 2;
    const int boltTop = iconY + 2;
    const int boltBot = iconY + battH - 3;
    const int boltMid = (boltTop + boltBot) / 2;
    r.fillRect(boltCx - 3, boltTop - 1, 7, boltBot - boltTop + 3, t.backgroundColor);
    r.drawLine(boltCx + 2, boltTop, boltCx - 1, boltMid, t.primaryTextBlack);
    r.drawLine(boltCx - 1, boltMid, boltCx + 1, boltMid, t.primaryTextBlack);
    r.drawLine(boltCx + 1, boltMid, boltCx - 2, boltBot, t.primaryTextBlack);
  }

  // Percentage text
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  r.drawText(t.smallFontId, x + battW + tipW + 5, y, buf, t.primaryTextBlack);
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
  const auto titleLines = r.wrapTextWithHyphenation(t.uiFontId, titleText, maxTextW, 2, EpdFontFamily::BOLD);
  int textY = y + 10;
  const int lineHeight = r.getLineHeight(t.uiFontId);

  for (const auto& line : titleLines) {
    r.drawText(t.uiFontId, textX, textY, line.c_str(), t.primaryTextBlack, EpdFontFamily::BOLD);
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
  (void)t;
  if (width <= 0 || height <= 0) {
    return;
  }
  r.fillRect(x, y, width, height, true);
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

void popupMenu(const GfxRenderer& r, const Theme& t, const char* titleText, const char* const* items, int itemCount,
               int selected) {
  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();

  const int menuW = 200;
  const int menuH = itemCount * (t.itemHeight + 8) + 40 + 10;
  const int menuX = (screenW - menuW) / 2;
  const int menuY = (screenH - menuH) / 2;

  r.clearArea(menuX, menuY, menuW, menuH, t.backgroundColor);
  r.drawRect(menuX, menuY, menuW, menuH, t.primaryTextBlack);

  // Title
  const int titleW = r.getTextWidth(t.uiFontId, titleText, EpdFontFamily::BOLD);
  const int titleX = menuX + (menuW - titleW) / 2;
  r.drawText(t.uiFontId, titleX, menuY + 10, titleText, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Separator
  r.drawLine(menuX + 10, menuY + 35, menuX + menuW - 10, menuY + 35, t.primaryTextBlack);

  // Items
  const int itemStartY = menuY + 45;
  for (int i = 0; i < itemCount; i++) {
    const int itemY = itemStartY + i * (t.itemHeight + 8);
    const int itemX = menuX + 10;
    const int itemW = menuW - 20;

    const int textW = r.getTextWidth(t.uiFontId, items[i]);
    const int textX = itemX + (itemW - textW) / 2;
    const int textY = itemY + (t.itemHeight - r.getLineHeight(t.uiFontId)) / 2;

    if (i == selected) {
      r.fillRect(itemX, itemY, itemW, t.itemHeight, t.selectionFillBlack);
      r.drawText(t.uiFontId, textX, textY, items[i], t.selectionTextBlack);
    } else {
      r.drawText(t.uiFontId, textX, textY, items[i], t.primaryTextBlack);
    }
  }
}

}  // namespace ui
