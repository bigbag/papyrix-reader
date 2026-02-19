#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// HomeView - Main home screen with current book and direct action buttons
// ============================================================================

struct CardDimensions {
  int x, y, width, height;

  struct CoverArea {
    int x, y, width, height;
  };

  static CardDimensions calculate(int screenWidth, int screenHeight) {
    const int w = screenWidth * 7 / 10;  // 70% width for larger cover
    const int h = screenHeight / 2 + 100;
    const int x = (screenWidth - w) / 2;
    constexpr int y = 50;
    return {x, y, w, h};
  }

  CoverArea getCoverArea() const {
    constexpr int padding = 10;
    return {x + padding, y + padding, width - 2 * padding, height - 2 * padding};
  }
};

struct HomeView {
  static constexpr int MAX_TITLE_LEN = 64;
  static constexpr int MAX_AUTHOR_LEN = 48;
  static constexpr int MAX_PATH_LEN = 128;

  ButtonBar buttons{"", "Files", "Sync", "Settings"};

  // Current book info
  char bookTitle[MAX_TITLE_LEN] = {0};
  char bookAuthor[MAX_AUTHOR_LEN] = {0};
  char bookPath[MAX_PATH_LEN] = {0};
  bool hasBook = false;

  // Cover image (external pointer - not owned)
  const uint8_t* coverData = nullptr;
  int16_t coverWidth = 0;
  int16_t coverHeight = 0;

  // Cover from BMP file (rendered by HomeState after ui::render)
  bool hasCoverBmp = false;

  // Font override for title/author (resolved by state, -1 = use theme default)
  int titleFontId = -1;

  // UI state
  int8_t batteryPercent = 100;
  bool needsRender = true;

  void setBook(const char* title, const char* author, const char* path) {
    strncpy(bookTitle, title, MAX_TITLE_LEN - 1);
    bookTitle[MAX_TITLE_LEN - 1] = '\0';
    strncpy(bookAuthor, author, MAX_AUTHOR_LEN - 1);
    bookAuthor[MAX_AUTHOR_LEN - 1] = '\0';
    strncpy(bookPath, path, MAX_PATH_LEN - 1);
    bookPath[MAX_PATH_LEN - 1] = '\0';
    hasBook = true;
    buttons.labels[0] = "Read";
    needsRender = true;
  }

  void clearBook() {
    bookTitle[0] = '\0';
    bookAuthor[0] = '\0';
    bookPath[0] = '\0';
    hasBook = false;
    coverData = nullptr;
    coverWidth = 0;
    coverHeight = 0;
    hasCoverBmp = false;
    buttons.labels[0] = "";
    needsRender = true;
  }

  void setCover(const uint8_t* data, int w, int h) {
    coverData = data;
    coverWidth = static_cast<int16_t>(w);
    coverHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setBattery(int percent) {
    if (batteryPercent != percent) {
      batteryPercent = static_cast<int8_t>(percent);
      needsRender = true;
    }
  }

  void clear() {
    clearBook();
    batteryPercent = 100;
  }
};

void render(const GfxRenderer& r, const Theme& t, const HomeView& v);

// ============================================================================
// FileListView - Paginated file browser
// ============================================================================

struct FileListView {
  static constexpr int MAX_FILES = 64;
  static constexpr int NAME_LEN = 48;
  static constexpr int PATH_LEN = 128;
  static constexpr int PAGE_SIZE = 12;

  // File entry structure (packed for memory efficiency)
  struct FileEntry {
    char name[NAME_LEN];
    bool isDirectory;
  };

  ButtonBar buttons{"Back", "Open", "", ""};

  // Path and file list
  char currentPath[PATH_LEN] = "/";
  FileEntry files[MAX_FILES];
  uint8_t fileCount = 0;
  uint8_t page = 0;
  uint8_t selected = 0;
  bool needsRender = true;

  void clear() {
    fileCount = 0;
    page = 0;
    selected = 0;
    needsRender = true;
  }

  bool addFile(const char* name, bool isDir) {
    if (fileCount < MAX_FILES) {
      strncpy(files[fileCount].name, name, NAME_LEN - 1);
      files[fileCount].name[NAME_LEN - 1] = '\0';
      files[fileCount].isDirectory = isDir;
      fileCount++;
      return true;
    }
    return false;
  }

