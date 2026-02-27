#include "ReaderViews.h"

#include <cstdio>

namespace ui {

// Static definitions
constexpr const char* const ReaderMenuView::ITEMS[];

void renderStatusBar(const GfxRenderer& r, const Theme& t, const ReaderStatusView& v) {
  // Draw status bar at bottom of screen
  statusBar(r, t, v.currentPage, v.totalPages, v.progressPercent);
}

void render(const GfxRenderer& r, const Theme& t, const CoverPageView& v) {
  r.clearScreen(t.backgroundColor);

  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();

  // Draw cover image centered in upper portion
  if (v.coverData != nullptr) {
    // Calculate scaled size to fit screen while maintaining aspect ratio
    // Use 450x750 containment (0.6 ratio) to match converted image dimensions and avoid scaling artifacts
    const int maxW = 450;
    const int maxH = 750;

    int drawW = v.coverWidth;
    int drawH = v.coverHeight;

    if (drawW > maxW || drawH > maxH) {
      const float scaleW = static_cast<float>(maxW) / drawW;
      const float scaleH = static_cast<float>(maxH) / drawH;
      const float scale = (scaleW < scaleH) ? scaleW : scaleH;
      drawW = static_cast<int>(drawW * scale);
      drawH = static_cast<int>(drawH * scale);
    }

    const int coverX = (screenW - drawW) / 2;
    const int coverY = 20;
    image(r, coverX, coverY, v.coverData, drawW, drawH);
  }

  // Title below cover
  const int titleY = screenH - 120;
  const int maxTitleW = screenW - 40;

  if (v.title[0] != '\0') {
    // Wrap title if needed
    const auto titleLines = r.wrapTextWithHyphenation(t.readerFontId, v.title, maxTitleW, 2, EpdFontFamily::BOLD);
    int lineY = titleY;
    const int lineHeight = r.getLineHeight(t.readerFontId);

    for (const auto& line : titleLines) {
      r.drawCenteredText(t.readerFontId, lineY, line.c_str(), t.primaryTextBlack, EpdFontFamily::BOLD);
      lineY += lineHeight;
    }
  }

  // Author below title
  if (v.author[0] != '\0') {
    const int authorY = screenH - 50;
    r.drawCenteredText(t.uiFontId, authorY, v.author, t.secondaryTextBlack);
  }

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderMenuView& v) {
  if (!v.visible) return;

  ui::popupMenu(r, t, "Menu", ReaderMenuView::ITEMS, ReaderMenuView::ITEM_COUNT, v.selected);
  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const JumpToPageView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Go to Page");

  const int centerY = r.getScreenHeight() / 2 - 40;

  // Current page number (large)
  char pageStr[16];
  snprintf(pageStr, sizeof(pageStr), "%d", v.targetPage);
  r.drawCenteredText(t.readerFontIdLarge, centerY, pageStr, t.primaryTextBlack, EpdFontFamily::BOLD);

  // Range info
  char rangeStr[32];
  snprintf(rangeStr, sizeof(rangeStr), "of %d", v.maxPage);
  centeredText(r, t, centerY + 50, rangeStr);

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

}  // namespace ui
