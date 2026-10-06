// Regression tests for the issue #171 hard open gate: a book opens only when
// free space covers the page cache. Below the threshold the reader refuses
// the book with a warning instead of stalling on cache writes.

#include "test_utils.h"

#include <cstdint>

#include "core/StorageLimits.h"

using papyrix::kMinOpenBookFreeBytes;
using papyrix::openBookAllowed;

int main() {
  TestUtils::TestRunner runner("StorageLimitsTest");

  // Open gate: low space refuses the book, threshold value still opens it
  {
    runner.expectFalse(openBookAllowed(0), "open: full disk refuses the book");
    runner.expectFalse(openBookAllowed(kMinOpenBookFreeBytes - 1), "open: below threshold refuses the book");
    runner.expectTrue(openBookAllowed(kMinOpenBookFreeBytes), "open: threshold value opens the book");
  }
  // The user-facing threshold is 20 MB.
  runner.expectTrue(kMinOpenBookFreeBytes == 20ull * 1024 * 1024, "threshold is 20 MB");

  return runner.allPassed() ? 0 : 1;
}
