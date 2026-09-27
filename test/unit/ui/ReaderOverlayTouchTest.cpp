#include "test_utils.h"
#include "ui/views/ReaderViews.h"

using ui::touch::Point;

int main() {
  TestUtils::TestRunner r("Reader overlay touch");

  ui::ReaderMenuView menu;
  menu.show();
  auto menuHit = menu.hitTest({310, 210}, 800, 480, 32);
  r.expectTrue(menuHit.type == ui::ReaderMenuView::Hit::Type::Item && menuHit.index == 0,
               "reader menu first item");
  menuHit = menu.hitTest({310, 290}, 800, 480, 32);
  r.expectTrue(menuHit.type == ui::ReaderMenuView::Hit::Type::Item && menuHit.index == 2,
               "reader menu book statistics");
  menu.hide();
  r.expectTrue(menu.hitTest({310, 210}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "hidden reader menu rejects taps");

  auto toc = ui::tocHitTest({20, 100}, 800, 480, 40, 12, 4, 14);
  r.expectTrue(toc.type == ui::TocHit::Type::Item && toc.index == 13, "TOC visible row maps absolute index");
  toc = ui::tocHitTest({20, 180}, 800, 480, 40, 12, 4, 14);
  r.expectTrue(toc.type == ui::TocHit::Type::None, "TOC clips final page rows");
  r.expectTrue(ui::tocHitTest({438, 430}, 800, 480, 40, 0, 0, 0).type == ui::TocHit::Type::PageUp,
               "TOC page up action");
  r.expectTrue(ui::tocHitTest({619, 430}, 800, 480, 40, 0, 0, 0).type == ui::TocHit::Type::PageDown,
               "TOC page down action");

  ui::BookmarkListView bookmarks;
  bookmarks.itemCount = 14;
  bookmarks.scrollOffset = 12;
  auto bookmark = bookmarks.hitTest({20, 100}, 800, 480, 40, 4);
  r.expectTrue(bookmark.type == ui::BookmarkListView::Hit::Type::Item && bookmark.index == 13,
               "bookmark row maps absolute index");
  bookmark = bookmarks.hitTest({20, 180}, 800, 480, 40, 4);
  r.expectTrue(bookmark.type == ui::BookmarkListView::Hit::Type::None, "bookmark clips final page rows");
  bookmarks.clear();
  r.expectTrue(bookmarks.hitTest({20, 60}, 800, 480, 40, 4).type == ui::BookmarkListView::Hit::Type::None,
               "empty bookmarks reject row taps");
  r.expectTrue(bookmarks.hitTest({438, 430}, 800, 480, 40, 4).type == ui::BookmarkListView::Hit::Type::Add,
               "empty bookmarks keep add action");


  ui::BookStatsView stats;
  stats.showOpen = true;
  r.expectTrue(stats.hitTest({256, 430}, 800, 480) == ui::BookStatsView::Hit::Open,
               "book statistics open action");
  stats.showOpen = false;
  r.expectTrue(stats.hitTest({256, 430}, 800, 480) == ui::BookStatsView::Hit::None,
               "reader statistics hides open action");
  r.expectTrue(stats.hitTest({75, 430}, 800, 480) == ui::BookStatsView::Hit::Back,
               "book statistics back action");
  r.expectTrue(stats.hitTest({619, 430}, 800, 480) == ui::BookStatsView::Hit::None,
               "reader statistics hides remove action");
  stats.showRemove = true;
  r.expectTrue(stats.hitTest({619, 430}, 800, 480) == ui::BookStatsView::Hit::Remove,
               "recent statistics offers remove action");
  r.expectTrue(stats.hitTest({256, 430}, 800, 480, true) == ui::BookStatsView::Hit::Remove,
               "recent statistics maps LRBC remove action");

  ui::OverlayTapGuard guard;
  r.expectTrue(!guard.suppressPageTap(900), "inactive close guard allows page taps");
  guard.closedAt(1000);
  r.expectTrue(guard.suppressPageTap(1100), "tap immediately after overlay close is suppressed");
  r.expectTrue(!guard.suppressPageTap(1300), "page taps resume after close guard");

  return r.allPassed() ? 0 : 1;
}
