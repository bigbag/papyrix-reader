#include "test_utils.h"

#include <cstdint>

// Mirror the viewport calculation from ReaderState::getReaderViewport()
// without pulling in firmware dependencies.
struct Viewport {
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  int width = 0;
  int height = 0;
};

static constexpr int horizontalPadding = 5;
static constexpr int statusBarMargin = 23;

// Mirrors getReaderViewport(bool showStatusBar) logic
static Viewport computeViewport(int screenWidth, int screenHeight, int baseTop, int baseRight, int baseBottom,
                                int baseLeft, bool showStatusBar) {
  Viewport vp{};
  vp.marginTop = baseTop;
  vp.marginRight = baseRight;
  vp.marginBottom = baseBottom;
  vp.marginLeft = baseLeft;
  vp.marginLeft += horizontalPadding;
  vp.marginRight += horizontalPadding;
  if (showStatusBar) {
    vp.marginBottom += statusBarMargin;
  }
  vp.width = screenWidth - vp.marginLeft - vp.marginRight;
  vp.height = screenHeight - vp.marginTop - vp.marginBottom;
  return vp;
}

int main() {
  TestUtils::TestRunner runner("ReaderViewportTest");

  // Device dimensions: 480x800, base margins from getOrientedViewableTRBL: top=9, right=3, bottom=3, left=3
  const int screenWidth = 480;
  const int screenHeight = 800;
  const int baseTop = 9;
  const int baseRight = 3;
  const int baseBottom = 3;
  const int baseLeft = 3;

  // Test 1: Status bar enabled - should include 23px margin
  {
    auto vp = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, true);
    runner.expectEq(baseLeft + horizontalPadding, vp.marginLeft, "statusbar_on_marginLeft");
    runner.expectEq(baseRight + horizontalPadding, vp.marginRight, "statusbar_on_marginRight");
    runner.expectEq(baseBottom + statusBarMargin, vp.marginBottom, "statusbar_on_marginBottom");
    runner.expectEq(baseTop, vp.marginTop, "statusbar_on_marginTop");
    runner.expectEq(464, vp.width, "statusbar_on_width");    // 480 - (3+5) - (3+5)
    runner.expectEq(765, vp.height, "statusbar_on_height");  // 800 - 9 - (3+23)
  }

  // Test 2: Status bar disabled - no 23px margin, content expands
  {
    auto vp = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, false);
    runner.expectEq(baseLeft + horizontalPadding, vp.marginLeft, "statusbar_off_marginLeft");
    runner.expectEq(baseRight + horizontalPadding, vp.marginRight, "statusbar_off_marginRight");
    runner.expectEq(baseBottom, vp.marginBottom, "statusbar_off_marginBottom");
    runner.expectEq(baseTop, vp.marginTop, "statusbar_off_marginTop");
    runner.expectEq(464, vp.width, "statusbar_off_width");    // Same width
    runner.expectEq(788, vp.height, "statusbar_off_height");  // 800 - 9 - 3 (no +23)
  }

  // Test 3: Height difference is exactly statusBarMargin
  {
    auto vpOn = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, true);
    auto vpOff = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, false);
    runner.expectEq(statusBarMargin, vpOff.height - vpOn.height, "height_diff_is_statusBarMargin");
  }

  // Test 4: Width unchanged regardless of status bar
  {
    auto vpOn = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, true);
    auto vpOff = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, false);
    runner.expectEq(vpOn.width, vpOff.width, "width_unchanged");
  }

  // Test 5: StatusBar enum values (StatusNone=0, StatusShow=1)
  {
    const uint8_t StatusNone = 0;
    const uint8_t StatusShow = 1;

    auto vpShow = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, StatusShow != 0);
    auto vpNone = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft, StatusNone != 0);

    runner.expectEq(765, vpShow.height, "enum_show_height");
    runner.expectEq(788, vpNone.height, "enum_none_height");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
