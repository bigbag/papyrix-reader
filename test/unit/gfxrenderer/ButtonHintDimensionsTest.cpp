#include "test_utils.h"

#include <GfxRenderer.h>

int main() {
  TestUtils::TestRunner runner("ButtonHintDimensionsTest");
  runner.expectEq(106, GfxRenderer::BUTTON_HINT_WIDTH, "button hint width stays aligned with hardware");
  runner.expectEq(6, GfxRenderer::BUTTON_HINT_TEXT_PADDING, "button hint keeps horizontal text padding");
  runner.expectEq(94, GfxRenderer::BUTTON_HINT_MAX_TEXT_WIDTH, "button label width excludes both paddings");
  return runner.allPassed() ? 0 : 1;
}