  void setPath(const char* path) {
    strncpy(currentPath, path, PATH_LEN - 1);
    currentPath[PATH_LEN - 1] = '\0';
    needsRender = true;
  }

  int getPageCount() const { return (fileCount + PAGE_SIZE - 1) / PAGE_SIZE; }

  int getPageStart() const { return page * PAGE_SIZE; }

  int getPageEnd() const {
    int end = (page + 1) * PAGE_SIZE;
    return end > fileCount ? fileCount : end;
  }

  void moveUp() {
    if (selected > 0) {
      selected--;
      // Update page if needed
      if (selected < getPageStart()) {
        page--;
      }
      needsRender = true;
    }
  }

  void moveDown() {
    if (selected < fileCount - 1) {
      selected++;
      // Update page if needed
      if (selected >= getPageEnd()) {
        page++;
      }
      needsRender = true;
    }
  }

  void pageUp() {
    if (page > 0) {
      page--;
      selected = page * PAGE_SIZE;
      needsRender = true;
    }
  }

  void pageDown() {
    if (page < getPageCount() - 1) {
      page++;
      selected = page * PAGE_SIZE;
      needsRender = true;
    }
  }

  const FileEntry* getSelectedFile() const {
    if (selected < fileCount) {
      return &files[selected];
    }
    return nullptr;
  }
};

void render(const GfxRenderer& r, const Theme& t, const FileListView& v);

// ============================================================================
// ChapterListView - Chapter/TOC selection for readers
// ============================================================================

struct ChapterListView {
  static constexpr int MAX_CHAPTERS = 256;
  static constexpr int TITLE_LEN = 64;

  struct Chapter {
    char title[TITLE_LEN];
    uint16_t pageNum;
    uint8_t depth;  // Nesting level (0 = root)
  };

  ButtonBar buttons{"Back", "Go", "", ""};
  Chapter chapters[MAX_CHAPTERS];
  uint16_t chapterCount = 0;
  uint16_t currentChapter = 0;  // The chapter user is currently reading
  uint16_t selected = 0;
  uint16_t scrollOffset = 0;  // First visible item
  bool needsRender = true;

  void clear() {
    chapterCount = 0;
    selected = 0;
    scrollOffset = 0;
    needsRender = true;
  }

  bool addChapter(const char* title, uint16_t pageNum, uint8_t depth = 0) {
    if (chapterCount < MAX_CHAPTERS) {
      strncpy(chapters[chapterCount].title, title, TITLE_LEN - 1);
      chapters[chapterCount].title[TITLE_LEN - 1] = '\0';
      chapters[chapterCount].pageNum = pageNum;
      chapters[chapterCount].depth = depth;
      chapterCount++;
      return true;
    }
    return false;
  }

  void setCurrentChapter(uint16_t idx) {
    currentChapter = idx;
    selected = idx;
    scrollOffset = idx;  // Start with current chapter at top
    needsRender = true;
  }

  void moveUp() {
    if (chapterCount == 0) return;
    selected = (selected == 0) ? chapterCount - 1 : selected - 1;
    needsRender = true;
  }

  void moveDown() {
    if (chapterCount == 0) return;
    selected = (selected + 1) % chapterCount;
    needsRender = true;
  }

  void movePageUp(int count) {
    if (chapterCount == 0 || count <= 0) return;
    selected = (selected >= count) ? selected - count : 0;
    needsRender = true;
  }

  void movePageDown(int count) {
    if (chapterCount == 0 || count <= 0) return;
    int target = selected + count;
    selected = (target < chapterCount) ? static_cast<uint16_t>(target) : chapterCount - 1;
    needsRender = true;
  }

  // Adjust scroll to keep selected visible (call before rendering)
  void ensureVisible(int visibleCount) {
    if (chapterCount == 0 || visibleCount <= 0) return;
    const int sel = selected;
    const int off = scrollOffset;
    if (sel < off) {
      scrollOffset = static_cast<uint16_t>(sel);
    } else if (sel >= off + visibleCount) {
      scrollOffset = static_cast<uint16_t>(sel - visibleCount + 1);
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, ChapterListView& v);

}  // namespace ui
