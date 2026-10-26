#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../Elements.h"

namespace ui {
class OverlayTapGuard {
 public:
  void closedAt(uint32_t nowMs) {
    suppressUntilMs_ = nowMs + 250;
    active_ = true;
  }
  bool suppressPageTap(uint32_t nowMs) {
    if (!active_) return false;
    if (static_cast<int32_t>(suppressUntilMs_ - nowMs) > 0) return true;
    active_ = false;
    return false;
  }

 private:
  uint32_t suppressUntilMs_ = 0;
  bool active_ = false;
};

// ============================================================================
// ReaderStatusView - Status bar for reader screens
// ============================================================================

struct ReaderStatusView {
  int16_t currentPage = 1;
  int16_t totalPages = 1;
  int8_t progressPercent = 0;
  bool showProgress = true;
  bool needsRender = true;

  void setPage(int current, int total) {
    currentPage = static_cast<int16_t>(current);
    totalPages = static_cast<int16_t>(total);
    if (totalPages > 0) {
      progressPercent = static_cast<int8_t>((current * 100) / total);
    }
    needsRender = true;
  }

  void setShowProgress(bool show) {
    showProgress = show;
    needsRender = true;
  }
};

void renderStatusBar(const GfxRenderer& r, const Theme& t, const ReaderStatusView& v);

// ============================================================================
// CoverPageView - Book cover display (for EPUB cover pages)
// ============================================================================

struct CoverPageView {
  static constexpr int MAX_TITLE_LEN = 128;
  static constexpr int MAX_AUTHOR_LEN = 64;

  // External cover image pointer (not owned)
  const uint8_t* coverData = nullptr;
  int16_t coverWidth = 0;
  int16_t coverHeight = 0;

  char title[MAX_TITLE_LEN] = {0};
  char author[MAX_AUTHOR_LEN] = {0};
  bool needsRender = true;

  void setCover(const uint8_t* data, int w, int h) {
    coverData = data;
    coverWidth = static_cast<int16_t>(w);
    coverHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setTitle(const char* t) {
    strncpy(title, t, MAX_TITLE_LEN - 1);
    title[MAX_TITLE_LEN - 1] = '\0';
    needsRender = true;
  }

  void setAuthor(const char* a) {
    strncpy(author, a, MAX_AUTHOR_LEN - 1);
    author[MAX_AUTHOR_LEN - 1] = '\0';
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const CoverPageView& v);

// ============================================================================
// BookStatsView - Shared per-book reading statistics
// ============================================================================

inline void formatReadingDuration(char* out, size_t outSize, uint32_t totalSeconds) {
  if (!out || outSize == 0) return;
  if (totalSeconds == 0) {
    snprintf(out, outSize, "—");
  } else if (totalSeconds < 60) {
    snprintf(out, outSize, "%lus", static_cast<unsigned long>(totalSeconds));
  } else if (totalSeconds < 3600) {
    snprintf(out, outSize, "%lum", static_cast<unsigned long>(totalSeconds / 60));
  } else {
    snprintf(out, outSize, "%luh %lum", static_cast<unsigned long>(totalSeconds / 3600),
             static_cast<unsigned long>((totalSeconds % 3600) / 60));
  }
}

inline void formatBookStatsSummary(char* out, size_t outSize, bool hasProgress, uint8_t progressPercent,
                                   uint32_t totalSeconds) {
  if (!out || outSize == 0) return;

  char duration[24];
  formatReadingDuration(duration, sizeof(duration), totalSeconds);
  if (hasProgress && totalSeconds > 0) {
    snprintf(out, outSize, "%u%% · %s", std::min<unsigned int>(progressPercent, 100), duration);
  } else if (hasProgress) {
    snprintf(out, outSize, "%u%%", std::min<unsigned int>(progressPercent, 100));
  } else if (totalSeconds > 0) {
    snprintf(out, outSize, "%s", duration);
  } else {
    snprintf(out, outSize, "—");
  }
}

struct BookStatsView {
  enum class Hit : uint8_t { None, Back, Open, Remove };
  static constexpr int MAX_TITLE_LINES = 2;

  char title[129] = {};
  char author[65] = {};
  char progress[8] = {};
  char timeRead[24] = {};
  char sessions[16] = {};
  bool showOpen = false;
  bool showRemove = false;
  bool needsRender = true;

  void setBook(const char* bookTitle, const char* bookAuthor) {
    strncpy(title, bookTitle ? bookTitle : "", sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    strncpy(author, bookAuthor ? bookAuthor : "", sizeof(author) - 1);
    author[sizeof(author) - 1] = '\0';
    needsRender = true;
  }

  void setStats(bool hasProgress, uint8_t progressPercent, uint32_t totalSeconds, uint32_t sessionCount) {
    if (hasProgress) {
      snprintf(progress, sizeof(progress), "%u%%", std::min<unsigned int>(progressPercent, 100));
    } else {
      snprintf(progress, sizeof(progress), "—");
    }
    formatReadingDuration(timeRead, sizeof(timeRead), totalSeconds);
    snprintf(sessions, sizeof(sessions), "%lu", static_cast<unsigned long>(sessionCount));
    needsRender = true;
  }
  Hit hitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight, bool frontLrbc = false) const {
    const int action = touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc);
    if (action == 0) return Hit::Back;
    if (action == 1 && showOpen) return Hit::Open;
    if (action == 3 && showRemove) return Hit::Remove;
    return Hit::None;
  }
};

void render(const GfxRenderer& r, const Theme& t, const BookStatsView& v);

// ============================================================================
// ReaderMenuView - In-reader quick menu overlay
// ============================================================================

struct ReaderMenuView {
  struct Hit {
    enum class Type : uint8_t { None, Item, Brightness, Warmth, Dismiss };
    Type type = Type::None;
    int index = -1;
    int value = 0;
  };
  enum class Item : int8_t { Chapters, Bookmarks, BookStats, Back, Count };
  static constexpr int ITEM_COUNT = static_cast<int>(Item::Count);

