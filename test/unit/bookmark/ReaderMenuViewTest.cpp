#include <cstdint>

#include "test_utils.h"
#include "ui/views/ReaderViews.h"

using ui::ReaderMenuView;

int main() {
  TestUtils::TestRunner runner("ReaderMenuViewTest");

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
    view.moveUp();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Bookmarks, "menu moves back to bookmarks");
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
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Back, "moveUp wraps to the exit action");
    runner.expectTrue(view.needsRender, "moveUp wrap sets needsRender");
    view.moveDown();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Chapters, "exit action wraps to Chapters");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
