#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

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
// ReaderMenuView - In-reader quick menu overlay
// ============================================================================

struct ReaderMenuView {
  static constexpr const char* const ITEMS[] = {"Chapters", "Bookmarks"};
  static constexpr int ITEM_COUNT = 2;

  int8_t selected = 0;
  bool visible = false;
  bool needsRender = true;

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
    if (selected > 0) {
      selected--;
      needsRender = true;
    }
  }

  void moveDown() {
    if (selected < ITEM_COUNT - 1) {
      selected++;
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const ReaderMenuView& v);

// ============================================================================
// BookmarkListView - Compact bookmark list (20 items max vs 256 for ChapterListView)
// ============================================================================

struct BookmarkListView {
  static constexpr int MAX_ITEMS = 20;
  static constexpr int TITLE_LEN = 64;

  struct Item {
    char title[TITLE_LEN];
    uint8_t depth;
  };

  ButtonBar buttons{"Back", "Go", "", ""};
  Item items[MAX_ITEMS];
  int16_t itemCount = 0;
  int16_t selected = 0;
  int16_t scrollOffset = 0;

  void clear() {
    itemCount = 0;
    selected = 0;
    scrollOffset = 0;
  }

  bool addItem(const char* title, uint8_t depth = 0) {
    if (itemCount >= MAX_ITEMS) return false;
    strncpy(items[itemCount].title, title, TITLE_LEN - 1);
    items[itemCount].title[TITLE_LEN - 1] = '\0';
    items[itemCount].depth = depth;
    itemCount++;
    return true;
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
};

// ============================================================================
// JumpToPageView - Page number input for reader
// ============================================================================

struct JumpToPageView {
  ButtonBar buttons{"Cancel", "Go", "-10", "+10"};
  int16_t targetPage = 1;
  int16_t maxPage = 1;
  bool needsRender = true;

  void setMaxPage(int max) {
    maxPage = static_cast<int16_t>(max);
    if (targetPage > maxPage) {
      targetPage = maxPage;
    }
    needsRender = true;
  }

  void setPage(int page) {
    if (page >= 1 && page <= maxPage) {
      targetPage = static_cast<int16_t>(page);
      needsRender = true;
    }
  }

  void incrementPage(int delta) {
    int newPage = targetPage + delta;
    if (newPage < 1) newPage = 1;
    if (newPage > maxPage) newPage = maxPage;
    if (newPage != targetPage) {
      targetPage = static_cast<int16_t>(newPage);
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const JumpToPageView& v);

}  // namespace ui
