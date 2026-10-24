#include "Ssd1677Driver.h"

#include "test_utils.h"

using papyrix::eink::Ssd1677RefreshMode;
using papyrix::eink::makeSsd1677UpdatePlan;

int main() {
  TestUtils::TestRunner runner("SSD1677 update plan");

  auto plan = makeSsd1677UpdatePlan(Ssd1677RefreshMode::Full, 48000);
  runner.expectTrue(plan.writeBwBeforeRefresh, "full refresh writes BW before refresh");
  runner.expectTrue(plan.writeRedBeforeRefresh, "full refresh writes RED before refresh");
  runner.expectTrue(plan.syncRedAfterRefresh, "full refresh synchronizes RED after refresh");

  plan = makeSsd1677UpdatePlan(Ssd1677RefreshMode::Fast, 48000);
  runner.expectTrue(plan.writeBwBeforeRefresh, "fast refresh writes BW before refresh");
  runner.expectFalse(plan.writeRedBeforeRefresh, "fast refresh keeps the previous RED frame");
  runner.expectTrue(plan.syncRedAfterRefresh, "fast refresh synchronizes RED after refresh");

  plan = makeSsd1677UpdatePlan(Ssd1677RefreshMode::Window, 8);
  runner.expectTrue(plan.writeBwBeforeRefresh, "window refresh writes BW before refresh");
  runner.expectFalse(plan.writeRedBeforeRefresh, "window refresh keeps the previous RED window");
  runner.expectTrue(plan.syncRedAfterRefresh, "window refresh synchronizes the RED window");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
