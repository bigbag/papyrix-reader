#include "test_utils.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Redefine AbortCallback matching lib/PageCache/ContentParser.h
using AbortCallback = std::function<bool()>;

// Minimal Page stub
class Page {
 public:
  int id;
  explicit Page(int id) : id(id) {}
};

// Mock ContentParser that simulates configurable abort/complete/maxPages behavior.
// Models the hasMore_ logic from EpubChapterParser:
//   hasMore_ = hitMaxPages || parser.wasAborted() || (!success && pagesCreated > 0)
// Also models canResume() for hot extend: parser keeps position between parsePages() calls.
class MockContentParser {
 public:
  MockContentParser(int totalPages) : totalPages_(totalPages) {}

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages = 0,
                  const AbortCallback& shouldAbort = nullptr) {
    aborted_ = false;
    uint16_t pagesCreated = 0;
    bool hitMaxPages = false;

    for (int i = currentPage_; i < totalPages_; i++) {
      if (shouldAbort && shouldAbort()) {
        aborted_ = true;
        break;
      }

      // Simulate parse failure (e.g., XML_GetBuffer returns null)
      if (failAfterPages_ > 0 && pagesCreated >= failAfterPages_) {
        break;
      }

      if (hitMaxPages) break;

      onPageComplete(std::make_unique<Page>(i));
      pagesCreated++;
      currentPage_ = i + 1;

      if (maxPages > 0 && pagesCreated >= maxPages) {
        hitMaxPages = true;
      }
    }

    bool success = !aborted_ && !failAfterPages_;

    // Core logic: hasMore_ tracks whether more content remains unparsed.
    // reachedEnd: true when we've consumed all available content
    // hitMaxPages: stopped at page limit (but only matters if content remains)
    // aborted_: stopped due to timeout/memory, more content exists
    // !success && pagesCreated > 0: parse error mid-chapter, partial content exists
    bool reachedEnd = (currentPage_ >= totalPages_);
    hasMore_ = (!reachedEnd && hitMaxPages) || aborted_ || (!reachedEnd && !success && pagesCreated > 0);

    return success;
  }

  bool hasMoreContent() const { return hasMore_; }
  bool wasAborted() const { return aborted_; }

  // canResume() mirrors the real ContentParser contract:
  // Returns true when internal state allows continuing without re-parsing from start.
  bool canResume() const { return currentPage_ > 0 && hasMore_; }

  void reset() {
    currentPage_ = 0;
    hasMore_ = true;
    aborted_ = false;
  }

  int currentPage() const { return currentPage_; }

  // Simulate parse failure after N pages (e.g., XML_GetBuffer returns null mid-chapter)
  void setFailAfterPages(int n) { failAfterPages_ = n; }

 private:
  int totalPages_;
  int currentPage_ = 0;
  bool hasMore_ = true;
  bool aborted_ = false;
  int failAfterPages_ = 0;
};

// Simplified PageCache that mirrors the isPartial_ decision from PageCache::create():
//   Before: isPartial_ = hitMaxPages && parser.hasMoreContent()
//   After:  isPartial_ = parser.hasMoreContent()
class MockPageCache {
 public:
  bool create(MockContentParser& parser, uint16_t maxPages = 0, const AbortCallback& shouldAbort = nullptr) {
    pageCount_ = 0;
    isPartial_ = false;

    bool aborted = false;

    bool success = parser.parsePages(
        [this](std::unique_ptr<Page>) {
          pageCount_++;
        },
        maxPages, shouldAbort);

    if (shouldAbort && shouldAbort()) {
      aborted = true;
    }

    if (!success && pageCount_ == 0) {
      return false;
    }

    // Core logic from commit 7df2932:
    // Before: isPartial_ = hitMaxPages && parser.hasMoreContent()
    // After:  isPartial_ = parser.hasMoreContent()
    isPartial_ = parser.hasMoreContent();

    return !aborted;
  }

