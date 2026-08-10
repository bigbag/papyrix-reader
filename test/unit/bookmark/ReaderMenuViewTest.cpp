#include <cstdint>

#include "test_utils.h"
#include "ui/views/ReaderViews.h"

using ui::ReaderMenuView;

int main() {
  TestUtils::TestRunner runner("ReaderMenuViewTest");

  // --- defaults ---
  {
    ReaderMenuView view;
    runner.expectEq(int8_t(0), view.selected, "default selected is 0");
    runner.expectFalse(view.visible, "default visible is false");
    runner.expectTrue(view.needsRender, "default needsRender is true");
  }

  // --- production item contract ---
  {
    runner.expectEq(3, ReaderMenuView::ITEM_COUNT, "ITEM_COUNT is 3");
    runner.expectEq(0, static_cast<int>(ReaderMenuView::Item::Chapters), "Chapters is first");
    runner.expectEq(1, static_cast<int>(ReaderMenuView::Item::Bookmarks), "Bookmarks is second");
    runner.expectEq(2, static_cast<int>(ReaderMenuView::Item::BookStats), "Book stats is third");
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
    runner.expectEq(int8_t(2), view.selected, "second moveDown reaches Book stats");
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::BookStats, "selectedItem returns production enum");
    runner.expectTrue(view.needsRender, "second moveDown sets needsRender");

    view.needsRender = false;
    view.moveDown();
    runner.expectEq(int8_t(2), view.selected, "moveDown clamps at last item");
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
