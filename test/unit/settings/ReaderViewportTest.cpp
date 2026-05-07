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

static Viewport computeViewport(int screenWidth, int screenHeight, int baseTop, int baseRight, int baseBottom,
                                int baseLeft) {
  Viewport vp{};
  vp.marginTop = baseTop;
  vp.marginRight = baseRight;
  vp.marginBottom = baseBottom;
  vp.marginLeft = baseLeft;
  vp.marginLeft += horizontalPadding;
  vp.marginRight += horizontalPadding;
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

  auto vp = computeViewport(screenWidth, screenHeight, baseTop, baseRight, baseBottom, baseLeft);
  runner.expectEq(baseLeft + horizontalPadding, vp.marginLeft, "marginLeft");
  runner.expectEq(baseRight + horizontalPadding, vp.marginRight, "marginRight");
  runner.expectEq(baseBottom, vp.marginBottom, "marginBottom");
  runner.expectEq(baseTop, vp.marginTop, "marginTop");
  runner.expectEq(464, vp.width, "width");    // 480 - (3+5) - (3+5)
  runner.expectEq(788, vp.height, "height");  // 800 - 9 - 3

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
