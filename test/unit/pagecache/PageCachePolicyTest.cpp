#include <PageCachePolicy.h>

#include <cstdint>

#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("PageCachePolicy");

  runner.expectFalse(page_cache::needsExtension(2, false, 0), "complete cache never extends");
  runner.expectTrue(page_cache::needsExtension(0, true, 0), "empty partial cache extends");
  runner.expectTrue(page_cache::needsExtension(1, true, 0), "one-page partial cache extends");
  runner.expectTrue(page_cache::needsExtension(2, true, 0), "two-page partial cache extends");
  runner.expectTrue(page_cache::needsExtension(3, true, 0), "threshold-sized partial cache extends");
  runner.expectFalse(page_cache::needsExtension(4, true, 0), "page before threshold does not extend");
  runner.expectTrue(page_cache::needsExtension(4, true, 1), "page at threshold extends");
  runner.expectFalse(page_cache::needsExtension(UINT16_MAX, true, 0), "large partial cache stays below threshold");
  runner.expectTrue(page_cache::needsExtension(UINT16_MAX, true, UINT16_MAX - 3),
                    "large partial cache extends without arithmetic wrap");

  runner.expectFalse(page_cache::hotExtendShouldCommit(false, 0),
                     "failed hot extend without progress is not committed");
  runner.expectTrue(page_cache::hotExtendShouldCommit(false, 1), "failed hot extend preserves completed pages");
  runner.expectTrue(page_cache::hotExtendShouldCommit(true, 0),
                    "successful hot extend may mark content complete without new pages");
  runner.expectTrue(page_cache::hotExtendIsPartial(false, false),
                    "failed hot extend remains partial despite parser state");
  runner.expectTrue(page_cache::hotExtendIsPartial(true, true), "successful hot extend follows parser continuation");
  runner.expectFalse(page_cache::hotExtendIsPartial(true, false), "successful completed hot extend is not partial");

  return runner.allPassed() ? 0 : 1;
}
