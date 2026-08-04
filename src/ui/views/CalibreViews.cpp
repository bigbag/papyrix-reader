#include "CalibreViews.h"

#include <I18n.h>

#include <algorithm>

#include "../Elements.h"

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const CalibreView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(CALIBRE_SYNC));

  const int centerY = r.getScreenHeight() / 2 - 60;
  const int maxTextWidth = r.getScreenWidth() - 2 * (t.screenMarginSide + t.itemPaddingX);
  const int lineHeight = r.getLineHeight(t.uiFontId);
  const int statusLines = centeredTextWrapped(r, t.uiFontId, centerY, v.statusMsg, maxTextWidth,
                                              CalibreView::MAX_TEXT_LINES, t.primaryTextBlack);
  int nextY = centerY + std::max(40, std::max(1, statusLines) * lineHeight + 10);

  if (v.helpText[0] != '\0') {
    const int helpLines = centeredTextWrapped(r, t.uiFontId, nextY, v.helpText, maxTextWidth,
                                              CalibreView::MAX_TEXT_LINES, t.primaryTextBlack);
    nextY += std::max(40, std::max(1, helpLines) * lineHeight + 10);
  }

  if (v.status == CalibreView::Status::Receiving && v.total > 0) {
    const int progressY = std::max(centerY + 50, nextY);
    progress(r, t, progressY, v.received, v.total);

    char sizeStr[32];
    snprintf(sizeStr, sizeof(sizeStr), "%d / %d KB", v.received / 1024, v.total / 1024);
    centeredText(r, t, progressY + 50, sizeStr);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

}  // namespace ui
