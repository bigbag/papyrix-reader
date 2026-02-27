#include "test_utils.h"

#include <cstdint>
#include <cstring>

// Inline ReaderMenuView to avoid firmware/graphics dependencies
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

constexpr const char* const ReaderMenuView::ITEMS[];

int main() {
  TestUtils::TestRunner runner("ReaderMenuViewTest");

  // --- defaults ---
  {
    ReaderMenuView view;
    runner.expectEq(int8_t(0), view.selected, "default selected is 0");
    runner.expectFalse(view.visible, "default visible is false");
    runner.expectTrue(view.needsRender, "default needsRender is true");
  }

  // --- ITEM_COUNT matches ITEMS ---
  {
    runner.expectEq(2, ReaderMenuView::ITEM_COUNT, "ITEM_COUNT is 2");
    runner.expectTrue(strcmp(ReaderMenuView::ITEMS[0], "Chapters") == 0, "first item is Chapters");
    runner.expectTrue(strcmp(ReaderMenuView::ITEMS[1], "Bookmarks") == 0, "second item is Bookmarks");
  }

  // --- show ---
  {
    ReaderMenuView view;
    view.selected = 1;
    view.needsRender = false;
    view.show();
    runner.expectTrue(view.visible, "show sets visible");
    runner.expectEq(int8_t(0), view.selected, "show resets selected to 0");
    runner.expectTrue(view.needsRender, "show sets needsRender");
  }

  // --- hide ---
  {
    ReaderMenuView view;
    view.show();
    view.needsRender = false;
    view.hide();
    runner.expectFalse(view.visible, "hide clears visible");
    runner.expectTrue(view.needsRender, "hide sets needsRender");
  }

  // --- moveDown ---
  {
    ReaderMenuView view;
    view.show();
    view.needsRender = false;

    view.moveDown();
    runner.expectEq(int8_t(1), view.selected, "moveDown increments");
    runner.expectTrue(view.needsRender, "moveDown sets needsRender");

    view.needsRender = false;
    view.moveDown();
    runner.expectEq(int8_t(1), view.selected, "moveDown clamps at last item");
    runner.expectFalse(view.needsRender, "moveDown at end doesn't set needsRender");
  }

  // --- moveUp ---
  {
    ReaderMenuView view;
    view.show();
    view.selected = 1;
    view.needsRender = false;

    view.moveUp();
    runner.expectEq(int8_t(0), view.selected, "moveUp decrements");
    runner.expectTrue(view.needsRender, "moveUp sets needsRender");

    view.needsRender = false;
    view.moveUp();
    runner.expectEq(int8_t(0), view.selected, "moveUp clamps at first item");
    runner.expectFalse(view.needsRender, "moveUp at start doesn't set needsRender");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
