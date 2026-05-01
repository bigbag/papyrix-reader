#include "test_utils.h"

#include <cstdint>

// ============================================================================
// Inline dependencies to avoid linker issues (following project pattern)
// ============================================================================

// ContentType enum from src/core/Types.h
enum class ContentType : uint8_t {
  None = 0,
  Epub,
  Xtc,
  Txt,
  Markdown,
  Fb2,
};

// Mock PageCache - minimal interface needed for navigation tests
class PageCache {
 public:
  PageCache(int pageCount, bool partial) : pageCount_(pageCount), partial_(partial) {}

  int pageCount() const { return pageCount_; }
  bool isPartial() const { return partial_; }
  bool needsExtension(int) const { return partial_; }

  void setPageCount(int count) { pageCount_ = count; }
  void setPartial(bool partial) { partial_ = partial; }

 private:
  int pageCount_;
  bool partial_;
};

// ============================================================================
// Inline ReaderNavigation implementation from src/content/ReaderNavigation.cpp
// ============================================================================

class ReaderNavigation {
 public:
  struct Position {
    int spineIndex = 0;
    int sectionPage = 0;
    uint32_t flatPage = 0;
  };

  struct NavResult {
    Position position;
    bool needsRender = false;
    bool needsCacheReset = false;
  };

  static NavResult next(ContentType type, const Position& current, const PageCache* cache, uint32_t totalPages) {
    NavResult result;
    result.position = current;
    result.needsRender = false;
    result.needsCacheReset = false;

    const int pageCount = cache ? cache->pageCount() : 0;

    if (type == ContentType::Xtc) {
      if (current.flatPage + 1 < totalPages) {
        result.position.flatPage = current.flatPage + 1;
        result.needsRender = true;
      }
    } else if (type == ContentType::Epub || type == ContentType::Fb2) {
      if (pageCount > 0 && current.sectionPage < pageCount - 1) {
        result.position.sectionPage = current.sectionPage + 1;
        result.needsRender = true;
      } else if (cache && cache->isPartial()) {
        result.position.sectionPage = current.sectionPage + 1;
        result.needsRender = true;
      } else if (pageCount > 0 && current.sectionPage >= pageCount - 1) {
        result.position.spineIndex = current.spineIndex + 1;
        result.position.sectionPage = 0;
        result.needsCacheReset = true;
        result.needsRender = true;
      }
    } else {
      if (pageCount > 0 && current.sectionPage < pageCount - 1) {
        result.position.sectionPage = current.sectionPage + 1;
        result.needsRender = true;
      } else if (cache && cache->isPartial()) {
        result.position.sectionPage = current.sectionPage + 1;
        result.needsRender = true;
      }
    }

    return result;
  }

  static NavResult prev(ContentType type, const Position& current, const PageCache* cache) {
    (void)cache;  // Used only for potential future extensions

    NavResult result;
    result.position = current;
    result.needsRender = false;
    result.needsCacheReset = false;

    if (type == ContentType::Xtc) {
      if (current.flatPage > 0) {
        result.position.flatPage = current.flatPage - 1;
        result.needsRender = true;
      }
    } else if (type == ContentType::Epub || type == ContentType::Fb2) {
      if (current.sectionPage > 0) {
        result.position.sectionPage = current.sectionPage - 1;
        result.needsRender = true;
      } else if (current.spineIndex > 0) {
        result.position.spineIndex = current.spineIndex - 1;
        result.position.sectionPage = INT16_MAX;
        result.needsCacheReset = true;
        result.needsRender = true;
      }
    } else {
      if (current.sectionPage > 0) {
        result.position.sectionPage = current.sectionPage - 1;
        result.needsRender = true;
      }
    }

    return result;
  }
};

// ============================================================================
// Tests
// ============================================================================

