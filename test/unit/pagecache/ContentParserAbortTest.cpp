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
    // hitMaxPages: stopped at page limit, more content exists
    // aborted_: stopped due to timeout/memory, more content exists
    // !success && pagesCreated > 0: parse error mid-chapter, partial content exists
    hasMore_ = hitMaxPages || aborted_ || (!success && pagesCreated > 0);

    return success;
  }

  bool hasMoreContent() const { return hasMore_; }
  bool wasAborted() const { return aborted_; }

  void reset() {
    currentPage_ = 0;
    hasMore_ = true;
    aborted_ = false;
  }

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
    uint16_t targetPages = pageCount_ + additionalPages;
    parser.reset();
    bool result = create(parser, targetPages, shouldAbort);

    // No forward progress → deterministic error, stop retrying
    if (result && pageCount_ <= currentPages) {
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

  // Test 8: Extend after deterministic parse error should stop retrying (no-progress guard)
  // If extend() re-parses and gets the same page count, the error is deterministic.
  // The cache should mark itself complete to prevent infinite extend loops.
  {
    MockContentParser parser(100);
    parser.setFailAfterPages(5);  // Always fails after 5 pages

    MockPageCache cache;
    bool ok = cache.create(parser, 10);  // maxPages=10, but fails at 5

    runner.expectTrue(ok, "no_progress_initial_create");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "no_progress_initial_count");
    runner.expectTrue(cache.isPartial(), "no_progress_initial_partial");

    // Extend: re-parses from start, hits same error at page 5 → no progress
    ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "no_progress_extend_success");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "no_progress_extend_count");
    runner.expectFalse(cache.isPartial(), "no_progress_extend_not_partial");

    // Further extend should be a no-op since isPartial is now false
    ok = cache.extend(parser, 10);
    runner.expectTrue(ok, "no_progress_extend_noop");
    runner.expectEq(static_cast<uint16_t>(5), cache.pageCount(), "no_progress_extend_noop_count");
  }

  return runner.allPassed() ? 0 : 1;
}
