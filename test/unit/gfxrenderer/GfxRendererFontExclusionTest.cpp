#include "test_utils.h"

#include <cstdint>
#include <cstring>

// Inline the font exclusion list from GfxRenderer.h:48-58 and
// getEffectiveLineHeight logic from GfxRenderer.cpp:710-717.
// Also inline the notFound logic from ExternalFont.cpp:273.

// --- Font exclusion list (inlined from GfxRenderer) ---

static constexpr size_t MAX_EXCLUDED_FONT_IDS = 4;

struct FontExclusionList {
  int excludedIds[MAX_EXCLUDED_FONT_IDS] = {};
  int excludedCount = 0;

  void exclude(int fontId) {
    if (excludedCount < static_cast<int>(MAX_EXCLUDED_FONT_IDS))
      excludedIds[excludedCount++] = fontId;
  }

  bool isAllowed(int fontId) const {
    for (int i = 0; i < excludedCount; i++) {
      if (excludedIds[i] == fontId) return false;
    }
    return true;
  }
};

// --- getEffectiveLineHeight (inlined from GfxRenderer) ---

static int getEffectiveLineHeight(int builtinLineHeight, bool externalLoaded, bool externalAllowed,
                                  int extCharHeight) {
  int h = builtinLineHeight;
  if (externalAllowed && externalLoaded) {
    int extH = extCharHeight + 2;
    if (extH > h) h = extH;
  }
  return h;
}

// --- notFound logic (inlined from ExternalFont::getGlyph) ---

static bool computeNotFound(bool readSuccess, bool isEmpty, uint32_t codepoint) {
  bool isWhitespace = (codepoint >= 0x2000 && codepoint <= 0x200F) || codepoint == 0x3000;
  return !readSuccess || (isEmpty && !isWhitespace);
}

int main() {
  TestUtils::TestRunner runner("GfxRendererFontExclusion");

  // === Font exclusion list tests ===

  // 1. Empty list allows any fontId
  {
    FontExclusionList list;
    runner.expectTrue(list.isAllowed(42), "empty list: fontId 42 allowed");
    runner.expectTrue(list.isAllowed(0), "empty list: fontId 0 allowed");
  }

  // 2. Excluded font is blocked
  {
    FontExclusionList list;
    list.exclude(10);
    runner.expectFalse(list.isAllowed(10), "excluded font 10: blocked");
  }

  // 3. Non-excluded font still allowed
  {
    FontExclusionList list;
    list.exclude(10);
    runner.expectTrue(list.isAllowed(20), "non-excluded 20: still allowed");
  }

  // 4. Multiple exclusions - all blocked, others allowed
  {
    FontExclusionList list;
    list.exclude(1);
    list.exclude(2);
    list.exclude(3);
    runner.expectFalse(list.isAllowed(1), "multi-exclude: 1 blocked");
    runner.expectFalse(list.isAllowed(2), "multi-exclude: 2 blocked");
    runner.expectFalse(list.isAllowed(3), "multi-exclude: 3 blocked");
    runner.expectTrue(list.isAllowed(4), "multi-exclude: 4 allowed");
  }

  // 5. Max capacity (4 IDs) - 5th exclude() is silently ignored, no crash
  {
    FontExclusionList list;
    list.exclude(10);
    list.exclude(20);
    list.exclude(30);
    list.exclude(40);
    list.exclude(50);  // Should be silently ignored
    runner.expectEq(4, list.excludedCount, "max capacity: count stays at 4");
    runner.expectFalse(list.isAllowed(40), "max capacity: 4th excluded still blocked");
    runner.expectTrue(list.isAllowed(50), "max capacity: 5th was not added, still allowed");
  }

  // 6. Duplicate exclusion - fontId still blocked, count increments
  {
    FontExclusionList list;
    list.exclude(7);
    list.exclude(7);
    runner.expectEq(2, list.excludedCount, "duplicate: count increments (no dedup)");
    runner.expectFalse(list.isAllowed(7), "duplicate: still blocked");
  }

  // 7. FontId 0 works correctly
  {
    FontExclusionList list;
    list.exclude(0);
    runner.expectFalse(list.isAllowed(0), "fontId 0: excluded correctly");
    runner.expectTrue(list.isAllowed(1), "fontId 0: others still allowed");
  }

  // === getEffectiveLineHeight tests ===

  // 8. No external font loaded - returns builtin height
  {
    int h = getEffectiveLineHeight(20, false, true, 30);
    runner.expectEq(20, h, "no external loaded: returns builtin 20");
  }

  // 9. External font excluded for this fontId - returns builtin height
  {
    int h = getEffectiveLineHeight(20, true, false, 30);
    runner.expectEq(20, h, "external excluded: returns builtin 20");
  }

  // 10. External font taller than builtin - returns extCharHeight + 2
  {
    int h = getEffectiveLineHeight(20, true, true, 25);
    runner.expectEq(27, h, "external taller: returns 25+2=27");
  }

  // 11. External font shorter than builtin - returns builtin height
  {
    int h = getEffectiveLineHeight(20, true, true, 15);
    runner.expectEq(20, h, "external shorter: returns builtin 20 (15+2=17 < 20)");
  }

  // 12. External font exactly matching (builtinH == extCharHeight + 2)
  {
    int h = getEffectiveLineHeight(20, true, true, 18);
    runner.expectEq(20, h, "exact match: 18+2=20 == 20, returns builtin");
  }

  // === notFound fallthrough tests ===

  // 13. Read failed - always falls through (notFound = true)
  {
    runner.expectTrue(computeNotFound(false, false, 0x4E2D), "read failed, non-empty: notFound=true");
    runner.expectTrue(computeNotFound(false, true, 0x4E2D), "read failed, empty: notFound=true");
  }

  // 14. Empty non-whitespace glyph - falls through (notFound = true)
  //     This covers ASCII empty slots from CJK-only fonts
  {
    runner.expectTrue(computeNotFound(true, true, 'A'), "empty ASCII 'A': notFound=true (falls to builtin)");
    runner.expectTrue(computeNotFound(true, true, 0x4E2D), "empty CJK U+4E2D: notFound=true");
  }

  // 15. Empty whitespace glyph - does NOT fall through (notFound = false)
  {
    runner.expectFalse(computeNotFound(true, true, 0x2003), "empty em-space U+2003: notFound=false (rendered)");
    runner.expectFalse(computeNotFound(true, true, 0x2000), "empty U+2000: notFound=false");
    runner.expectFalse(computeNotFound(true, true, 0x200F), "empty U+200F: notFound=false");
    runner.expectFalse(computeNotFound(true, true, 0x3000), "empty ideographic space U+3000: notFound=false");
  }

  // 16. Non-empty glyph with successful read - normal (notFound = false)
  {
    runner.expectFalse(computeNotFound(true, false, 'A'), "non-empty ASCII: notFound=false");
    runner.expectFalse(computeNotFound(true, false, 0x4E2D), "non-empty CJK: notFound=false");
  }

  // 17. Whitespace boundary: U+1FFF is NOT whitespace, U+2010 is NOT whitespace
  {
    runner.expectTrue(computeNotFound(true, true, 0x1FFF), "empty U+1FFF: notFound=true (not in whitespace range)");
    runner.expectTrue(computeNotFound(true, true, 0x2010), "empty U+2010: notFound=true (not in whitespace range)");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