int main() {
  TestUtils::TestRunner runner("ReaderNavigation");

  // ============================================
  // XTC Navigation Tests (new code path)
  // ============================================

  // Test 1: XTC next() - advances flatPage when not at end
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 5;
    auto result = ReaderNavigation::next(ContentType::Xtc, pos, nullptr, 10);
    runner.expectEq(6u, result.position.flatPage, "XTC next: advances flatPage from 5 to 6");
    runner.expectTrue(result.needsRender, "XTC next: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "XTC next: needsCacheReset is false");
  }

  // Test 2: XTC next() - stays at last page (boundary)
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 9;
    auto result = ReaderNavigation::next(ContentType::Xtc, pos, nullptr, 10);
    runner.expectEq(9u, result.position.flatPage, "XTC next: stays at page 9 (last page)");
    runner.expectFalse(result.needsRender, "XTC next: needsRender is false at boundary");
  }

  // Test 3: XTC next() - page 0 to page 1
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 0;
    auto result = ReaderNavigation::next(ContentType::Xtc, pos, nullptr, 100);
    runner.expectEq(1u, result.position.flatPage, "XTC next: advances from page 0 to 1");
    runner.expectTrue(result.needsRender, "XTC next: needsRender is true");
  }

  // Test 4: XTC prev() - decrements flatPage when not at start
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 5;
    auto result = ReaderNavigation::prev(ContentType::Xtc, pos, nullptr);
    runner.expectEq(4u, result.position.flatPage, "XTC prev: decrements flatPage from 5 to 4");
    runner.expectTrue(result.needsRender, "XTC prev: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "XTC prev: needsCacheReset is false");
  }

  // Test 5: XTC prev() - stays at first page (boundary)
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 0;
    auto result = ReaderNavigation::prev(ContentType::Xtc, pos, nullptr);
    runner.expectEq(0u, result.position.flatPage, "XTC prev: stays at page 0 (first page)");
    runner.expectFalse(result.needsRender, "XTC prev: needsRender is false at boundary");
  }

  // Test 6: XTC prev() - page 1 to page 0
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 1;
    auto result = ReaderNavigation::prev(ContentType::Xtc, pos, nullptr);
    runner.expectEq(0u, result.position.flatPage, "XTC prev: decrements from page 1 to 0");
    runner.expectTrue(result.needsRender, "XTC prev: needsRender is true");
  }

  // ============================================
  // EPUB Navigation Tests
  // ============================================

  // Test 7: EPUB next() - advances sectionPage within chapter
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 2;
    pos.sectionPage = 3;
    auto result = ReaderNavigation::next(ContentType::Epub, pos, &cache, 0);
    runner.expectEq(2, result.position.spineIndex, "EPUB next: spineIndex unchanged");
    runner.expectEq(4, result.position.sectionPage, "EPUB next: sectionPage advances from 3 to 4");
    runner.expectTrue(result.needsRender, "EPUB next: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "EPUB next: needsCacheReset is false within chapter");
  }

  // Test 8: EPUB next() - moves to next chapter when at end of section (cache complete)
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 2;
    pos.sectionPage = 9;  // Last page (0-indexed)
    auto result = ReaderNavigation::next(ContentType::Epub, pos, &cache, 0);
    runner.expectEq(3, result.position.spineIndex, "EPUB next: spineIndex advances to 3");
    runner.expectEq(0, result.position.sectionPage, "EPUB next: sectionPage resets to 0");
    runner.expectTrue(result.needsRender, "EPUB next: needsRender is true");
    runner.expectTrue(result.needsCacheReset, "EPUB next: needsCacheReset is true for chapter change");
  }

  // Test 9: EPUB next() - extends cache when partial
  {
    PageCache cache(5, true);  // Partial cache with 5 pages
    ReaderNavigation::Position pos;
    pos.spineIndex = 1;
    pos.sectionPage = 4;  // At last cached page
    auto result = ReaderNavigation::next(ContentType::Epub, pos, &cache, 0);
    runner.expectEq(1, result.position.spineIndex, "EPUB next partial: spineIndex unchanged");
    runner.expectEq(5, result.position.sectionPage, "EPUB next partial: sectionPage advances to trigger extension");
    runner.expectTrue(result.needsRender, "EPUB next partial: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "EPUB next partial: needsCacheReset is false (cache extends, not resets)");
  }

  // Test 10: EPUB prev() - decrements sectionPage within chapter
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 2;
    pos.sectionPage = 5;
    auto result = ReaderNavigation::prev(ContentType::Epub, pos, &cache);
    runner.expectEq(2, result.position.spineIndex, "EPUB prev: spineIndex unchanged");
    runner.expectEq(4, result.position.sectionPage, "EPUB prev: sectionPage decrements from 5 to 4");
    runner.expectTrue(result.needsRender, "EPUB prev: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "EPUB prev: needsCacheReset is false within chapter");
  }

  // Test 11: EPUB prev() - moves to previous chapter from sectionPage 0
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 3;
    pos.sectionPage = 0;
    auto result = ReaderNavigation::prev(ContentType::Epub, pos, &cache);
    runner.expectEq(2, result.position.spineIndex, "EPUB prev: spineIndex decrements to 2");
    runner.expectEq(static_cast<int>(INT16_MAX), result.position.sectionPage,
                    "EPUB prev: sectionPage set to INT16_MAX (clamped later)");
    runner.expectTrue(result.needsRender, "EPUB prev: needsRender is true");
    runner.expectTrue(result.needsCacheReset, "EPUB prev: needsCacheReset is true for chapter change");
  }

  // Test 12: EPUB prev() - stays at first chapter, first page
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 0;
    pos.sectionPage = 0;
    auto result = ReaderNavigation::prev(ContentType::Epub, pos, &cache);
    runner.expectEq(0, result.position.spineIndex, "EPUB prev: spineIndex stays at 0");
    runner.expectEq(0, result.position.sectionPage, "EPUB prev: sectionPage stays at 0");
    runner.expectFalse(result.needsRender, "EPUB prev: needsRender is false at start of book");
  }

  // ============================================
  // TXT Navigation Tests
  // ============================================

  // Test 13: TXT next() - advances sectionPage
  {
    PageCache cache(20, false);
    ReaderNavigation::Position pos;
    pos.sectionPage = 7;
    auto result = ReaderNavigation::next(ContentType::Txt, pos, &cache, 0);
    runner.expectEq(8, result.position.sectionPage, "TXT next: sectionPage advances from 7 to 8");
    runner.expectTrue(result.needsRender, "TXT next: needsRender is true");
  }

  // Test 14: TXT next() - stops at end (no chapter concept)
  {
    PageCache cache(20, false);
    ReaderNavigation::Position pos;
    pos.sectionPage = 19;  // Last page
    auto result = ReaderNavigation::next(ContentType::Txt, pos, &cache, 0);
    runner.expectEq(19, result.position.sectionPage, "TXT next: stays at page 19 (last page)");
    runner.expectFalse(result.needsRender, "TXT next: needsRender is false at end");
  }

  // Test 15: TXT prev() - decrements sectionPage
  {
    PageCache cache(20, false);
    ReaderNavigation::Position pos;
    pos.sectionPage = 10;
    auto result = ReaderNavigation::prev(ContentType::Txt, pos, &cache);
    runner.expectEq(9, result.position.sectionPage, "TXT prev: sectionPage decrements from 10 to 9");
    runner.expectTrue(result.needsRender, "TXT prev: needsRender is true");
  }

  // Test 16: TXT prev() - stays at page 0
  {
    PageCache cache(20, false);
    ReaderNavigation::Position pos;
    pos.sectionPage = 0;
    auto result = ReaderNavigation::prev(ContentType::Txt, pos, &cache);
    runner.expectEq(0, result.position.sectionPage, "TXT prev: stays at page 0");
    runner.expectFalse(result.needsRender, "TXT prev: needsRender is false at start");
  }

  // ============================================
  // Markdown Navigation Tests
  // ============================================

  // Test 17: Markdown next() - advances sectionPage
  {
    PageCache cache(15, false);
    ReaderNavigation::Position pos;
    pos.sectionPage = 5;
    auto result = ReaderNavigation::next(ContentType::Markdown, pos, &cache, 0);
    runner.expectEq(6, result.position.sectionPage, "Markdown next: sectionPage advances from 5 to 6");
    runner.expectTrue(result.needsRender, "Markdown next: needsRender is true");
  }

  // Test 18: Markdown prev() - decrements sectionPage
  {
    PageCache cache(15, false);
    ReaderNavigation::Position pos;
    pos.sectionPage = 5;
    auto result = ReaderNavigation::prev(ContentType::Markdown, pos, &cache);
    runner.expectEq(4, result.position.sectionPage, "Markdown prev: sectionPage decrements from 5 to 4");
    runner.expectTrue(result.needsRender, "Markdown prev: needsRender is true");
  }

  // ============================================
  // Edge Cases
  // ============================================

  // Test 19: EPUB with null cache - no navigation happens
  {
    ReaderNavigation::Position pos;
    pos.spineIndex = 1;
    pos.sectionPage = 5;
    auto result = ReaderNavigation::next(ContentType::Epub, pos, nullptr, 0);
    runner.expectEq(1, result.position.spineIndex, "EPUB null cache: spineIndex unchanged");
    runner.expectEq(5, result.position.sectionPage, "EPUB null cache: sectionPage unchanged");
    runner.expectFalse(result.needsRender, "EPUB null cache: needsRender is false");
  }

  // Test 20: EPUB with empty cache (pageCount=0) - no navigation
  {
    PageCache cache(0, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 1;
    pos.sectionPage = 0;
    auto result = ReaderNavigation::next(ContentType::Epub, pos, &cache, 0);
    runner.expectFalse(result.needsRender, "EPUB empty cache: needsRender is false");
  }

  // Test 21: XTC single page book - next stays in place
  {
    ReaderNavigation::Position pos;
    pos.flatPage = 0;
    auto result = ReaderNavigation::next(ContentType::Xtc, pos, nullptr, 1);
    runner.expectEq(0u, result.position.flatPage, "XTC single page: next stays at page 0");
    runner.expectFalse(result.needsRender, "XTC single page: needsRender is false");
  }

  // Test 22: TXT with partial cache - triggers extension
  {
    PageCache cache(5, true);
    ReaderNavigation::Position pos;
    pos.sectionPage = 4;
    auto result = ReaderNavigation::next(ContentType::Txt, pos, &cache, 0);
    runner.expectEq(5, result.position.sectionPage, "TXT partial: sectionPage advances to trigger extension");
    runner.expectTrue(result.needsRender, "TXT partial: needsRender is true");
  }

  // ============================================
  // FB2 Navigation Tests
  // ============================================

  // Test 23: FB2 next() - advances sectionPage within section
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 2;
    pos.sectionPage = 3;
    auto result = ReaderNavigation::next(ContentType::Fb2, pos, &cache, 0);
    runner.expectEq(2, result.position.spineIndex, "FB2 next: spineIndex unchanged within section");
    runner.expectEq(4, result.position.sectionPage, "FB2 next: sectionPage advances from 3 to 4");
    runner.expectTrue(result.needsRender, "FB2 next: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "FB2 next: needsCacheReset is false within section");
  }

  // Test 24: FB2 next() - moves to next section at end of complete cache
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 2;
    pos.sectionPage = 9;
    auto result = ReaderNavigation::next(ContentType::Fb2, pos, &cache, 0);
    runner.expectEq(3, result.position.spineIndex, "FB2 next: spineIndex advances to next section");
    runner.expectEq(0, result.position.sectionPage, "FB2 next: sectionPage resets to 0");
    runner.expectTrue(result.needsRender, "FB2 next: needsRender is true");
    runner.expectTrue(result.needsCacheReset, "FB2 next: needsCacheReset is true for section change");
  }

  // Test 25: FB2 next() - extends cache when partial (no section change)
  {
    PageCache cache(5, true);
    ReaderNavigation::Position pos;
    pos.spineIndex = 1;
    pos.sectionPage = 4;
    auto result = ReaderNavigation::next(ContentType::Fb2, pos, &cache, 0);
    runner.expectEq(1, result.position.spineIndex, "FB2 next partial: spineIndex unchanged");
    runner.expectEq(5, result.position.sectionPage, "FB2 next partial: sectionPage advances to trigger extension");
    runner.expectTrue(result.needsRender, "FB2 next partial: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "FB2 next partial: needsCacheReset is false");
  }

  // Test 26: FB2 prev() - decrements sectionPage within section
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 2;
    pos.sectionPage = 5;
    auto result = ReaderNavigation::prev(ContentType::Fb2, pos, &cache);
    runner.expectEq(2, result.position.spineIndex, "FB2 prev: spineIndex unchanged within section");
    runner.expectEq(4, result.position.sectionPage, "FB2 prev: sectionPage decrements from 5 to 4");
    runner.expectTrue(result.needsRender, "FB2 prev: needsRender is true");
    runner.expectFalse(result.needsCacheReset, "FB2 prev: needsCacheReset is false within section");
  }

  // Test 27: FB2 prev() - moves to previous section from sectionPage 0
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 3;
    pos.sectionPage = 0;
    auto result = ReaderNavigation::prev(ContentType::Fb2, pos, &cache);
    runner.expectEq(2, result.position.spineIndex, "FB2 prev: spineIndex decrements to 2");
    runner.expectEq(static_cast<int>(INT16_MAX), result.position.sectionPage,
                    "FB2 prev: sectionPage set to INT16_MAX (clamped later)");
    runner.expectTrue(result.needsRender, "FB2 prev: needsRender is true");
    runner.expectTrue(result.needsCacheReset, "FB2 prev: needsCacheReset is true for section change");
  }

  // Test 28: FB2 prev() - stays at first section, first page
  {
    PageCache cache(10, false);
    ReaderNavigation::Position pos;
    pos.spineIndex = 0;
    pos.sectionPage = 0;
    auto result = ReaderNavigation::prev(ContentType::Fb2, pos, &cache);
    runner.expectEq(0, result.position.spineIndex, "FB2 prev: spineIndex stays at 0");
    runner.expectEq(0, result.position.sectionPage, "FB2 prev: sectionPage stays at 0");
    runner.expectFalse(result.needsRender, "FB2 prev: needsRender is false at start of book");
  }

  return runner.allPassed() ? 0 : 1;
}
