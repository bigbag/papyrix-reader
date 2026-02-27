#include "test_utils.h"

#include <cstdint>

// Inline ButtonBar to avoid pulling in GfxRenderer/Theme dependencies
struct ButtonBar {
  const char* labels[4] = {"", "", "", ""};

  explicit constexpr ButtonBar(const char* b1 = "", const char* b2 = "", const char* b3 = "", const char* b4 = "")
      : labels{b1 ? b1 : "", b2 ? b2 : "", b3 ? b3 : "", b4 ? b4 : ""} {}

  bool isActive(int idx) const { return idx >= 0 && idx < 4 && labels[idx] && labels[idx][0] != '\0'; }
};

// Inline JumpToPageView to avoid firmware/graphics dependencies
struct JumpToPageView {
  ButtonBar buttons{"Cancel", "Go", "-10", "+10"};
  int16_t targetPage = 1;
  int16_t maxPage = 1;
  bool needsRender = true;

  void setMaxPage(int max) {
    maxPage = static_cast<int16_t>(max);
    if (targetPage > maxPage) {
      targetPage = maxPage;
    }
    needsRender = true;
  }

  void setPage(int page) {
    if (page >= 1 && page <= maxPage) {
      targetPage = static_cast<int16_t>(page);
      needsRender = true;
    }
  }

  void incrementPage(int delta) {
    int newPage = targetPage + delta;
    if (newPage < 1) newPage = 1;
    if (newPage > maxPage) newPage = maxPage;
    if (newPage != targetPage) {
      targetPage = static_cast<int16_t>(newPage);
      needsRender = true;
    }
  }
};

int main() {
  TestUtils::TestRunner runner("JumpToPageViewTest");

  // --- defaults ---
  {
    JumpToPageView view;
    runner.expectEq(int16_t(1), view.targetPage, "default targetPage is 1");
    runner.expectEq(int16_t(1), view.maxPage, "default maxPage is 1");
    runner.expectTrue(view.needsRender, "default needsRender is true");
  }

  // --- setMaxPage ---
  {
    JumpToPageView view;
    view.needsRender = false;
    view.setMaxPage(100);
    runner.expectEq(int16_t(100), view.maxPage, "setMaxPage updates maxPage");
    runner.expectEq(int16_t(1), view.targetPage, "setMaxPage preserves targetPage when in range");
    runner.expectTrue(view.needsRender, "setMaxPage sets needsRender");
  }

  // --- setMaxPage clamps targetPage ---
  {
    JumpToPageView view;
    view.setMaxPage(50);
    view.targetPage = 50;
    view.setMaxPage(30);
    runner.expectEq(int16_t(30), view.targetPage, "setMaxPage clamps targetPage to new max");
    runner.expectEq(int16_t(30), view.maxPage, "maxPage updated");
  }

  // --- setPage valid ---
  {
    JumpToPageView view;
    view.setMaxPage(100);
    view.needsRender = false;
    view.setPage(50);
    runner.expectEq(int16_t(50), view.targetPage, "setPage updates targetPage");
    runner.expectTrue(view.needsRender, "setPage sets needsRender");
  }

  // --- setPage boundary values ---
  {
    JumpToPageView view;
    view.setMaxPage(100);

    view.setPage(1);
    runner.expectEq(int16_t(1), view.targetPage, "setPage accepts minimum (1)");

    view.setPage(100);
    runner.expectEq(int16_t(100), view.targetPage, "setPage accepts maximum");
  }

  // --- setPage out of range ignored ---
  {
    JumpToPageView view;
    view.setMaxPage(100);
    view.setPage(50);
    view.needsRender = false;

    view.setPage(0);
    runner.expectEq(int16_t(50), view.targetPage, "setPage(0) ignored");
    runner.expectFalse(view.needsRender, "setPage(0) doesn't set needsRender");

    view.setPage(-1);
    runner.expectEq(int16_t(50), view.targetPage, "setPage(-1) ignored");

    view.setPage(101);
    runner.expectEq(int16_t(50), view.targetPage, "setPage(101) ignored when max=100");
  }

  // --- incrementPage basic ---
  {
    JumpToPageView view;
    view.setMaxPage(100);
    view.setPage(50);
    view.needsRender = false;

    view.incrementPage(10);
    runner.expectEq(int16_t(60), view.targetPage, "incrementPage(+10) works");
    runner.expectTrue(view.needsRender, "incrementPage sets needsRender");

    view.needsRender = false;
    view.incrementPage(-10);
    runner.expectEq(int16_t(50), view.targetPage, "incrementPage(-10) works");
    runner.expectTrue(view.needsRender, "incrementPage(-10) sets needsRender");
  }

  // --- incrementPage clamps at boundaries ---
  {
    JumpToPageView view;
    view.setMaxPage(100);
    view.setPage(5);
    view.incrementPage(-10);
    runner.expectEq(int16_t(1), view.targetPage, "incrementPage clamps at 1");

    view.setPage(95);
    view.incrementPage(10);
    runner.expectEq(int16_t(100), view.targetPage, "incrementPage clamps at maxPage");
  }

  // --- incrementPage no-op when already at boundary ---
  {
    JumpToPageView view;
    view.setMaxPage(100);
    view.setPage(1);
    view.needsRender = false;
    view.incrementPage(-10);
    runner.expectEq(int16_t(1), view.targetPage, "incrementPage at min is no-op");
    runner.expectFalse(view.needsRender, "incrementPage no-op doesn't set needsRender");

    view.setPage(100);
    view.needsRender = false;
    view.incrementPage(10);
    runner.expectEq(int16_t(100), view.targetPage, "incrementPage at max is no-op");
    runner.expectFalse(view.needsRender, "incrementPage no-op doesn't set needsRender");
  }

  // --- incrementPage(0) is no-op ---
  {
    JumpToPageView view;
    view.setMaxPage(100);
    view.setPage(50);
    view.needsRender = false;
    view.incrementPage(0);
    runner.expectEq(int16_t(50), view.targetPage, "incrementPage(0) is no-op");
    runner.expectFalse(view.needsRender, "incrementPage(0) doesn't set needsRender");
  }

  // --- incrementPage with single page ---
  {
    JumpToPageView view;
    view.needsRender = false;
    view.incrementPage(1);
    runner.expectEq(int16_t(1), view.targetPage, "incrementPage(+1) clamped on single page");
    runner.expectFalse(view.needsRender, "no change on single page");

    view.incrementPage(-1);
    runner.expectEq(int16_t(1), view.targetPage, "incrementPage(-1) clamped on single page");
  }

  // --- ButtonBar defaults ---
  {
    JumpToPageView view;
    runner.expectTrue(view.buttons.isActive(0), "button 0 (Cancel) is active");
    runner.expectTrue(view.buttons.isActive(1), "button 1 (Go) is active");
    runner.expectTrue(view.buttons.isActive(2), "button 2 (-10) is active");
    runner.expectTrue(view.buttons.isActive(3), "button 3 (+10) is active");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