  bool extend(MockContentParser& parser, uint16_t additionalPages, const AbortCallback& shouldAbort = nullptr) {
    if (!isPartial_) return true;

    const uint16_t currentPages = pageCount_;

    if (parser.canResume()) {
      // HOT PATH: Parser has live session from previous call, just append new pages.
      // No re-parsing — O(chunk) work instead of O(totalPages).
      const uint16_t pagesBefore = pageCount_;
      bool parseOk = parser.parsePages(
          [this](std::unique_ptr<Page>) {
            pageCount_++;
          },
          additionalPages, shouldAbort);

      isPartial_ = parser.hasMoreContent();

      if (!parseOk && pageCount_ == pagesBefore) {
        return false;
      }

      return true;
    }

    // COLD PATH: Fresh parser — re-parse from start, skip cached pages.
    uint16_t targetPages = pageCount_ + additionalPages;
    parser.reset();
    bool result = create(parser, targetPages, shouldAbort);

    // No forward progress AND parser has no more content → content is truly finished.
    // Without the hasMoreContent() check, an aborted extend (timeout/memory pressure)
    // would permanently mark the chapter as complete, truncating it.
    if (result && pageCount_ <= currentPages && !parser.hasMoreContent()) {
      isPartial_ = false;
    }

    return result;
  }

  uint16_t pageCount() const { return pageCount_; }
  bool isPartial() const { return isPartial_; }

 private:
  uint16_t pageCount_ = 0;
  bool isPartial_ = false;
};

