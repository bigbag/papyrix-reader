#include <cstdint>

#include "test_utils.h"
#include "ui/views/ReaderViews.h"

using ui::ReaderMenuView;

int main() {
  TestUtils::TestRunner runner("ReaderMenuViewTest");

  {
    ReaderMenuView view;
    view.selected = 1;
    view.show();
    runner.expectTrue(view.visible, "show makes menu visible");
    runner.expectEq(int8_t(0), view.selected, "show resets selected to 0");
    view.hide();
    runner.expectFalse(view.visible, "hide makes menu invisible");
  }

  {
    ReaderMenuView view;
    view.show();

    view.moveDown();
    runner.expectEq(int8_t(1), view.selected, "moveDown increments");

    view.moveDown();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::BookStats, "second moveDown reaches Book stats");

    view.moveUp();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Bookmarks, "menu moves back to bookmarks");

    view.moveUp();
    view.moveUp();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Back, "moveUp wraps to the exit action");
    view.moveDown();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Chapters, "exit action wraps to Chapters");
  }

  {
    ReaderMenuView view;
    view.showBack = false;
    view.show();
    view.moveUp();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::BookStats, "hidden Back is skipped when moving up");
    view.moveDown();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Chapters, "Book stats wraps to Chapters");
    view.moveDown();
    view.moveDown();
    view.moveDown();
    runner.expectTrue(view.selectedItem() == ReaderMenuView::Item::Chapters, "hidden Back is skipped when moving down");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
