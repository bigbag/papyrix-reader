#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

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
