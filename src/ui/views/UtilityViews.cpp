#include "UtilityViews.h"

#include <I18n.h>

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const MessageView& v) {
  r.clearScreen(t.backgroundColor);

  const int centerY = r.getScreenHeight() / 2;
  centeredText(r, t, centerY, v.message);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ConfirmView& v) {
  r.clearScreen(t.backgroundColor);

  dialog(r, t, v.title, v.message, v.selected);

  ButtonBar btns{tr(BACK), tr(SELECT), "<", ">"};
  buttonBar(r, t, btns);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const KeyboardView& v) {
  r.clearScreen(t.backgroundColor);

  // Title
  title(r, t, t.screenMarginTop, v.title);

  // Input field with border
  const int inputY = 50;
  const int inputX = t.screenMarginSide + 10;
  const int inputW = r.getScreenWidth() - 2 * inputX;
  const int inputH = 40;

  r.drawRect(inputX, inputY, inputW, inputH, t.primaryTextBlack);

  // Draw input text (or placeholder with cursor)
  if (v.inputLen > 0) {
    // Build display text (password mode shows asterisks)
    char displayBuf[KeyboardView::MAX_INPUT_LEN + 2];
    if (v.isPassword) {
      // Show asterisks for password
      for (int i = 0; i < v.inputLen && i < KeyboardView::MAX_INPUT_LEN - 1; i++) {
        displayBuf[i] = '*';
      }
      displayBuf[v.inputLen] = '_';  // Cursor
      displayBuf[v.inputLen + 1] = '\0';
    } else {
      // Copy input and add cursor
      for (int i = 0; i < v.inputLen; i++) {
        displayBuf[i] = v.input[i];
      }
      displayBuf[v.inputLen] = '_';  // Cursor
      displayBuf[v.inputLen + 1] = '\0';
    }

    // Truncate from left if too long
    const char* displayText = displayBuf;
    int textW = r.getTextWidth(t.uiFontId, displayText);
    const int maxW = inputW - 16;

    while (textW > maxW && *displayText != '\0') {
      displayText++;
      textW = r.getTextWidth(t.uiFontId, displayText);
    }

    r.drawText(t.uiFontId, inputX + 8, inputY + 10, displayText, t.primaryTextBlack);
  } else {
    r.drawText(t.uiFontId, inputX + 8, inputY + 10, "_", t.secondaryTextBlack);
  }

  // Keyboard below input
  const int keyboardY = inputY + inputH + 20;
  keyboard(r, t, keyboardY, v.keyboard);

  ButtonBar kbBtns{tr(BACK), tr(SELECT), "<", ">"};
  buttonBar(r, t, kbBtns);

  r.displayBuffer();
}

}  // namespace ui
