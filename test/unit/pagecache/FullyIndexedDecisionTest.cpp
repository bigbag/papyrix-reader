// Regression tests for ReaderState::isFullyIndexed's "is the whole book paginated?"
// decision. Mirrors the two decision paths from ReaderState.cpp (the project pattern
// of inlining logic to avoid heavy linker dependencies — see GlobalPageMetricsTest /
// MetricsIndexTest).
//
// Guards issue #136: with "Full Book Process" ON, large books showed "~" (approximate
// total) again. A section whose page cache was left *partial* (skipped during indexing
// when the heap gate tripped) must count as NOT fully indexed, so Full Book Process
// re-runs and finishes it instead of permanently treating the book as complete. The
// regression was that the probe-loop fallback only rejected *missing* sections
// (!valid) and ignored partial ones, and a bare ".indexed" marker made it permanent.

#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

// Mirrors the probe-loop fallback in isFullyIndexed (used when metrics.bin is absent):
// the book is fully indexed iff every section cache is present (valid) AND complete
// (not partial).
struct ProbeResult {
  bool valid = false;
  bool partial = false;
};

bool allSectionsComplete(const std::vector<ProbeResult>& probes) {
  for (const auto& p : probes) {
    if (!p.valid || p.partial) return false;
  }
  return true;
}

// Mirrors the metrics.bin fast path in isFullyIndexed: fully indexed iff every recorded
// section is exact. A skipped/partial section is recorded with exact=false.
struct MetricsEntry {
  uint16_t pages = 0;
  bool exact = false;
};

bool allEntriesExact(const std::vector<MetricsEntry>& entries) {
  return std::all_of(entries.begin(), entries.end(), [](const MetricsEntry& e) { return e.exact; });
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("FullyIndexedDecision");

  // ============================================
  // Probe-loop fallback
  // ============================================

  // All sections present and complete → fully indexed
  {
    std::vector<ProbeResult> probes = {{true, false}, {true, false}, {true, false}};
    runner.expectTrue(allSectionsComplete(probes), "all_complete_is_indexed");
  }

  // Regression (#136): one section present but PARTIAL → NOT fully indexed.
  // The buggy version ignored `partial` and reported true here, leaving the book
  // permanently approximate.
  {
    std::vector<ProbeResult> probes = {{true, false}, {true, true}, {true, false}};
    runner.expectFalse(allSectionsComplete(probes), "one_partial_not_indexed");
  }

  // A partial section anywhere counts — first and last positions
  {
    std::vector<ProbeResult> first = {{true, true}, {true, false}};
    std::vector<ProbeResult> last = {{true, false}, {true, true}};
    runner.expectFalse(allSectionsComplete(first), "first_partial_not_indexed");
    runner.expectFalse(allSectionsComplete(last), "last_partial_not_indexed");
  }

  // A missing (never-cached) section → NOT fully indexed
  {
    std::vector<ProbeResult> probes = {{true, false}, {false, false}, {true, false}};
    runner.expectFalse(allSectionsComplete(probes), "one_missing_not_indexed");
  }

  // Missing reported as partial too (invalid header) → still not indexed
  {
    std::vector<ProbeResult> probes = {{false, true}};
    runner.expectFalse(allSectionsComplete(probes), "invalid_not_indexed");
  }

  // Empty book (no sections) → vacuously complete, matching the loop's behaviour
  {
    std::vector<ProbeResult> probes;
    runner.expectTrue(allSectionsComplete(probes), "empty_is_indexed");
  }

  // Single complete section
  {
    std::vector<ProbeResult> probes = {{true, false}};
    runner.expectTrue(allSectionsComplete(probes), "single_complete_is_indexed");
  }

  // ============================================
  // metrics.bin fast path
  // ============================================

  // All exact → fully indexed
  {
    std::vector<MetricsEntry> entries = {{10, true}, {25, true}, {5, true}};
    runner.expectTrue(allEntriesExact(entries), "metrics_all_exact_is_indexed");
  }

  // Regression (#136): one estimated/non-exact entry → NOT fully indexed.
  // A skipped section is persisted with exact=false; the fast path must reject it so
  // indexing resumes rather than locking in the "~" total.
  {
    std::vector<MetricsEntry> entries = {{10, true}, {7, false}, {12, true}};
    runner.expectFalse(allEntriesExact(entries), "metrics_one_estimated_not_indexed");
  }

  // Empty entries → vacuously exact
  {
    std::vector<MetricsEntry> entries;
    runner.expectTrue(allEntriesExact(entries), "metrics_empty_is_indexed");
  }

  return runner.allPassed() ? 0 : 1;
}