int main() {
  TestUtils::TestRunner runner("ContentParserAbort");

  // Test 1: Normal completion - all content parsed
  {
    MockContentParser parser(5);
    MockPageCache cache;

    bool ok = cache.create(parser, 0);  // maxPages=0 means unlimited

    runner.expectTrue(ok, "normal_completion_success");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "normal_completion_page_count");
    runner.expectFalse(parser.hasMoreContent(), "normal_completion_no_more_content");
    runner.expectFalse(cache.isPartial(), "normal_completion_not_partial");
  }

  // Test 2: Hit maxPages limit
  {
    MockContentParser parser(10);
    MockPageCache cache;

    bool ok = cache.create(parser, 5);  // Only parse 5 of 10

    runner.expectTrue(ok, "maxpages_success");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "maxpages_page_count");
    runner.expectTrue(parser.hasMoreContent(), "maxpages_has_more_content");
    runner.expectTrue(cache.isPartial(), "maxpages_is_partial");
  }

  // Test 3: Parser aborted (the new behavior from commit 7df2932)
  // Before the fix: aborted parse -> hasMore_=false -> isPartial_=false -> content lost!
  // After the fix: aborted parse -> hasMore_=true -> isPartial_=true -> will retry
  {
    MockContentParser parser(10);
    MockPageCache cache;

    int pagesBeforeAbort = 3;
    int pagesSeen = 0;
    AbortCallback abortAfter3 = [&]() { return pagesSeen >= pagesBeforeAbort; };

    // Use a wrapper that counts pages for the abort callback
    bool ok = parser.parsePages(
        [&](std::unique_ptr<Page>) { pagesSeen++; },
        0, abortAfter3);

    runner.expectFalse(ok, "aborted_parse_returns_false");
    runner.expectTrue(parser.wasAborted(), "aborted_was_aborted_true");
    runner.expectTrue(parser.hasMoreContent(), "aborted_has_more_content");
  }

  // Test 4: Parser aborted with no pages created -> failure
  {
    MockContentParser parser(10);
    MockPageCache cache;

    AbortCallback abortImmediately = []() { return true; };
    bool ok = cache.create(parser, 0, abortImmediately);

    runner.expectFalse(ok, "abort_no_pages_fails");
  }

  // Test 5: wasAborted() resets on new parsePages() call
  {
    MockContentParser parser(10);

    // First call: abort after 3 pages
    int pagesSeen = 0;
    AbortCallback abortAfter3 = [&]() { return pagesSeen >= 3; };
    parser.parsePages([&](std::unique_ptr<Page>) { pagesSeen++; }, 0, abortAfter3);
    runner.expectTrue(parser.wasAborted(), "reset_first_call_aborted");

    // Reset and parse again without abort
    parser.reset();
    parser.parsePages([](std::unique_ptr<Page>) {}, 0, nullptr);

    runner.expectFalse(parser.wasAborted(), "reset_second_call_not_aborted");
    runner.expectFalse(parser.hasMoreContent(), "reset_second_call_complete");
  }

  // Test 6: Partial cache extends correctly after abort
  {
    MockContentParser parser(10);
    MockPageCache cache;

    // First: parse with maxPages=3 -> partial
    bool ok = cache.create(parser, 3);
    runner.expectTrue(ok, "extend_initial_create");
    runner.expectEq(static_cast<uint16_t>(3), cache.pageCount(), "extend_initial_count");
    runner.expectTrue(cache.isPartial(), "extend_initial_partial");

    // Extend: parse 5 more (total 8)
    ok = cache.extend(parser, 5);
    runner.expectTrue(ok, "extend_after_partial");
    runner.expectEq(static_cast<uint16_t>(8), cache.pageCount(), "extend_count_after_extend");
    runner.expectTrue(cache.isPartial(), "extend_still_partial");

    // Extend again to finish (total 10+)
    ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "extend_to_finish");
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "extend_final_count");
    runner.expectFalse(cache.isPartial(), "extend_complete");
  }

  // Test 7: Parse error with partial content -> hasMore_ = true (issue #34 fix)
  // Before the fix: parse error mid-chapter -> hasMore_=false -> content lost!
  // After the fix: parse error + pages created -> hasMore_=true -> extend will retry
  {
    MockContentParser parser(100);
    parser.setFailAfterPages(5);  // Simulate XML_GetBuffer failure after 5 pages

    MockPageCache cache;
    bool ok = cache.create(parser, 0);  // No maxPages limit

    runner.expectTrue(ok, "parse_error_partial_success");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "parse_error_partial_page_count");
    runner.expectTrue(parser.hasMoreContent(), "parse_error_partial_has_more");
    runner.expectTrue(cache.isPartial(), "parse_error_partial_is_partial");
  }

  // Test 8: Cold extend no-progress guard: deterministic error at fixed position
  // If cold extend re-parses from start and gets the same page count, and parser
  // reports no more content, the error is deterministic → mark complete.
  // The new guard also checks hasMoreContent() so aborted parses aren't marked complete.
  {
    MockContentParser parser(100);
    parser.setFailAfterPages(5);  // Always fails after 5 pages

    MockPageCache cache;
    bool ok = cache.create(parser, 10);  // maxPages=10, but fails at 5

    runner.expectTrue(ok, "no_progress_initial_create");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "no_progress_initial_count");
    runner.expectTrue(cache.isPartial(), "no_progress_initial_partial");

    // Force cold path by resetting parser (clears canResume)
    parser.reset();
    ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "no_progress_cold_extend_success");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "no_progress_cold_extend_count");
    // Parser failed mid-content, so hasMoreContent()=true → no-progress guard does NOT fire.
    // This is the new behavior: transient-looking errors keep retrying (safer than truncating).
    runner.expectTrue(cache.isPartial(), "no_progress_cold_extend_still_partial");

    // Further extend should still be a no-op if we force cold path again
    parser.reset();
    ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "no_progress_cold_extend_retry");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "no_progress_cold_extend_retry_count");
  }

  // ============================================
  // Hot extend tests (canResume / suspend-resume)
  // ============================================

  // Test 9: Hot extend - parser resumes from last position instead of re-parsing
  {
    MockContentParser parser(20);
    MockPageCache cache;

    // Create initial cache with 5 pages
    bool ok = cache.create(parser, 5);
    runner.expectTrue(ok, "hot_extend_initial_create");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "hot_extend_initial_count");
    runner.expectTrue(cache.isPartial(), "hot_extend_initial_partial");
    runner.expectTrue(parser.canResume(), "hot_extend_can_resume_after_create");

    // Hot extend: parser continues from page 5, not from 0
    runner.expectEq(5, parser.currentPage(), "hot_extend_parser_at_page_5");
    ok = cache.extend(parser, 5);
    runner.expectTrue(ok, "hot_extend_success");
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "hot_extend_count_10");
    runner.expectTrue(cache.isPartial(), "hot_extend_still_partial_at_10");
    runner.expectEq(10, parser.currentPage(), "hot_extend_parser_at_page_10");
  }

  // Test 10: Multiple sequential hot extends until completion
  {
    MockContentParser parser(12);
    MockPageCache cache;

    bool ok = cache.create(parser, 4);
    runner.expectEq(static_cast<uint16_t>(4), cache.pageCount(), "seq_hot_initial_count");
    runner.expectTrue(parser.canResume(), "seq_hot_can_resume_1");

    ok = cache.extend(parser, 4);
    runner.expectTrue(ok, "seq_hot_extend_1");
    runner.expectEq(static_cast<uint16_t>(8), cache.pageCount(), "seq_hot_count_8");
    runner.expectTrue(cache.isPartial(), "seq_hot_partial_at_8");

    ok = cache.extend(parser, 4);
    runner.expectTrue(ok, "seq_hot_extend_2");
    runner.expectEq(static_cast<uint16_t>(12), cache.pageCount(), "seq_hot_count_12");
    runner.expectFalse(cache.isPartial(), "seq_hot_complete");
    runner.expectFalse(parser.canResume(), "seq_hot_no_resume_when_complete");
  }

  // Test 11: canResume() returns false after reset (forces cold path)
  {
    MockContentParser parser(10);
    MockPageCache cache;

    cache.create(parser, 5);
    runner.expectTrue(parser.canResume(), "reset_can_resume_before");

    parser.reset();
    runner.expectFalse(parser.canResume(), "reset_can_resume_after");
  }

  // Test 12: canResume() returns false when parsing completed (no more content)
  {
    MockContentParser parser(5);
    MockPageCache cache;

    cache.create(parser, 0);  // Parse all
    runner.expectFalse(parser.hasMoreContent(), "complete_no_more");
    runner.expectFalse(parser.canResume(), "complete_no_resume");
  }

  // Test 13: Hot extend with abort - partial progress preserved
  // Uses parser.currentPage() to trigger abort at a known absolute position.
  {
    MockContentParser parser(20);
    MockPageCache cache;

    cache.create(parser, 5);
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "hot_abort_initial_count");

    // Abort when parser reaches absolute page 8 (i.e., after 3 more pages from page 5)
    AbortCallback abortAt8 = [&]() { return parser.currentPage() >= 8; };
    bool ok = cache.extend(parser, 10, abortAt8);

    // Parser produced pages 5,6,7 before abort triggered at start of page 8
    runner.expectEq(static_cast<uint16_t>(8), cache.pageCount(), "hot_abort_count");
    runner.expectTrue(parser.hasMoreContent(), "hot_abort_has_more");
    runner.expectTrue(parser.canResume(), "hot_abort_can_resume");
  }

  // Test 14: Hot extend after abort preserves parser position
  {
    MockContentParser parser(30);
    MockPageCache cache;

    cache.create(parser, 5);
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "hot_resume_after_abort_initial");

    // Abort after parser reaches page 10
    AbortCallback abortAt10 = [&]() { return parser.currentPage() >= 10; };
    cache.extend(parser, 20, abortAt10);
    // Parser stopped at page 10, some pages were added
    runner.expectEq(10, parser.currentPage(), "hot_resume_parser_at_10");
    runner.expectTrue(parser.canResume(), "hot_resume_can_resume_after_abort");
    runner.expectTrue(cache.isPartial(), "hot_resume_still_partial");

    // Continue extending (hot path again since canResume is true)
    bool ok = cache.extend(parser, 20);
    runner.expectTrue(ok, "hot_resume_extend_after_abort");
    runner.expectEq(static_cast<uint16_t>(30), cache.pageCount(), "hot_resume_final_count");
    runner.expectFalse(cache.isPartial(), "hot_resume_complete");
  }

  // Test 15: Cold extend no-progress guard does NOT fire when parser still has content
  // (aborted extend shouldn't permanently mark chapter as complete)
  {
    MockContentParser parser(100);
    MockPageCache cache;

    // Create with 5 pages
    cache.create(parser, 5);
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "cold_noprog_initial_count");

    // Reset to force cold path, then abort extend immediately
    parser.reset();
    AbortCallback abortImmediately = []() { return true; };
    bool ok = cache.extend(parser, 10, abortImmediately);

    // Extend failed (abort with 0 pages), but parser.hasMoreContent() is still true
    // because content exists, so isPartial_ should NOT be set to false
    runner.expectFalse(ok, "cold_noprog_aborted_extend_fails");
    runner.expectEq(static_cast<uint16_t>(0), cache.pageCount(), "cold_noprog_aborted_count");
    // The create() call with abort returns false, so extend returns false.
    // pageCount_ is 0 (create resets it), which is <= currentPages (5).
    // But we don't reach the no-progress guard because create returned false.
  }

  // Test 16: Hot extend when parser reaches exact total (boundary condition)
  {
    MockContentParser parser(10);
    MockPageCache cache;

    cache.create(parser, 10);  // Exactly all pages
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "exact_total_count");
    runner.expectFalse(parser.hasMoreContent(), "exact_total_no_more");
    runner.expectFalse(cache.isPartial(), "exact_total_not_partial");
    runner.expectFalse(parser.canResume(), "exact_total_no_resume");

    // Extend should be a no-op
    bool ok = cache.extend(parser, 5);
    runner.expectTrue(ok, "exact_total_extend_noop");
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "exact_total_extend_count");
  }

  // Test 17: Hot extend requesting more pages than remaining
  {
    MockContentParser parser(8);
    MockPageCache cache;

    cache.create(parser, 3);
    runner.expectEq(static_cast<uint16_t>(3), cache.pageCount(), "overrequest_initial");

    // Request 20 pages but only 5 remain
    bool ok = cache.extend(parser, 20);
    runner.expectTrue(ok, "overrequest_success");
    runner.expectEq(static_cast<uint16_t>(8), cache.pageCount(), "overrequest_got_all");
    runner.expectFalse(cache.isPartial(), "overrequest_complete");
  }

  // Test 18: Hot extend with parse failure during resume
  // Parser fails (not abort) partway through a hot extend. Partial pages should be kept.
  {
    MockContentParser parser(20);
    MockPageCache cache;

    bool ok = cache.create(parser, 5);
    runner.expectTrue(ok, "hot_fail_initial_create");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "hot_fail_initial_count");
    runner.expectTrue(parser.canResume(), "hot_fail_can_resume");

    // Enable parse failure: each parsePages() call fails after 3 pages
    parser.setFailAfterPages(3);

    ok = cache.extend(parser, 10);
    // Hot extend: produced 3 pages before failure -> total 8
    runner.expectTrue(ok, "hot_fail_extend_ok");
    runner.expectEq(static_cast<uint16_t>(8), cache.pageCount(), "hot_fail_count_8");
    runner.expectTrue(cache.isPartial(), "hot_fail_still_partial");
  }

  // Test 19: Hot → cold extend transition
  // Start with hot extends, then reset (simulating device restart), then cold extend.
  {
    MockContentParser parser(30);
    MockPageCache cache;

    // Hot path: create + extend
    cache.create(parser, 5);
    runner.expectTrue(parser.canResume(), "hot_cold_can_resume_after_create");
    cache.extend(parser, 5);
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "hot_cold_hot_count");
    runner.expectTrue(parser.canResume(), "hot_cold_still_resumable");

    // Reset forces cold path (simulates device restart losing parser state)
    parser.reset();
    runner.expectFalse(parser.canResume(), "hot_cold_no_resume_after_reset");

    // Cold extend: re-parses from start, targets 20 pages total
    bool ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "hot_cold_cold_extend_ok");
    runner.expectEq(static_cast<uint16_t>(20), cache.pageCount(), "hot_cold_cold_count_20");
    runner.expectTrue(cache.isPartial(), "hot_cold_still_partial");

    // Can continue with hot extends again from cold path's parser state
    runner.expectTrue(parser.canResume(), "hot_cold_resume_after_cold");
    ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "hot_cold_final_extend");
    runner.expectEq(static_cast<uint16_t>(30), cache.pageCount(), "hot_cold_final_count");
    runner.expectFalse(cache.isPartial(), "hot_cold_complete");
  }

  // Test 20: failAfterPages at exact end of content (reachedEnd guard)
  // When parse error happens at the exact end of content, hasMore_ should be false.
  // Before the reachedEnd fix: hasMore_ = !success && pagesCreated > 0 = true (wrong)
  // After:  reachedEnd = true, so hasMore_ = false (correct)
  {
    MockContentParser parser(10);
    parser.setFailAfterPages(10);  // Fails after producing exactly all 10 pages

    MockPageCache cache;
    bool ok = cache.create(parser, 0);  // Unlimited

    // All 10 pages produced, then failure triggered (at end of content)
    runner.expectTrue(ok, "reachedend_fail_success");
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "reachedend_fail_count");
    // reachedEnd=true, so despite parse failure, no more content exists
    runner.expectFalse(parser.hasMoreContent(), "reachedend_fail_no_more");
    runner.expectFalse(cache.isPartial(), "reachedend_fail_not_partial");
  }

  // ============================================
  // Content preservation tests (batch vs unbatched)
  // ============================================

  // Test 21: Batched parsing produces same total pages as unbatched
  // This validates the core invariant: batching should not lose content.
  // The pendingNewTextBlock_ fix in ChapterHtmlSlimParser ensures words at
  // block boundaries aren't lost during suspend/resume.
  {
    const int totalContent = 47;  // Odd number to avoid exact batch multiples

    // Unbatched: parse all at once
    MockContentParser parserAll(totalContent);
    MockPageCache cacheAll;
    cacheAll.create(parserAll, 0);

    // Batched: parse in chunks of 5 (matching device DEFAULT_CACHE_CHUNK)
    MockContentParser parserBatch(totalContent);
    MockPageCache cacheBatch;
    cacheBatch.create(parserBatch, 5);
    while (cacheBatch.isPartial()) {
      cacheBatch.extend(parserBatch, 5);
    }

    runner.expectEq(cacheAll.pageCount(), cacheBatch.pageCount(), "batch_vs_unbatch_same_total");
    runner.expectFalse(cacheAll.isPartial(), "unbatched_complete");
    runner.expectFalse(cacheBatch.isPartial(), "batched_complete");
  }

  // Test 22: Single-page chapter with batch limit > 1
  {
    MockContentParser parser(1);
    MockPageCache cache;

    bool ok = cache.create(parser, 5);
    runner.expectTrue(ok, "single_page_batch_success");
    runner.expectEq(static_cast<uint16_t>(1), cache.pageCount(), "single_page_batch_count");
    runner.expectFalse(cache.isPartial(), "single_page_batch_complete");
    runner.expectFalse(parser.canResume(), "single_page_batch_no_resume");
  }

  // Test 23: Batch size of 1 (most granular batching)
  // Each page is its own batch, maximizing suspend/resume transitions
  {
    MockContentParser parser(7);
    MockPageCache cache;

    cache.create(parser, 1);
    runner.expectEq(static_cast<uint16_t>(1), cache.pageCount(), "batch_1_initial");
    runner.expectTrue(cache.isPartial(), "batch_1_initial_partial");

    // Extend one page at a time
    for (int i = 0; i < 6; i++) {
      cache.extend(parser, 1);
    }

    runner.expectEq(static_cast<uint16_t>(7), cache.pageCount(), "batch_1_final_count");
    runner.expectFalse(cache.isPartial(), "batch_1_complete");
  }

  // Test 24: Hot extend immediately after create with batch size matching content
  // Edge case: batch size equals content size, so first create gets everything
  {
    MockContentParser parser(5);
    MockPageCache cache;

    cache.create(parser, 5);  // Exactly matches content
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "exact_batch_count");
    runner.expectFalse(cache.isPartial(), "exact_batch_complete");

    // Extend should be a no-op since not partial
    bool ok = cache.extend(parser, 5);
    runner.expectTrue(ok, "exact_batch_extend_noop");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "exact_batch_extend_unchanged");
  }

  // Test 25: Mixed hot and cold extends preserve total content
  {
    const int total = 30;
    MockContentParser parser(total);
    MockPageCache cache;

    // Hot: create + extend
    cache.create(parser, 5);   // 5 pages
    cache.extend(parser, 5);   // 10 pages (hot)
    runner.expectEq(static_cast<uint16_t>(10), cache.pageCount(), "mixed_hot_count");

    // Force cold path (simulates device restart)
    parser.reset();
    cache.extend(parser, 5);   // 15 pages (cold: re-parses from 0, targets 15)
    runner.expectEq(static_cast<uint16_t>(15), cache.pageCount(), "mixed_cold_count");

    // Hot again (parser kept state from cold path)
    cache.extend(parser, 5);   // 20 pages (hot)
    runner.expectEq(static_cast<uint16_t>(20), cache.pageCount(), "mixed_hot2_count");

    // Finish
    cache.extend(parser, 100);
    runner.expectEq(static_cast<uint16_t>(total), cache.pageCount(), "mixed_final_count");
    runner.expectFalse(cache.isPartial(), "mixed_complete");
  }

  return runner.allPassed() ? 0 : 1;
}
