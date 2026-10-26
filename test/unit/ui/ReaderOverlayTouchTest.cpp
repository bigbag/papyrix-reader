#include "test_utils.h"
#include "ui/views/ReaderViews.h"

using ui::touch::Point;

namespace {
using ui::touch::Rect;
Point center(Rect rect) {
  return {static_cast<int16_t>(rect.x + rect.width / 2), static_cast<int16_t>(rect.y + rect.height / 2)};
}
bool overlaps(Rect a, Rect b) {
  return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
}
bool inside(Rect outer, Rect inner) {
  return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}
}  // namespace
int main() {
  TestUtils::TestRunner r("Reader overlay touch");

  ui::ReaderMenuView menu;
  menu.show();
  auto menuHit = menu.hitTest({310, 210}, 800, 480, 32);
  r.expectTrue(menuHit.type == ui::ReaderMenuView::Hit::Type::Item && menuHit.index == 0, "reader menu first item");
  menuHit = menu.hitTest({310, 290}, 800, 480, 32);
  r.expectTrue(menuHit.type == ui::ReaderMenuView::Hit::Type::Item && menuHit.index == 2,
               "reader menu book statistics");
  menuHit = menu.hitTest({310, 310}, 800, 480, 32);
  r.expectTrue(menuHit.type == ui::ReaderMenuView::Hit::Type::Item &&
                   menuHit.index == static_cast<int>(ui::ReaderMenuView::Item::Back),
               "reader menu exit action accepts touch");
  menu.hide();
  r.expectTrue(menu.hitTest({310, 210}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "hidden reader menu rejects taps");
  menu.show();
  r.expectTrue(menu.hitTest({0, 0}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "menu without light does not dismiss on outside tap");

  ui::ReaderMenuView touchMenu;
  touchMenu.hasTouch = true;
  touchMenu.showBack = false;
  touchMenu.show();
  const auto touchLayout = touchMenu.layout(800, 480, 32);
  auto touchHit = touchMenu.hitTest(center(touchLayout.items[0]), 800, 480, 32);
  r.expectTrue(touchHit.type == ui::ReaderMenuView::Hit::Type::Item && touchHit.index == 0,
               "touch menu without light keeps first item");
  r.expectTrue(touchMenu.hitTest({0, 0}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::Dismiss,
               "touch menu without light dismisses on outside tap");
  r.expectTrue(touchMenu.hitTest({799, 479}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::Confirm,
               "bottom-right corner confirms the current menu item");
  r.expectTrue(touchMenu.hitTest({0, 479}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::Dismiss,
               "bottom-left corner closes the menu");
  r.expectTrue(touchMenu.hitTest({400, 479}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::Dismiss,
               "bottom center closes the menu instead of staying inactive");
  const Rect touchControls[] = {touchLayout.items[0], touchLayout.items[1], touchLayout.items[2]};
  bool touchFits = true;
  bool touchDisjoint = true;
  for (const Rect& control : touchControls) {
    touchFits = touchFits && inside(touchLayout.menu, control) && control.width >= 30 && control.height >= 30;
    for (const Rect& other : touchControls) {
      if (&control != &other) touchDisjoint = touchDisjoint && !overlaps(control, other);
    }
  }
  r.expectTrue(touchFits, "touch menu without light controls fit with usable targets");
  r.expectTrue(touchDisjoint, "touch menu without light controls do not overlap");
  touchMenu.hide();
  r.expectTrue(touchMenu.hitTest({0, 0}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "hidden touch menu without light does not dismiss");
  ui::ReaderMenuView lightMenu;
  lightMenu.hasFrontLight = true;
  lightMenu.hasTouch = true;
  lightMenu.showBack = false;
  lightMenu.brightness = 45;
  lightMenu.warmth = 50;
  lightMenu.show();
  const auto lightLayout = lightMenu.layout(800, 480, 32);
  const auto brightnessBar = lightLayout.brightnessBar;
  const auto warmthBar = lightLayout.warmthBar;
  auto lightHit = lightMenu.hitTest(center(lightLayout.items[0]), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Item && lightHit.index == 0,
               "light menu keeps first item");
  lightHit = lightMenu.hitTest(center(lightLayout.items[2]), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Item &&
                   lightHit.index == static_cast<int>(ui::ReaderMenuView::Item::BookStats),
               "light menu keeps book statistics");
  lightHit = lightMenu.hitTest({brightnessBar.x, center(brightnessBar).y}, 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value == 0,
               "brightness bar left edge is 0 percent");
  lightHit = lightMenu.hitTest(
      {static_cast<int16_t>(brightnessBar.x + brightnessBar.width - 1), center(brightnessBar).y}, 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value == 100,
               "brightness bar right edge is 100 percent");
  lightHit = lightMenu.hitTest(center(brightnessBar), 800, 480, 32);
  r.expectTrue(
      lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value >= 49 && lightHit.value <= 51,
      "brightness bar center is about 50 percent");
  lightHit = lightMenu.hitTest(center(lightLayout.brightnessMinus), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value == 40,
               "brightness minus steps down by 5");
  lightHit = lightMenu.hitTest(center(lightLayout.brightnessPlus), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value == 50,
               "brightness plus steps up by 5");
  lightHit = lightMenu.hitTest({warmthBar.x, center(warmthBar).y}, 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Warmth && lightHit.value == 0,
               "warmth bar left edge is 0 percent");
  lightHit =
      lightMenu.hitTest({static_cast<int16_t>(warmthBar.x + warmthBar.width - 1), center(warmthBar).y}, 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Warmth && lightHit.value == 100,
               "warmth bar right edge is 100 percent");
  lightHit = lightMenu.hitTest(center(lightLayout.warmthMinus), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Warmth && lightHit.value == 45,
               "warmth minus steps down by 5");
  lightHit = lightMenu.hitTest(center(lightLayout.warmthPlus), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Warmth && lightHit.value == 55,
               "warmth plus steps up by 5");
  lightMenu.brightness = 0;
  lightHit = lightMenu.hitTest(center(lightLayout.brightnessMinus), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value == 0,
               "brightness minus clamps at 0");
  lightHit = lightMenu.hitTest(center(warmthBar), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Warmth && lightHit.value >= 49 && lightHit.value <= 51,
               "warmth can change while light is off");
  lightMenu.brightness = 100;
  lightHit = lightMenu.hitTest(center(lightLayout.brightnessPlus), 800, 480, 32);
  r.expectTrue(lightHit.type == ui::ReaderMenuView::Hit::Type::Brightness && lightHit.value == 100,
               "brightness plus clamps at 100");
  r.expectTrue(lightMenu.hitTest({0, 0}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::Dismiss,
               "light menu dismisses on outside tap");
  r.expectTrue(lightMenu.hitTest({799, 479}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::Confirm,
               "light menu keeps the bottom-right Confirm action");
  const auto brightnessLabel = lightLayout.brightnessLabel;
  r.expectTrue(
      lightMenu
              .hitTest({static_cast<int16_t>(brightnessLabel.x + brightnessLabel.width - 1), center(brightnessLabel).y},
                       800, 480, 32)
              .type == ui::ReaderMenuView::Hit::Type::None,
      "light menu label tap does not fall through");
  lightMenu.hide();
  r.expectTrue(lightMenu.hitTest(center(brightnessBar), 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "hidden light menu rejects brightness taps");
  r.expectTrue(lightMenu.hitTest({0, 0}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "hidden light menu does not dismiss");

  ui::ReaderMenuView buttonLightMenu;
  buttonLightMenu.hasFrontLight = true;
  buttonLightMenu.brightness = 45;
  buttonLightMenu.show();
  const auto buttonLightLayout = buttonLightMenu.layout(800, 480, 32);
  r.expectTrue(buttonLightMenu.hitTest(center(buttonLightLayout.brightnessBar), 800, 480, 32).type ==
                   ui::ReaderMenuView::Hit::Type::Brightness,
               "light controls do not require touch capability flag");
  r.expectTrue(buttonLightMenu.hitTest({0, 0}, 800, 480, 32).type == ui::ReaderMenuView::Hit::Type::None,
               "menu without touch does not dismiss on outside tap");

  for (const auto screen : {Point{800, 480}, Point{480, 800}}) {
    for (const int16_t itemHeight : {32, 50, 255}) {
      ui::ReaderMenuView view;
      view.hasFrontLight = true;
      view.hasTouch = true;
      view.showBack = false;
      view.show();
      const auto geometry = view.layout(screen.x, screen.y, itemHeight);
      const Rect controls[] = {geometry.items[0],        geometry.items[1],      geometry.items[2],
                               geometry.brightnessMinus, geometry.brightnessBar, geometry.brightnessPlus,
                               geometry.warmthMinus,     geometry.warmthBar,     geometry.warmthPlus};
      bool fits = inside({0, 0, screen.x, screen.y}, geometry.menu);
      bool disjoint = true;
      bool usable = true;
      for (const Rect& control : controls) {
        fits = fits && inside(geometry.menu, control);
        usable = usable && control.width >= 30 && control.height >= 30;
        for (const Rect& other : controls) {
          if (&control != &other) disjoint = disjoint && !overlaps(control, other);
        }
      }
      r.expectTrue(fits, "enhanced light menu fits screen at bounded item height");
      r.expectTrue(disjoint, "enhanced light controls do not overlap at bounded item height");
      r.expectTrue(usable, "enhanced light controls have usable targets at bounded item height");
      r.expectTrue(geometry.warmthBar.y + geometry.warmthBar.height < geometry.items[0].y,
                   "light controls appear above navigation items");
      const Point belowItems{center(geometry.items[2]).x,
                             static_cast<int16_t>(geometry.items[2].y + geometry.items[2].height + 1)};
      r.expectTrue(view.hitTest(belowItems, screen.x, screen.y, itemHeight).type == ui::ReaderMenuView::Hit::Type::None,
                   "space below the last item has no hidden Back or Done action");
      const auto itemHit = view.hitTest(center(geometry.items[2]), screen.x, screen.y, itemHeight);
      r.expectTrue(itemHit.type == ui::ReaderMenuView::Hit::Type::Item && itemHit.index == 2,
                   "item hit uses bounded item height");
    }
  }

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
  r.expectTrue(stats.hitTest({256, 430}, 800, 480) == ui::BookStatsView::Hit::Open, "book statistics open action");
  stats.showOpen = false;
  r.expectTrue(stats.hitTest({256, 430}, 800, 480) == ui::BookStatsView::Hit::None,
               "reader statistics hides open action");
  r.expectTrue(stats.hitTest({75, 430}, 800, 480) == ui::BookStatsView::Hit::Back, "book statistics back action");
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