  int8_t selected = 0;
  bool visible = false;
  bool needsRender = true;
  bool hasFrontLight = false;
  bool hasTouch = false;
  bool showBack = true;
  uint8_t brightness = 0;
  uint8_t warmth = 50;

  void show() {
    visible = true;
    selected = 0;
    needsRender = true;
  }

  void hide() {
    visible = false;
    needsRender = true;
  }

  void moveUp() {
    selected = (selected == 0) ? itemCount() - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    selected = (selected + 1) % itemCount();
    needsRender = true;
  }

  Item selectedItem() const { return static_cast<Item>(selected); }
  int itemCount() const { return showBack ? ITEM_COUNT : static_cast<int>(Item::Back); }

  struct Layout {
    touch::Rect menu;
    touch::Rect items[ITEM_COUNT];
    touch::Rect brightnessLabel;
    touch::Rect brightnessMinus;
    touch::Rect brightnessBar;
    touch::Rect brightnessPlus;
    touch::Rect warmthLabel;
    touch::Rect warmthMinus;
    touch::Rect warmthBar;
    touch::Rect warmthPlus;
  };

  Layout layout(int16_t screenWidth, int16_t screenHeight, int16_t itemHeight) const {
    const bool enhanced = hasFrontLight || hasTouch;
    const int rowHeight = enhanced ? std::clamp<int>(itemHeight, 30, 40) : itemHeight;
    Layout result{};
    int itemsOffset = 45;
    if (!enhanced) {
      const int16_t height = static_cast<int16_t>(itemCount() * (rowHeight + 8) + 50);
      result.menu = {static_cast<int16_t>((screenWidth - 200) / 2), static_cast<int16_t>((screenHeight - height) / 2),
                     200, height};
    } else {
      const int menuWidth = hasFrontLight ? std::min(360, std::max(0, static_cast<int>(screenWidth) - 16)) : 200;
      const int labelHeight = std::max(rowHeight, 30);
      const int barHeight = 38;
      const int itemsHeight = itemCount() * (rowHeight + 8);
      int menuHeight = 45 + itemsHeight + 10;
      if (hasFrontLight) menuHeight += labelHeight + barHeight + 6 + labelHeight + barHeight + 12;
      result.menu = {static_cast<int16_t>((screenWidth - menuWidth) / 2),
                     static_cast<int16_t>((screenHeight - menuHeight) / 2), static_cast<int16_t>(menuWidth),
                     static_cast<int16_t>(menuHeight)};
      const int innerX = result.menu.x + 10;
      const int innerWidth = menuWidth - 20;
      int y = result.menu.y + 45;
      if (hasFrontLight) {
        result.brightnessLabel = {static_cast<int16_t>(innerX), static_cast<int16_t>(y),
                                  static_cast<int16_t>(innerWidth), static_cast<int16_t>(labelHeight)};
        y += labelHeight;
        result.brightnessMinus = {static_cast<int16_t>(innerX), static_cast<int16_t>(y), 40,
                                  static_cast<int16_t>(barHeight)};
        result.brightnessBar = {static_cast<int16_t>(innerX + 48), static_cast<int16_t>(y),
                                static_cast<int16_t>(innerWidth - 96), static_cast<int16_t>(barHeight)};
        result.brightnessPlus = {static_cast<int16_t>(result.menu.x + menuWidth - 50), static_cast<int16_t>(y), 40,
                                 static_cast<int16_t>(barHeight)};
        y += barHeight + 6;
        result.warmthLabel = {static_cast<int16_t>(innerX), static_cast<int16_t>(y), static_cast<int16_t>(innerWidth),
                              static_cast<int16_t>(labelHeight)};
        y += labelHeight;
        result.warmthMinus = {static_cast<int16_t>(innerX), static_cast<int16_t>(y), 40,
                              static_cast<int16_t>(barHeight)};
        result.warmthBar = {static_cast<int16_t>(innerX + 48), static_cast<int16_t>(y),
                            static_cast<int16_t>(innerWidth - 96), static_cast<int16_t>(barHeight)};
        result.warmthPlus = {static_cast<int16_t>(result.menu.x + menuWidth - 50), static_cast<int16_t>(y), 40,
                             static_cast<int16_t>(barHeight)};
        y += barHeight;
        itemsOffset = y - result.menu.y + 12;
      }
    }
    for (int i = 0; i < itemCount(); ++i) {
      result.items[i] = {static_cast<int16_t>(result.menu.x + 10),
                         static_cast<int16_t>(result.menu.y + itemsOffset + i * (rowHeight + 8)),
                         static_cast<int16_t>(result.menu.width - 20), static_cast<int16_t>(rowHeight)};
    }
    return result;
  }

