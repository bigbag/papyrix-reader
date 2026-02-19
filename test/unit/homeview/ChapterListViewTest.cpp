#include "test_utils.h"

#include <cstdint>
#include <cstring>

// Inline ChapterListView to avoid firmware/graphics dependencies
struct ChapterListView {
  static constexpr int MAX_CHAPTERS = 256;
  static constexpr int TITLE_LEN = 64;

  struct Chapter {
    char title[TITLE_LEN];
    uint16_t pageNum;
    uint8_t depth;
  };

  Chapter chapters[MAX_CHAPTERS];
  uint16_t chapterCount = 0;
  uint16_t currentChapter = 0;
  uint16_t selected = 0;
  uint16_t scrollOffset = 0;
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
    scrollOffset = idx;
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

int main() {
  TestUtils::TestRunner runner("ChapterListViewTest");

  // --- addChapter basic ---
  {
    ChapterListView view;
    bool added = view.addChapter("Introduction", 1, 0);
    runner.expectTrue(added, "addChapter returns true");
    runner.expectEq(uint16_t(1), view.chapterCount, "chapterCount incremented");
    runner.expectTrue(strcmp(view.chapters[0].title, "Introduction") == 0, "title stored correctly");
    runner.expectEq(uint16_t(1), view.chapters[0].pageNum, "pageNum stored correctly");
    runner.expectEq(uint8_t(0), view.chapters[0].depth, "depth stored correctly");
  }

  // --- addChapter with depth ---
  {
    ChapterListView view;
    view.addChapter("Part 1", 0, 0);
    view.addChapter("Chapter 1", 1, 1);
    view.addChapter("Section 1.1", 5, 2);
    runner.expectEq(uint16_t(3), view.chapterCount, "3 chapters added");
    runner.expectEq(uint8_t(1), view.chapters[1].depth, "depth=1 stored");
    runner.expectEq(uint8_t(2), view.chapters[2].depth, "depth=2 stored");
  }

  // --- addChapter overflow ---
  {
    ChapterListView view;
    for (int i = 0; i < ChapterListView::MAX_CHAPTERS; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      runner.expectTrue(view.addChapter(title, static_cast<uint16_t>(i)), "addChapter succeeds up to MAX");
    }
    runner.expectEq(uint16_t(ChapterListView::MAX_CHAPTERS), view.chapterCount, "chapterCount at MAX");
    runner.expectFalse(view.addChapter("Overflow", 99), "addChapter fails when full");
    runner.expectEq(uint16_t(ChapterListView::MAX_CHAPTERS), view.chapterCount, "chapterCount unchanged");
  }

  // --- addChapter title truncation ---
  {
    ChapterListView view;
    const char* longTitle =
        "This is a very long chapter title that exceeds the maximum allowed length for storage";
    view.addChapter(longTitle, 0);
    runner.expectEq(size_t(ChapterListView::TITLE_LEN - 1), strlen(view.chapters[0].title),
                    "long title truncated to TITLE_LEN-1");
  }

  // --- setCurrentChapter ---
  {
    ChapterListView view;
    for (int i = 0; i < 10; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.needsRender = false;
    view.setCurrentChapter(5);
    runner.expectEq(uint16_t(5), view.currentChapter, "currentChapter set");
    runner.expectEq(uint16_t(5), view.selected, "selected set to currentChapter");
    runner.expectEq(uint16_t(5), view.scrollOffset, "scrollOffset set to currentChapter");
    runner.expectTrue(view.needsRender, "setCurrentChapter sets needsRender");
  }

  // --- clear ---
  {
    ChapterListView view;
    view.addChapter("Ch1", 1);
    view.addChapter("Ch2", 2);
    view.selected = 1;
    view.scrollOffset = 1;
    view.needsRender = false;
    view.clear();
    runner.expectEq(uint16_t(0), view.chapterCount, "clear resets chapterCount");
    runner.expectEq(uint16_t(0), view.selected, "clear resets selected");
    runner.expectEq(uint16_t(0), view.scrollOffset, "clear resets scrollOffset");
    runner.expectTrue(view.needsRender, "clear sets needsRender");
  }

  // --- moveUp/moveDown on empty list ---
  {
    ChapterListView view;
    view.needsRender = false;
    view.moveDown();
    runner.expectEq(uint16_t(0), view.selected, "moveDown on empty is no-op");
    runner.expectFalse(view.needsRender, "moveDown on empty doesn't set needsRender");

    view.moveUp();
    runner.expectEq(uint16_t(0), view.selected, "moveUp on empty is no-op");
    runner.expectFalse(view.needsRender, "moveUp on empty doesn't set needsRender");
  }

  // --- moveUp/moveDown wrapping ---
  {
    ChapterListView view;
    view.addChapter("Ch0", 0);
    view.addChapter("Ch1", 1);
    view.addChapter("Ch2", 2);

    runner.expectEq(uint16_t(0), view.selected, "initial selected is 0");

    view.needsRender = false;
    view.moveDown();
    runner.expectEq(uint16_t(1), view.selected, "moveDown increments");
    runner.expectTrue(view.needsRender, "moveDown sets needsRender");

    view.moveDown();
    runner.expectEq(uint16_t(2), view.selected, "moveDown to last");

    view.moveDown();
    runner.expectEq(uint16_t(0), view.selected, "moveDown wraps to 0");

    view.moveUp();
    runner.expectEq(uint16_t(2), view.selected, "moveUp wraps to last");

    view.moveUp();
    runner.expectEq(uint16_t(1), view.selected, "moveUp decrements");

    view.moveUp();
    runner.expectEq(uint16_t(0), view.selected, "moveUp to first");
  }

  // --- movePageUp on empty list ---
  {
    ChapterListView view;
    view.needsRender = false;
    view.movePageUp(5);
    runner.expectEq(uint16_t(0), view.selected, "movePageUp on empty is no-op");
    runner.expectFalse(view.needsRender, "movePageUp on empty doesn't set needsRender");
  }

  // --- movePageDown on empty list ---
  {
    ChapterListView view;
    view.needsRender = false;
    view.movePageDown(5);
    runner.expectEq(uint16_t(0), view.selected, "movePageDown on empty is no-op");
    runner.expectFalse(view.needsRender, "movePageDown on empty doesn't set needsRender");
  }

  // --- movePageUp/movePageDown with count <= 0 ---
  {
    ChapterListView view;
    view.addChapter("Ch0", 0);
    view.addChapter("Ch1", 1);
    view.selected = 1;
    view.needsRender = false;

    view.movePageUp(0);
    runner.expectEq(uint16_t(1), view.selected, "movePageUp(0) is no-op");
    runner.expectFalse(view.needsRender, "movePageUp(0) doesn't set needsRender");

    view.movePageDown(0);
    runner.expectEq(uint16_t(1), view.selected, "movePageDown(0) is no-op");
    runner.expectFalse(view.needsRender, "movePageDown(0) doesn't set needsRender");

    view.movePageUp(-1);
    runner.expectEq(uint16_t(1), view.selected, "movePageUp(-1) is no-op");

    view.movePageDown(-1);
    runner.expectEq(uint16_t(1), view.selected, "movePageDown(-1) is no-op");
  }

  // --- movePageDown basic ---
  {
    ChapterListView view;
    for (int i = 0; i < 20; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }

    view.needsRender = false;
    view.movePageDown(5);
    runner.expectEq(uint16_t(5), view.selected, "movePageDown(5) from 0 -> 5");
    runner.expectTrue(view.needsRender, "movePageDown sets needsRender");

    view.movePageDown(5);
    runner.expectEq(uint16_t(10), view.selected, "movePageDown(5) from 5 -> 10");

    view.movePageDown(5);
    runner.expectEq(uint16_t(15), view.selected, "movePageDown(5) from 10 -> 15");

    view.movePageDown(5);
    runner.expectEq(uint16_t(19), view.selected, "movePageDown clamps to last");

    view.movePageDown(5);
    runner.expectEq(uint16_t(19), view.selected, "movePageDown at last stays at last");
  }

  // --- movePageUp basic ---
  {
    ChapterListView view;
    for (int i = 0; i < 20; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.selected = 19;

    view.needsRender = false;
    view.movePageUp(5);
    runner.expectEq(uint16_t(14), view.selected, "movePageUp(5) from 19 -> 14");
    runner.expectTrue(view.needsRender, "movePageUp sets needsRender");

    view.movePageUp(5);
    runner.expectEq(uint16_t(9), view.selected, "movePageUp(5) from 14 -> 9");

    view.movePageUp(5);
    runner.expectEq(uint16_t(4), view.selected, "movePageUp(5) from 9 -> 4");

    view.movePageUp(5);
    runner.expectEq(uint16_t(0), view.selected, "movePageUp clamps to 0");

    view.movePageUp(5);
    runner.expectEq(uint16_t(0), view.selected, "movePageUp at 0 stays at 0");
  }

  // --- movePageDown clamps exactly at last ---
  {
    ChapterListView view;
    for (int i = 0; i < 7; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.selected = 4;
    view.movePageDown(5);
    runner.expectEq(uint16_t(6), view.selected, "movePageDown clamps when partial page remains");
  }

  // --- movePageUp clamps exactly at first ---
  {
    ChapterListView view;
    for (int i = 0; i < 7; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.selected = 2;
    view.movePageUp(5);
    runner.expectEq(uint16_t(0), view.selected, "movePageUp clamps when near start");
  }

  // --- movePageDown with count=1 (same as moveDown but without wrap) ---
  {
    ChapterListView view;
    view.addChapter("Ch0", 0);
    view.addChapter("Ch1", 1);
    view.addChapter("Ch2", 2);

    view.movePageDown(1);
    runner.expectEq(uint16_t(1), view.selected, "movePageDown(1) moves by 1");
    view.movePageDown(1);
    runner.expectEq(uint16_t(2), view.selected, "movePageDown(1) to last");
    view.movePageDown(1);
    runner.expectEq(uint16_t(2), view.selected, "movePageDown(1) clamps at last (no wrap)");
  }

  // --- movePageUp with count=1 ---
  {
    ChapterListView view;
    view.addChapter("Ch0", 0);
    view.addChapter("Ch1", 1);
    view.addChapter("Ch2", 2);
    view.selected = 2;

    view.movePageUp(1);
    runner.expectEq(uint16_t(1), view.selected, "movePageUp(1) moves by 1");
    view.movePageUp(1);
    runner.expectEq(uint16_t(0), view.selected, "movePageUp(1) to first");
    view.movePageUp(1);
    runner.expectEq(uint16_t(0), view.selected, "movePageUp(1) clamps at first (no wrap)");
  }

  // --- ensureVisible: selected below visible range ---
  {
    ChapterListView view;
    for (int i = 0; i < 20; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.scrollOffset = 0;
    view.selected = 12;
    view.ensureVisible(5);
    runner.expectEq(uint16_t(8), view.scrollOffset, "ensureVisible scrolls down: offset = selected - visible + 1");
  }

  // --- ensureVisible: selected above visible range ---
  {
    ChapterListView view;
    for (int i = 0; i < 20; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.scrollOffset = 10;
    view.selected = 5;
    view.ensureVisible(5);
    runner.expectEq(uint16_t(5), view.scrollOffset, "ensureVisible scrolls up: offset = selected");
  }

  // --- ensureVisible: selected within visible range ---
  {
    ChapterListView view;
    for (int i = 0; i < 20; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.scrollOffset = 5;
    view.selected = 7;
    view.ensureVisible(5);
    runner.expectEq(uint16_t(5), view.scrollOffset, "ensureVisible no change when visible");
  }

  // --- ensureVisible with invalid inputs ---
  {
    ChapterListView view;
    view.scrollOffset = 3;
    view.ensureVisible(5);
    runner.expectEq(uint16_t(3), view.scrollOffset, "ensureVisible on empty list is no-op");

    view.addChapter("Ch0", 0);
    view.scrollOffset = 0;
    view.ensureVisible(0);
    runner.expectEq(uint16_t(0), view.scrollOffset, "ensureVisible with visibleCount=0 is no-op");

    view.ensureVisible(-1);
    runner.expectEq(uint16_t(0), view.scrollOffset, "ensureVisible with negative visibleCount is no-op");
  }

  // --- movePageDown + ensureVisible integration ---
  {
    ChapterListView view;
    for (int i = 0; i < 30; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Ch%d", i);
      view.addChapter(title, static_cast<uint16_t>(i));
    }
    view.scrollOffset = 0;
    const int visibleCount = 8;

    view.movePageDown(visibleCount);
    view.ensureVisible(visibleCount);
    runner.expectEq(uint16_t(8), view.selected, "page down selects item 8");
    runner.expectEq(uint16_t(1), view.scrollOffset, "ensureVisible adjusts scroll after page down");

    view.movePageDown(visibleCount);
    view.ensureVisible(visibleCount);
    runner.expectEq(uint16_t(16), view.selected, "second page down selects item 16");
    runner.expectEq(uint16_t(9), view.scrollOffset, "scroll adjusted for second page down");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
