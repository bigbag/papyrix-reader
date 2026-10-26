#include "ReaderViews.h"

#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace ui {

void readerTouchHints(const GfxRenderer& r, bool black) {
  int top, right, bottom, left;
  r.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int height = r.getScreenHeight();
  const int rightX = r.getScreenWidth() - right - 4;
  const int middleY = (height - 24) / 2;
  const int bottomY = height - touch::READER_BOTTOM_ZONE_HEIGHT + (touch::READER_BOTTOM_ZONE_HEIGHT - 18) / 2;
  const auto drawTabs = [&](int y, int tabHeight) {
    r.fillRect(left, y, 2, tabHeight, black);
    r.fillRect(left + 2, y + 1, 1, tabHeight - 2, black);
    r.fillRect(left + 3, y + 2, 1, tabHeight - 4, black);
    r.fillRect(rightX + 2, y, 2, tabHeight, black);
    r.fillRect(rightX + 1, y + 1, 1, tabHeight - 2, black);
    r.fillRect(rightX, y + 2, 1, tabHeight - 4, black);
  };
  drawTabs(middleY, 24);
  drawTabs(bottomY, 18);
}

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

void render(const GfxRenderer& r, const Theme& t, const BookStatsView& v) {
  r.clearScreen(t.backgroundColor);

  const int maxTextWidth = r.getScreenWidth() - 2 * (t.screenMarginSide + 8);
  const auto titleLines =
      r.wrapTextWithHyphenation(t.uiFontId, v.title, maxTextWidth, BookStatsView::MAX_TITLE_LINES, EpdFontFamily::BOLD);
  const int titleLineHeight = r.getLineHeight(t.uiFontId);
  int textY = 10;
  for (const auto& line : titleLines) {
    r.drawCenteredText(t.uiFontId, textY, line.c_str(), t.primaryTextBlack, EpdFontFamily::BOLD);
    textY += titleLineHeight;
  }

  if (v.author[0] != '\0') {
    const auto author = r.truncatedText(t.smallFontId, v.author, maxTextWidth);
    const int authorY = std::max(52, textY + 4);
    r.drawCenteredText(t.smallFontId, authorY, author.c_str(), t.secondaryTextBlack);
  }
  twoColumnRow(r, t, 110, tr(PROGRESS), v.progress);
  twoColumnRow(r, t, 150, tr(TIME_READ), v.timeRead);
  twoColumnRow(r, t, 190, tr(SESSIONS), v.sessions);
  ui::buttonBar(r, t, tr(BACK), v.showOpen ? tr(OPEN) : "", "", v.showRemove ? tr(REMOVE_RECENT) : "");
  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const ReaderMenuView& v) {
  if (!v.visible) return;

  const char* items[] = {tr(CHAPTERS), tr(BOOKMARKS), tr(BOOK_STATS), tr(BACK)};
  static_assert(std::size(items) == ReaderMenuView::ITEM_COUNT);
  if (!v.hasFrontLight && !v.hasTouch) {
    ui::popupMenu(r, t, tr(MENU), items, v.itemCount(), v.selected);
    r.displayBuffer();
    return;
  }

  const int screenW = r.getScreenWidth();
  const int screenH = r.getScreenHeight();
  const int16_t itemHeight = static_cast<int16_t>(t.itemHeight);
  const auto geometry = v.layout(screenW, screenH, itemHeight);
  const touch::Rect menu = geometry.menu;
  r.clearArea(menu.x, menu.y, menu.width, menu.height, t.backgroundColor);
  r.drawRect(menu.x, menu.y, menu.width, menu.height, t.primaryTextBlack);

  const int titleW = r.getTextWidth(t.uiFontId, tr(MENU), EpdFontFamily::BOLD);
  r.drawText(t.uiFontId, menu.x + (menu.width - titleW) / 2, menu.y + 10, tr(MENU), t.primaryTextBlack,
             EpdFontFamily::BOLD);
  r.drawLine(menu.x + 10, menu.y + 35, menu.x + menu.width - 10, menu.y + 35, t.primaryTextBlack);

  const int lineHeight = r.getLineHeight(t.uiFontId);
  for (int i = 0; i < v.itemCount(); i++) {
    const touch::Rect item = geometry.items[i];
    const int textW = r.getTextWidth(t.uiFontId, items[i]);
    const int textX = item.x + (item.width - textW) / 2;
    const int textY = item.y + (item.height - lineHeight) / 2;
    if (i == v.selected) {
      r.fillRect(item.x, item.y, item.width, item.height, t.selectionFillBlack);
      r.drawText(t.uiFontId, textX, textY, items[i], t.selectionTextBlack);
    } else {
      r.drawText(t.uiFontId, textX, textY, items[i], t.primaryTextBlack);
    }
  }

  const auto drawCentered = [&](touch::Rect bounds, const char* text, bool black) {
    const int textW = r.getTextWidth(t.uiFontId, text);
    r.drawText(t.uiFontId, bounds.x + (bounds.width - textW) / 2, bounds.y + (bounds.height - lineHeight) / 2, text,
               black);
  };
  const auto drawBar = [&](touch::Rect minus, touch::Rect bar, touch::Rect plus, uint8_t value) {
    r.drawRect(minus.x, minus.y, minus.width, minus.height, t.primaryTextBlack);
    drawCentered(minus, "-", t.primaryTextBlack);
    r.drawRect(plus.x, plus.y, plus.width, plus.height, t.primaryTextBlack);
    drawCentered(plus, "+", t.primaryTextBlack);
    r.drawRect(bar.x, bar.y, bar.width, bar.height, t.primaryTextBlack);
    const int fillWidth = bar.width * std::min<int>(value, 100) / 100;
    if (fillWidth > 2) r.fillRect(bar.x + 1, bar.y + 1, fillWidth - 2, bar.height - 2, t.primaryTextBlack);
  };
  const auto drawLabel = [&](touch::Rect label, const char* text, uint8_t value) {
    char percent[8];
    snprintf(percent, sizeof(percent), "%u%%", static_cast<unsigned>(std::min<int>(value, 100)));
    const int textY = label.y + (label.height - lineHeight) / 2;
    r.drawText(t.uiFontId, label.x, textY, text, t.primaryTextBlack);
    const int percentW = r.getTextWidth(t.uiFontId, percent);
    r.drawText(t.uiFontId, label.x + label.width - percentW, textY, percent, t.primaryTextBlack);
  };

  if (v.hasFrontLight) {
    const int separatorY = geometry.items[0].y - 6;
    r.drawLine(menu.x + 10, separatorY, menu.x + menu.width - 10, separatorY, t.primaryTextBlack);
    drawLabel(geometry.brightnessLabel, tr(BRIGHTNESS), v.brightness);
    drawBar(geometry.brightnessMinus, geometry.brightnessBar, geometry.brightnessPlus, v.brightness);
    drawLabel(geometry.warmthLabel, tr(WARMTH), v.warmth);
    drawBar(geometry.warmthMinus, geometry.warmthBar, geometry.warmthPlus, v.warmth);
  }

  r.displayBuffer();
}

}  // namespace ui
