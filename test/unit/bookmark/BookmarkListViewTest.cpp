#include "test_utils.h"

#include <cstdint>
#include <cstring>

// Inline ButtonBar to avoid pulling in GfxRenderer/Theme dependencies
struct ButtonBar {
  const char* labels[4] = {"", "", "", ""};

  explicit constexpr ButtonBar(const char* b1 = "", const char* b2 = "", const char* b3 = "", const char* b4 = "")
      : labels{b1 ? b1 : "", b2 ? b2 : "", b3 ? b3 : "", b4 ? b4 : ""} {}

  bool isActive(int idx) const { return idx >= 0 && idx < 4 && labels[idx] && labels[idx][0] != '\0'; }
};

// Inline BookmarkListView to avoid firmware/graphics dependencies
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

int main() {
  TestUtils::TestRunner runner("BookmarkListViewTest");

  // --- addItem basic ---
  {
    BookmarkListView view;
    bool added = view.addItem("Page 5: Introduction");
    runner.expectTrue(added, "addItem returns true");
    runner.expectEq(int16_t(1), view.itemCount, "itemCount incremented");
    runner.expectTrue(strcmp(view.items[0].title, "Page 5: Introduction") == 0, "title stored correctly");
    runner.expectEq(uint8_t(0), view.items[0].depth, "default depth is 0");
  }

  // --- addItem with depth ---
  {
    BookmarkListView view;
    view.addItem("Bookmark 1", 0);
    view.addItem("Bookmark 2", 1);
    runner.expectEq(int16_t(2), view.itemCount, "2 items added");
    runner.expectEq(uint8_t(0), view.items[0].depth, "depth=0 stored");
    runner.expectEq(uint8_t(1), view.items[1].depth, "depth=1 stored");
  }

  // --- addItem overflow (MAX_ITEMS = 20) ---
  {
    BookmarkListView view;
    for (int i = 0; i < BookmarkListView::MAX_ITEMS; i++) {
      char title[32];
      snprintf(title, sizeof(title), "Bookmark %d", i);
      runner.expectTrue(view.addItem(title), "addItem succeeds up to MAX");
    }
    runner.expectEq(int16_t(BookmarkListView::MAX_ITEMS), view.itemCount, "itemCount at MAX");
    runner.expectFalse(view.addItem("Overflow"), "addItem fails when full");
    runner.expectEq(int16_t(BookmarkListView::MAX_ITEMS), view.itemCount, "itemCount unchanged after overflow");
  }

  // --- addItem title truncation ---
  {
    BookmarkListView view;
    const char* longTitle =
        "This is a very long bookmark title that exceeds the maximum allowed length for storage in the view";
    view.addItem(longTitle);
    runner.expectEq(size_t(BookmarkListView::TITLE_LEN - 1), strlen(view.items[0].title),
                    "long title truncated to TITLE_LEN-1");
  }

  // --- clear ---
  {
    BookmarkListView view;
    view.addItem("Bm1");
    view.addItem("Bm2");
    view.selected = 1;
    view.scrollOffset = 1;
    view.clear();
    runner.expectEq(int16_t(0), view.itemCount, "clear resets itemCount");
    runner.expectEq(int16_t(0), view.selected, "clear resets selected");
    runner.expectEq(int16_t(0), view.scrollOffset, "clear resets scrollOffset");
  }

  // --- moveUp/moveDown on empty list ---
  {
    BookmarkListView view;
    view.moveDown();
    runner.expectEq(int16_t(0), view.selected, "moveDown on empty is no-op");
    view.moveUp();
    runner.expectEq(int16_t(0), view.selected, "moveUp on empty is no-op");
  }

  // --- moveUp/moveDown wrapping ---
  {
    BookmarkListView view;
    view.addItem("Bm0");
    view.addItem("Bm1");
    view.addItem("Bm2");

    runner.expectEq(int16_t(0), view.selected, "initial selected is 0");

    view.moveDown();
    runner.expectEq(int16_t(1), view.selected, "moveDown increments");

    view.moveDown();
    runner.expectEq(int16_t(2), view.selected, "moveDown to last");

    view.moveDown();
    runner.expectEq(int16_t(0), view.selected, "moveDown wraps to 0");

    view.moveUp();
    runner.expectEq(int16_t(2), view.selected, "moveUp wraps to last");

    view.moveUp();
    runner.expectEq(int16_t(1), view.selected, "moveUp decrements");
  }

  // --- single item wrapping ---
  {
    BookmarkListView view;
    view.addItem("Only");

    view.moveDown();
    runner.expectEq(int16_t(0), view.selected, "moveDown single item wraps to 0");
    view.moveUp();
    runner.expectEq(int16_t(0), view.selected, "moveUp single item wraps to 0");
  }

  // --- ensureVisible: selected below visible range ---
  {
    BookmarkListView view;
    for (int i = 0; i < 15; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Bm%d", i);
      view.addItem(title);
    }
    view.scrollOffset = 0;
    view.selected = 10;
    view.ensureVisible(5);
    runner.expectEq(int16_t(6), view.scrollOffset, "ensureVisible scrolls down: offset = selected - visible + 1");
  }

  // --- ensureVisible: selected above visible range ---
  {
    BookmarkListView view;
    for (int i = 0; i < 15; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Bm%d", i);
      view.addItem(title);
    }
    view.scrollOffset = 8;
    view.selected = 3;
    view.ensureVisible(5);
    runner.expectEq(int16_t(3), view.scrollOffset, "ensureVisible scrolls up: offset = selected");
  }

  // --- ensureVisible: selected within visible range ---
  {
    BookmarkListView view;
    for (int i = 0; i < 15; i++) {
      char title[16];
      snprintf(title, sizeof(title), "Bm%d", i);
      view.addItem(title);
    }
    view.scrollOffset = 5;
    view.selected = 7;
    view.ensureVisible(5);
    runner.expectEq(int16_t(5), view.scrollOffset, "ensureVisible no change when visible");
  }

  // --- ensureVisible with invalid inputs ---
  {
    BookmarkListView view;
    view.scrollOffset = 3;
    view.ensureVisible(5);
    runner.expectEq(int16_t(3), view.scrollOffset, "ensureVisible on empty list is no-op");

    view.addItem("Bm0");
    view.scrollOffset = 0;
    view.ensureVisible(0);
    runner.expectEq(int16_t(0), view.scrollOffset, "ensureVisible with visibleCount=0 is no-op");

    view.ensureVisible(-1);
    runner.expectEq(int16_t(0), view.scrollOffset, "ensureVisible with negative visibleCount is no-op");
  }

  // --- ButtonBar defaults ---
  {
    BookmarkListView view;
    runner.expectTrue(view.buttons.isActive(0), "button 0 (Back) is active");
    runner.expectTrue(view.buttons.isActive(1), "button 1 (Go) is active");
    runner.expectFalse(view.buttons.isActive(2), "button 2 is inactive");
    runner.expectFalse(view.buttons.isActive(3), "button 3 is inactive");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
