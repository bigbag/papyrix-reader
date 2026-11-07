#include "test_utils.h"
#include "ui/views/AppLauncherViews.h"

namespace papyrix {
const int8_t APP_IMAGEVIEWER = 0;
const int8_t APP_PRINTER = 1;
const int8_t APP_LOCALSEND = 2;
const int8_t APP_POMODORO = 3;
const int8_t APP_CLOCK = 4;
const int8_t APP_2048 = 5;
const int8_t APP_GO = 6;
}  // namespace papyrix

using ui::AppMenuView;

int main() {
  TestUtils::TestRunner runner("AppMenuViewTest");

  // --- moveDown wraps around ---
  {
    AppMenuView view;
    view.itemCount = 3;
    view.selected = 0;

    view.moveDown();
    runner.expectEq(int8_t(1), view.selected, "moveDown 0 -> 1");
    view.moveDown();
    runner.expectEq(int8_t(2), view.selected, "moveDown 1 -> 2");
    view.moveDown();
    runner.expectEq(int8_t(0), view.selected, "moveDown wraps to 0");
  }

  // --- moveUp wraps around ---
  {
    AppMenuView view;
    view.itemCount = 3;
    view.selected = 0;

    view.moveUp();
    runner.expectEq(int8_t(2), view.selected, "moveUp wraps to last");
    view.moveUp();
    runner.expectEq(int8_t(1), view.selected, "moveUp 2 -> 1");
    view.moveUp();
    runner.expectEq(int8_t(0), view.selected, "moveUp 1 -> 0");
  }

  // --- empty list does nothing ---
  {
    AppMenuView view;
    view.itemCount = 0;
    view.selected = 0;
    view.needsRender = false;

    view.moveDown();
    runner.expectEq(int8_t(0), view.selected, "moveDown on empty is no-op");
    runner.expectFalse(view.needsRender, "moveDown on empty doesn't set needsRender");

    view.moveUp();
    runner.expectEq(int8_t(0), view.selected, "moveUp on empty is no-op");
    runner.expectFalse(view.needsRender, "moveUp on empty doesn't set needsRender");
  }

  // --- single app stays at 0 ---
  {
    AppMenuView view;
    view.itemCount = 1;
    view.selected = 0;

    view.moveDown();
    runner.expectEq(int8_t(0), view.selected, "moveDown single app stays at 0");
    view.moveUp();
    runner.expectEq(int8_t(0), view.selected, "moveUp single app stays at 0");
  }

  // --- sets needsRender on move ---
  {
    AppMenuView view;
    view.itemCount = 2;
    view.needsRender = false;

    view.moveDown();
    runner.expectTrue(view.needsRender, "moveDown sets needsRender");

    view.needsRender = false;
    view.moveUp();
    runner.expectTrue(view.needsRender, "moveUp sets needsRender");
  }

  // --- moveUp from middle ---
  {
    AppMenuView view;
    view.itemCount = 4;
    view.selected = 2;

    view.moveUp();
    runner.expectEq(int8_t(1), view.selected, "moveUp 2 -> 1");
    view.moveUp();
    runner.expectEq(int8_t(0), view.selected, "moveUp 1 -> 0");
    view.moveUp();
    runner.expectEq(int8_t(3), view.selected, "moveUp wraps to last");
  }

  {
    AppMenuView view;
    view.itemCount = 9;
    for (int i = 0; i < view.itemCount; ++i) {
      const auto hit = view.hitTest({20, static_cast<int16_t>(AppMenuView::rowY(i, 30) + 15)}, 800, 480, 30);
      runner.expectTrue(hit.type == AppMenuView::Hit::Type::Entry && hit.index == i, "each grouped row is clickable");
      runner.expectEq(static_cast<int8_t>(i), view.selected, "navigation reaches each grouped row");
      view.moveDown();
    }
    runner.expectEq(int8_t(0), view.selected, "down wraps across all groups");
    view.moveUp();
    runner.expectEq(int8_t(8), view.selected, "up wraps across all groups");
    for (int row : {4, 7}) {
      const int gap = AppMenuView::rowY(row, 30) - AppMenuView::GROUP_GAP;
      for (int y = gap; y < gap + AppMenuView::GROUP_GAP; ++y) {
        runner.expectTrue(
            view.hitTest({20, static_cast<int16_t>(y)}, 800, 480, 30).type == AppMenuView::Hit::Type::None,
            "separator gap is not clickable");
      }
    }
    runner.expectTrue(view.hitTest({75, 430}, 800, 480, 30).type == AppMenuView::Hit::Type::Back,
                      "back footer remains back");
    runner.expectTrue(view.hitTest({270, 430}, 800, 480, 30).type == AppMenuView::Hit::Type::Open,
                      "open footer remains open");
    runner.expectTrue(view.hitTest({460, 430}, 800, 480, 30, true).type == AppMenuView::Hit::Type::Back,
                      "remapped footer remains back");
    runner.expectTrue(view.hitTest({650, 430}, 800, 480, 30, true).type == AppMenuView::Hit::Type::Open,
                      "remapped footer remains open");
    runner.expectEq(int(papyrix::APP_LOCALSEND), ui::appIdForMenuItem(2), "LocalSend follows transfer actions");
    runner.expectEq(int(papyrix::APP_PRINTER), ui::appIdForMenuItem(3), "Printer ends file transfer apps");
    runner.expectEq(int(papyrix::APP_IMAGEVIEWER), ui::appIdForMenuItem(4), "Image Viewer starts common apps");
    runner.expectEq(int(papyrix::APP_POMODORO), ui::appIdForMenuItem(5), "Pomodoro follows Image Viewer");
    runner.expectEq(int(papyrix::APP_CLOCK), ui::appIdForMenuItem(6), "Clock ends common apps");
    runner.expectEq(int(papyrix::APP_2048), ui::appIdForMenuItem(7), "2048 starts games");
    runner.expectEq(int(papyrix::APP_GO), ui::appIdForMenuItem(8), "Go ends games");
  }

  {
    AppMenuView view;
    view.itemCount = 9;
    runner.expectEq(74, view.rowPitch(800, 80), "portrait pitch fits all rows");
    runner.expectEq(39, view.rowPitch(480, 140), "landscape pitch fits oversized spacing");
    runner.expectEq(30, view.rowPitch(480, 30), "normal pitch is unchanged");
    runner.expectEq(0, view.rowPitch(100, 80), "unavailable list has no rows");
    const int portraitCenters[] = {97, 171, 245, 319, 401, 475, 549, 631, 705};
    const int landscapeCenters[] = {79, 118, 157, 196, 243, 282, 321, 368, 407};
    for (int pitch : {80, 140}) {
      for (int i = 0; i < 9; ++i) {
        auto portrait = view.hitTest({20, static_cast<int16_t>(portraitCenters[i])}, 480, 800, pitch);
        auto landscape = view.hitTest({20, static_cast<int16_t>(landscapeCenters[i])}, 800, 480, pitch);
        runner.expectTrue(portrait.type == AppMenuView::Hit::Type::Entry && portrait.index == i,
                          "oversized portrait row is reachable");
        runner.expectTrue(landscape.type == AppMenuView::Hit::Type::Entry && landscape.index == i,
                          "oversized landscape row is reachable");
      }
      for (int y : {356, 363, 586, 593}) {
        runner.expectTrue(view.hitTest({20, static_cast<int16_t>(y)}, 480, 800, pitch).type ==
                              AppMenuView::Hit::Type::None,
                          "portrait divider is not clickable");
      }
      for (int y : {216, 223, 341, 348}) {
        runner.expectTrue(view.hitTest({20, static_cast<int16_t>(y)}, 800, 480, pitch).type ==
                              AppMenuView::Hit::Type::None,
                          "landscape divider is not clickable");
      }
      runner.expectTrue(view.hitTest({20, 756}, 480, 800, pitch).type == AppMenuView::Hit::Type::Back,
                        "portrait footer remains back");
      runner.expectTrue(view.hitTest({270, 430}, 800, 480, pitch).type == AppMenuView::Hit::Type::Open,
                        "landscape footer remains open");
    }
    runner.expectTrue(view.hitTest({20, 179}, 800, 480, 30).index == 3, "normal row position is unchanged");
    runner.expectTrue(view.hitTest({20, 180}, 800, 480, 30).type == AppMenuView::Hit::Type::None,
                      "normal divider position is unchanged");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