  touch::Rect menuBounds(int16_t screenWidth, int16_t screenHeight, int16_t itemHeight) const {
    return layout(screenWidth, screenHeight, itemHeight).menu;
  }

  touch::Rect itemBounds(int index, int16_t screenWidth, int16_t screenHeight, int16_t itemHeight) const {
    if (index < 0 || index >= itemCount()) return {};
    return layout(screenWidth, screenHeight, itemHeight).items[index];
  }

  Hit hitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight, int16_t itemHeight) const {
    if (!visible) return {};
    const Layout geometry = layout(screenWidth, screenHeight, itemHeight);
    if (hasFrontLight) {
      if (geometry.brightnessMinus.contains(point)) return {Hit::Type::Brightness, -1, stepped(brightness, -5)};
      if (geometry.brightnessPlus.contains(point)) return {Hit::Type::Brightness, -1, stepped(brightness, 5)};
      if (geometry.brightnessBar.contains(point))
        return {Hit::Type::Brightness, -1, barValue(point, geometry.brightnessBar)};
      if (geometry.warmthMinus.contains(point)) return {Hit::Type::Warmth, -1, stepped(warmth, -5)};
      if (geometry.warmthPlus.contains(point)) return {Hit::Type::Warmth, -1, stepped(warmth, 5)};
      if (geometry.warmthBar.contains(point)) return {Hit::Type::Warmth, -1, barValue(point, geometry.warmthBar)};
    }
    for (int i = 0; i < itemCount(); ++i) {
      if (geometry.items[i].contains(point)) return {Hit::Type::Item, i};
    }
    if (hasTouch && !geometry.menu.contains(point)) return {Hit::Type::Dismiss, -1, 0};
    return {};
  }

