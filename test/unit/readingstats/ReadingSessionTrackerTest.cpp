#include "ReadingSessionTracker.h"
#include "test_utils.h"

#include <climits>

using papyrix::ReadingSessionTracker;

int main() {
  TestUtils::TestRunner runner("ReadingSessionTracker");

  {
    ReadingSessionTracker tracker;
    tracker.begin("/a", 1000);
    tracker.onActivity(31000);
    tracker.updateProgress(25);
    const auto live = tracker.snapshot(61000);
    runner.expectTrue(live.active, "snapshot active");
    runner.expectEq(uint32_t(60), live.seconds, "live includes accumulated and current interval");
    runner.expectTrue(live.hasProgress, "live progress known");
    runner.expectEq(uint8_t(25), live.progressPercent, "live progress value");
  }

  {
    ReadingSessionTracker tracker;
    tracker.begin("/a", 0);
    tracker.onActivity(ReadingSessionTracker::MAX_IDLE_MS + 5000);
    const auto result = tracker.end(ReadingSessionTracker::MAX_IDLE_MS * 3);
    runner.expectEq(uint32_t(180), result.seconds, "each interval capped at 90 seconds");
  }

  {
    ReadingSessionTracker tracker;
    tracker.begin("/a", 1000);
    const auto result = tracker.end(31000);
    runner.expectEq(uint32_t(30), result.seconds, "end credits final interval");
    runner.expectFalse(tracker.end(40000).active, "second end is inactive");
  }

  {
    ReadingSessionTracker tracker;
    tracker.begin("/a", UINT32_MAX - 499);
    tracker.onActivity(500);
    runner.expectEq(uint32_t(1), tracker.snapshot(500).seconds, "millis wrap handled");
  }

  {
    ReadingSessionTracker tracker;
    tracker.updateProgress(90);
    tracker.onActivity(1000);
    runner.expectFalse(tracker.snapshot(1000).active, "inactive operations are no-op");
  }

  return runner.allPassed() ? 0 : 1;
}
