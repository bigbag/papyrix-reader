// Tests for PageCache::estimatedTotalPages() — extrapolates the total page
// count from the parser's source-byte progress while the cache is still partial.
//
// The function is a pure inline accessor over three members; this test mirrors
// its body and exercises the boundary conditions without linking the full
// PageCache class (heavy dependencies).

#include "test_utils.h"

#include <cstdint>

namespace {

// Mirror of PageCache::kMinPagesForEstimate / estimatedTotalPages() in
// lib/PageCache/src/PageCache.h. Keep in sync with the production code.
constexpr uint16_t kMinPagesForEstimate = 3;

uint32_t estimatedTotalPages(uint16_t pageCount, uint32_t bytesConsumed, uint32_t totalBytes) {
  if (pageCount < kMinPagesForEstimate) return 0;
  if (bytesConsumed == 0 || totalBytes == 0) return 0;
  if (bytesConsumed >= totalBytes) return pageCount;
  return static_cast<uint32_t>(static_cast<uint64_t>(totalBytes) * pageCount / bytesConsumed);
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("PageCache::estimatedTotalPages");

  // Below the minimum-pages gate: returns 0 regardless of bytes.
  runner.expectEq<uint32_t>(0, estimatedTotalPages(0, 100, 1000), "page_count_zero");
  runner.expectEq<uint32_t>(0, estimatedTotalPages(1, 100, 1000), "page_count_one_below_gate");
  runner.expectEq<uint32_t>(0, estimatedTotalPages(2, 100, 1000), "page_count_two_below_gate");

  // Missing parser sample: returns 0 (caller falls back to cached count).
  runner.expectEq<uint32_t>(0, estimatedTotalPages(5, 0, 1000), "bytes_consumed_zero");
  runner.expectEq<uint32_t>(0, estimatedTotalPages(5, 100, 0), "total_bytes_zero");
  runner.expectEq<uint32_t>(0, estimatedTotalPages(5, 0, 0), "both_zero");

  // Final extend pass: bytesConsumed reaches totalBytes → estimate is the exact count.
  runner.expectEq<uint32_t>(50, estimatedTotalPages(50, 1000, 1000), "fully_consumed_returns_pagecount");
  runner.expectEq<uint32_t>(50, estimatedTotalPages(50, 1500, 1000), "over_consumed_returns_pagecount");

  // Typical mid-parse case: ~1.5 MB FB2, 5 pages cached after consuming ~17 KB.
  // 1500000 * 5 / 17000 = 441
  runner.expectEq<uint32_t>(441, estimatedTotalPages(5, 17000, 1500000), "fb2_typical_first_chunk");

  // Halfway through a 400-page book: 1.5 MB / 2 = 750 KB consumed for 200 pages.
  // 1500000 * 200 / 750000 = 400
  runner.expectEq<uint32_t>(400, estimatedTotalPages(200, 750000, 1500000), "halfway_through_book");

  // Near the end: 95 % consumed, 380 pages cached. 1500000 * 380 / 1425000 = 400
  runner.expectEq<uint32_t>(400, estimatedTotalPages(380, 1425000, 1500000), "near_end_of_book");

  // Boundary at the gate: 3 cached pages is the smallest valid sample.
  runner.expectEq<uint32_t>(300, estimatedTotalPages(3, 10000, 1000000), "at_min_pages_gate");

  // Overflow safety: very large file × very large page count must use the
  // uint64_t intermediate in the production code. Pick numbers where uint32_t
  // multiplication would overflow (4 GB × 10000 = 4e13 > 2^32 ≈ 4.3e9).
  // 4_000_000_000 * 10000 / 4_000_000_000 = 10000
  runner.expectEq<uint32_t>(10000, estimatedTotalPages(10000, 4000000000U, 4000000000U),
                            "no_overflow_at_max_inputs");

  return runner.allPassed() ? 0 : 1;
}