 private:
  static int stepped(uint8_t current, int delta) { return std::clamp(static_cast<int>(current) + delta, 0, 100); }
  static int barValue(touch::Point point, touch::Rect bar) {
    const int span = bar.width - 1;
    if (span <= 0) return 0;
    return std::clamp(((point.x - bar.x) * 100 + span / 2) / span, 0, 100);
  }
};

void render(const GfxRenderer& r, const Theme& t, const ReaderMenuView& v);

// ============================================================================
// BookmarkListView - Bookmark list navigation
// ============================================================================

struct TocHit {
  enum class Type : uint8_t { None, Item, Back, Go, PageUp, PageDown };
  Type type = Type::None;
  int index = -1;
};

inline TocHit tocHitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight, int16_t rowHeight,
                         int scrollOffset, int visibleCount, int itemCount, bool frontLrbc = false) {
  const int action = touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc);
  if (action >= 0) {
    constexpr TocHit::Type actions[] = {TocHit::Type::Back, TocHit::Type::Go, TocHit::Type::PageUp,
                                        TocHit::Type::PageDown};
    return {actions[action], -1};
  }
  const int remaining = std::max(0, itemCount - scrollOffset);
  const int rows = std::min(visibleCount, remaining);
  const int row = touch::rowAt(point, {0, 60, screenWidth, static_cast<int16_t>(screenHeight - 130)}, rowHeight, rows);
  return row < 0 ? TocHit{} : TocHit{TocHit::Type::Item, scrollOffset + row};
}

struct BookmarkListView {
  struct Hit {
    enum class Type : uint8_t { None, Item, Back, Go, Add, Delete };
    Type type = Type::None;
    int index = -1;
  };
  ButtonBar buttons;
  int16_t itemCount = 0;
  int16_t selected = 0;
  int16_t scrollOffset = 0;

  void clear() {
    itemCount = 0;
    selected = 0;
    scrollOffset = 0;
  }

  void setItemCount(int16_t count) {
    itemCount = count;
    if (itemCount == 0) {
      selected = 0;
      scrollOffset = 0;
      return;
    }
    if (selected >= itemCount) selected = itemCount - 1;
    if (scrollOffset > selected) scrollOffset = selected;
  }

  void moveUp() {
    if (itemCount == 0) return;
    selected = (selected == 0) ? itemCount - 1 : selected - 1;
  }

  void moveDown() {
    if (itemCount == 0) return;
    selected = (selected + 1) % itemCount;
  }

  void ensureVisible(int visibleCount) {
    if (itemCount == 0 || visibleCount <= 0) return;
    if (selected < scrollOffset) {
      scrollOffset = selected;
    } else if (selected >= scrollOffset + visibleCount) {
      scrollOffset = selected - visibleCount + 1;
    }
  }
  Hit hitTest(touch::Point point, int16_t screenWidth, int16_t screenHeight, int16_t rowHeight, int visibleCount,
              bool frontLrbc = false) const {
    const int action = touch::semanticButtonBarIndex(point, screenWidth, screenHeight, frontLrbc);
    if (action >= 0) {
      constexpr Hit::Type actions[] = {Hit::Type::Back, Hit::Type::Go, Hit::Type::Add, Hit::Type::Delete};
      return {actions[action], -1};
    }
    const int remaining = std::max(0, static_cast<int>(itemCount - scrollOffset));
    const int rows = std::min(visibleCount, remaining);
    const int row =
        touch::rowAt(point, {0, 60, screenWidth, static_cast<int16_t>(screenHeight - 130)}, rowHeight, rows);
    return row < 0 ? Hit{} : Hit{Hit::Type::Item, scrollOffset + row};
  }
};

}  // namespace ui
