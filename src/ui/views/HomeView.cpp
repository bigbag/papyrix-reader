#include "HomeView.h"

#include <CoverHelpers.h>

#include <algorithm>
#include <cstdio>

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const HomeView& v) {
  // Only clear if no cover (HomeState handles clear when cover present)
  if (!v.hasCoverBmp) {
    r.clearScreen(t.backgroundColor);
  }

  const int pageWidth = r.getScreenWidth();
  const int pageHeight = r.getScreenHeight();

  // "Papyrix" brand title - bold in top-left corner
  brandTitle(r, t, 10, "Papyrix");

  // Battery indicator - top right
  battery(r, t, pageWidth - 90, 10, v.batteryPercent);

  // Book card dimensions (70% width, centered)
  const auto card = CardDimensions::calculate(pageWidth, pageHeight);
  const int cardX = card.x;
  const int cardY = card.y;
  const int cardWidth = card.width;
  const int cardHeight = card.height;

  const bool hasCover = v.coverData != nullptr || v.hasCoverBmp;

  if (v.hasBook) {
    // Draw cover image if available (in-memory version; BMP cover rendered by HomeState)
    const auto coverArea = card.getCoverArea();
    if (v.coverData != nullptr && v.coverWidth > 0 && v.coverHeight > 0) {
      const auto rect = CoverHelpers::calculateCenteredRect(v.coverWidth, v.coverHeight, coverArea.x, coverArea.y,
                                                            coverArea.width, coverArea.height);
      r.drawImage(v.coverData, rect.x, rect.y, v.coverWidth, v.coverHeight);
    }

    // Draw book placeholder when no cover available
    if (!hasCover) {
      bookPlaceholder(r, t, coverArea.x, coverArea.y, coverArea.width, coverArea.height);
    }

    // Title/author below the cover area
    const int titleFontId = (v.titleFontId >= 0) ? v.titleFontId : t.uiFontId;
    const int titleLineHeight = r.getLineHeight(titleFontId);
    constexpr int textSpacing = 10;
    constexpr int buttonBarHeight = 50;
    const int textStartY = cardY + cardHeight + textSpacing;
    const int availableHeight = pageHeight - textStartY - buttonBarHeight - textSpacing;
    const int authorHeight = (v.bookAuthor[0] != '\0') ? titleLineHeight * 3 / 2 : 0;
    const int maxTitleHeight = availableHeight - authorHeight;
    const int maxTitleLines = std::max(1, maxTitleHeight / titleLineHeight);

    const auto titleLines = r.wrapTextWithHyphenation(titleFontId, v.bookTitle, cardWidth, std::min(3, maxTitleLines));

    // Draw title lines centered
    int textY = textStartY;
    for (const auto& line : titleLines) {
      const int lineWidth = r.getTextWidth(titleFontId, line.c_str());
      const int lineX = cardX + (cardWidth - lineWidth) / 2;
      r.drawText(titleFontId, lineX, textY, line.c_str(), t.primaryTextBlack);
      textY += titleLineHeight;
    }

    // Draw author if available
    if (v.bookAuthor[0] != '\0') {
      textY += titleLineHeight / 4;
      const auto truncAuthor = r.truncatedText(titleFontId, v.bookAuthor, cardWidth);
      const int authorWidth = r.getTextWidth(titleFontId, truncAuthor.c_str());
      const int authorX = cardX + (cardWidth - authorWidth) / 2;
      r.drawText(titleFontId, authorX, textY, truncAuthor.c_str(), t.secondaryTextBlack);
    }

  } else {
    // No book open - show bordered placeholder with hint
    r.drawRect(cardX, cardY, cardWidth, cardHeight, t.primaryTextBlack);

    const int lineHeight = r.getLineHeight(t.uiFontId);
    const int centerY = cardY + cardHeight / 2;

    const char* noBookText = "No book open";
    const int noBookWidth = r.getTextWidth(t.uiFontId, noBookText);
    const int noBookX = cardX + (cardWidth - noBookWidth) / 2;
    r.drawText(t.uiFontId, noBookX, centerY - lineHeight, noBookText, t.primaryTextBlack);

    const char* hintText = "Press \"Files\" to browse";
    const int hintWidth = r.getTextWidth(t.uiFontId, hintText);
    const int hintX = cardX + (cardWidth - hintWidth) / 2;
    r.drawText(t.uiFontId, hintX, centerY + lineHeight / 2, hintText, t.secondaryTextBlack);
  }

  // Button hints - direct shortcuts (no menu navigation)
  buttonBar(r, t, v.buttons);

  // Note: displayBuffer() is NOT called here; HomeState will call it
  // after rendering the cover image on top of the card area
}

void render(const GfxRenderer& r, const Theme& t, const FileListView& v) {
  r.clearScreen(t.backgroundColor);

  // Title with path
  title(r, t, t.screenMarginTop, "Files");

  // Current path (truncated if needed)
  const int pathY = 40;
  const int maxPathW = r.getScreenWidth() - 2 * t.screenMarginSide - 16;
  const auto truncPath = r.truncatedText(t.smallFontId, v.currentPath, maxPathW);
  r.drawText(t.smallFontId, t.screenMarginSide + 8, pathY, truncPath.c_str(), t.secondaryTextBlack);

  // File list
  const int listStartY = 65;
  const int pageStart = v.getPageStart();
  const int pageEnd = v.getPageEnd();

  for (int i = pageStart; i < pageEnd; i++) {
    const int y = listStartY + (i - pageStart) * (t.itemHeight + t.itemSpacing);
    fileEntry(r, t, y, v.files[i].name, v.files[i].isDirectory, i == v.selected);
  }

  // Page indicator
  if (v.getPageCount() > 1) {
    char pageStr[16];
    snprintf(pageStr, sizeof(pageStr), "%d/%d", v.page + 1, v.getPageCount());
    const int pageY = r.getScreenHeight() - 50;
    centeredText(r, t, pageY, pageStr);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, ChapterListView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, "Chapters");

  constexpr int listStartY = 60;
  const int availableHeight = r.getScreenHeight() - listStartY - 50;
  const int itemHeight = t.itemHeight + t.itemSpacing;
  const int visibleCount = availableHeight / itemHeight;

  v.ensureVisible(visibleCount);

  const int end = std::min(v.scrollOffset + visibleCount, static_cast<int>(v.chapterCount));
  for (int i = v.scrollOffset; i < end; i++) {
    const int y = listStartY + (i - v.scrollOffset) * itemHeight;
    chapterItem(r, t, t.uiFontId, y, v.chapters[i].title, v.chapters[i].depth, i == v.selected, i == v.currentChapter);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

}  // namespace ui
