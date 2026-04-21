#include "test_utils.h"

#include <cstdint>
#include <vector>

#include "ArabicCharacter.h"
#include "ArabicShaper.h"
#include "ArabicShapingTables.h"

using namespace ArabicShaper;

int main() {
  TestUtils::TestRunner runner("Arabic Shaper");

  // ============================================
  // Character classification tests
  // ============================================

  // Test 1: Diacritics (harakat)
  runner.expectTrue(isArabicDiacritic(0x064B), "diacritic: fathatan U+064B");
  runner.expectTrue(isArabicDiacritic(0x0650), "diacritic: kasra U+0650");
  runner.expectTrue(isArabicDiacritic(0x065F), "diacritic: wavy hamza U+065F");
  runner.expectTrue(isArabicDiacritic(0x0670), "diacritic: superscript alef U+0670");
  runner.expectFalse(isArabicDiacritic(0x0628), "diacritic: Beh is not diacritic");
  runner.expectFalse(isArabicDiacritic(0x0041), "diacritic: ASCII 'A' is not diacritic");

  // Test 2: Base characters
  runner.expectTrue(isArabicBaseChar(0x0621), "baseChar: Hamza U+0621");
  runner.expectTrue(isArabicBaseChar(0x0628), "baseChar: Beh U+0628");
  runner.expectTrue(isArabicBaseChar(0x064A), "baseChar: Yeh U+064A");
  runner.expectFalse(isArabicBaseChar(0x064B), "baseChar: diacritic not base");
  runner.expectFalse(isArabicBaseChar(0x0041), "baseChar: ASCII not base");

  // Test 3: Joining types
  runner.expectTrue(getJoiningType(0x0621) == JoiningType::NON_JOINING, "joining: Hamza is NON_JOINING");
  runner.expectTrue(getJoiningType(0x0627) == JoiningType::RIGHT_JOINING, "joining: Alef is RIGHT_JOINING");
  runner.expectTrue(getJoiningType(0x062F) == JoiningType::RIGHT_JOINING, "joining: Dal is RIGHT_JOINING");
  runner.expectTrue(getJoiningType(0x0631) == JoiningType::RIGHT_JOINING, "joining: Ra is RIGHT_JOINING");
  runner.expectTrue(getJoiningType(0x0648) == JoiningType::RIGHT_JOINING, "joining: Waw is RIGHT_JOINING");
  runner.expectTrue(getJoiningType(0x0628) == JoiningType::DUAL_JOINING, "joining: Beh is DUAL_JOINING");
  runner.expectTrue(getJoiningType(0x0633) == JoiningType::DUAL_JOINING, "joining: Seen is DUAL_JOINING");
  runner.expectTrue(getJoiningType(0x064A) == JoiningType::DUAL_JOINING, "joining: Yeh is DUAL_JOINING");
  runner.expectTrue(getJoiningType(0x064B) == JoiningType::TRANSPARENT, "joining: diacritic is TRANSPARENT");
  runner.expectTrue(getJoiningType(0x0041) == JoiningType::NON_JOINING, "joining: ASCII is NON_JOINING");

  // ============================================
  // Contextual form tests
  // ============================================

  // Test 4: Isolated form (no neighbors join)
  {
    uint32_t form = getContextualForm(0x0628, false, false);  // Beh isolated
    runner.expectEq(static_cast<uint32_t>(0xFE8F), form, "contextual: Beh isolated");
  }

  // Test 5: Initial form (next joins, prev doesn't)
  {
    uint32_t form = getContextualForm(0x0628, false, true);  // Beh initial
    runner.expectEq(static_cast<uint32_t>(0xFE91), form, "contextual: Beh initial");
  }

  // Test 6: Medial form (both join)
  {
    uint32_t form = getContextualForm(0x0628, true, true);  // Beh medial
    runner.expectEq(static_cast<uint32_t>(0xFE92), form, "contextual: Beh medial");
  }

  // Test 7: Final form (prev joins, next doesn't)
  {
    uint32_t form = getContextualForm(0x0628, true, false);  // Beh final
    runner.expectEq(static_cast<uint32_t>(0xFE90), form, "contextual: Beh final");
  }

  // Test 8: Right-joining char only has isolated and final
  {
    uint32_t isolated = getContextualForm(0x0627, false, false);  // Alef isolated
    runner.expectEq(static_cast<uint32_t>(0xFE8D), isolated, "contextual: Alef isolated");

    uint32_t final_ = getContextualForm(0x0627, true, false);  // Alef final
    runner.expectEq(static_cast<uint32_t>(0xFE8E), final_, "contextual: Alef final");

    // Alef has no initial/medial - should fall back to isolated/final
    uint32_t noInitial = getContextualForm(0x0627, false, true);
    runner.expectEq(static_cast<uint32_t>(0xFE8D), noInitial, "contextual: Alef no initial form");
  }

  // Test 9: Non-Arabic codepoint returns unchanged
  {
    uint32_t form = getContextualForm(0x0041, true, true);  // ASCII 'A'
    runner.expectEq(static_cast<uint32_t>(0x0041), form, "contextual: non-Arabic unchanged");
  }

  // ============================================
  // Lam-Alef ligature tests
  // ============================================

  // Test 10: Lam + Alef isolated
  {
    uint32_t lig = getLamAlefLigature(0x0627, false);  // Alef, no prev join
    runner.expectEq(static_cast<uint32_t>(0xFEFB), lig, "lamAlef: Lam+Alef isolated");
  }

  // Test 11: Lam + Alef final (prev joins)
  {
    uint32_t lig = getLamAlefLigature(0x0627, true);
    runner.expectEq(static_cast<uint32_t>(0xFEFC), lig, "lamAlef: Lam+Alef final");
  }

  // Test 12: Lam + Alef Madda
  {
    uint32_t lig = getLamAlefLigature(0x0622, false);
    runner.expectEq(static_cast<uint32_t>(0xFEF5), lig, "lamAlef: Lam+AlefMadda isolated");
  }

  // Test 13: Lam + Alef Hamza Above
  {
    uint32_t lig = getLamAlefLigature(0x0623, false);
    runner.expectEq(static_cast<uint32_t>(0xFEF7), lig, "lamAlef: Lam+AlefHamzaAbove isolated");
  }

  // Test 14: Non-Alef returns 0
  {
    uint32_t lig = getLamAlefLigature(0x0628, false);  // Beh is not Alef
    runner.expectEq(static_cast<uint32_t>(0), lig, "lamAlef: non-Alef returns 0");
  }

  // ============================================
  // Full shapeText() tests
  // ============================================

  // Test 15: Empty/null input
  {
    auto result = shapeText(nullptr);
    runner.expectTrue(result.empty(), "shapeText: null returns empty");

    auto result2 = shapeText("");
    runner.expectTrue(result2.empty(), "shapeText: empty returns empty");
  }

  // Test 16: Single isolated character (Beh)
  {
    // U+0628 Beh = UTF-8: 0xD8 0xA8
    auto result = shapeText("\xD8\xA8");
    runner.expectEq(static_cast<size_t>(1), result.size(), "shapeText: single char count");
    runner.expectEq(static_cast<uint32_t>(0xFE8F), result[0], "shapeText: Beh isolated");
  }

  // Test 17: Two connected letters (Ba-Alef = "ba")
  // Logical: Beh(0x0628) + Alef(0x0627) → visual (reversed): Alef-final, Beh-initial
  {
    auto result = shapeText("\xD8\xA8\xD8\xA7");  // Beh + Alef
    runner.expectEq(static_cast<size_t>(2), result.size(), "shapeText: two chars count");
    // Visual order is reversed: Alef comes first (was last in logical order)
    runner.expectEq(static_cast<uint32_t>(0xFE8E), result[0], "shapeText: Alef final (visual first)");
    runner.expectEq(static_cast<uint32_t>(0xFE91), result[1], "shapeText: Beh initial (visual second)");
  }

  // Test 18: Three-letter word with medial form
  // Logical: Beh(0628) + Seen(0633) + Meem(0645)
  // Joining: Beh(D) + Seen(D) + Meem(D) → initial + medial + final (isolated end)
  // Wait: Meem is last so nextJoins=false → Meem is final? No, there's nothing after.
  // Beh: prev=none, next=Seen(D) → initial
  // Seen: prev=Beh(D)→joinsLeft=true, next=Meem(D)→joinsRight=true → medial
  // Meem: prev=Seen(D)→joinsLeft=true, next=none → final
  // Visual (reversed): Meem-final, Seen-medial, Beh-initial
  {
    auto result = shapeText("\xD8\xA8\xD8\xB3\xD9\x85");  // Beh + Seen + Meem
    runner.expectEq(static_cast<size_t>(3), result.size(), "shapeText: three chars count");
    runner.expectEq(static_cast<uint32_t>(0xFEE2), result[0], "shapeText: Meem final");
    runner.expectEq(static_cast<uint32_t>(0xFEB4), result[1], "shapeText: Seen medial");
    runner.expectEq(static_cast<uint32_t>(0xFE91), result[2], "shapeText: Beh initial");
  }

  // Test 19: Lam-Alef ligature in shapeText
  // Logical: Lam(0644) + Alef(0627) → single ligature codepoint
  {
    auto result = shapeText("\xD9\x84\xD8\xA7");  // Lam + Alef
    runner.expectEq(static_cast<size_t>(1), result.size(), "shapeText: LamAlef ligature count");
    runner.expectEq(static_cast<uint32_t>(0xFEFB), result[0], "shapeText: LamAlef isolated");
  }

  // Test 20: Lam-Alef with preceding letter (final form)
  // Logical: Beh(0628) + Lam(0644) + Alef(0627)
  // Beh is dual-joining, so it joins left → LamAlef gets final form
  // Beh: prev=none, next=LamAlef-ligature... After ligature, Beh has next joining
  // Result should be: Beh-initial + LamAlef-final
  {
    auto result = shapeText("\xD8\xA8\xD9\x84\xD8\xA7");  // Beh + Lam + Alef
    runner.expectEq(static_cast<size_t>(2), result.size(), "shapeText: Beh+LamAlef count");
    runner.expectEq(static_cast<uint32_t>(0xFEFC), result[0], "shapeText: LamAlef final (visual first)");
    runner.expectEq(static_cast<uint32_t>(0xFE91), result[1], "shapeText: Beh initial (visual second)");
  }

  // Test 21: Non-joining character (Hamza)
  {
    auto result = shapeText("\xD8\xA1");  // Hamza U+0621
    runner.expectEq(static_cast<size_t>(1), result.size(), "shapeText: Hamza count");
    runner.expectEq(static_cast<uint32_t>(0xFE80), result[0], "shapeText: Hamza isolated");
  }

  // Test 22: ASCII passes through unchanged (NOT reversed — LTR in RTL base)
  {
    auto result = shapeText("AB");
    runner.expectEq(static_cast<size_t>(2), result.size(), "shapeText: ASCII count");
    runner.expectEq(static_cast<uint32_t>('A'), result[0], "shapeText: A first (LTR kept)");
    runner.expectEq(static_cast<uint32_t>('B'), result[1], "shapeText: B second (LTR kept)");
  }

  // Test 23: Diacritics preserved
  // Logical: Beh(0628) + Fatha(064E) + Alef(0627)
  // Fatha is transparent, doesn't affect joining
  {
    auto result = shapeText("\xD8\xA8\xD9\x8E\xD8\xA7");  // Beh + Fatha + Alef
    runner.expectEq(static_cast<size_t>(3), result.size(), "shapeText: diacritic preserved count");
    // Visual: Alef-final, Fatha, Beh-initial
    runner.expectEq(static_cast<uint32_t>(0xFE8E), result[0], "shapeText: Alef final with diacritic");
    runner.expectEq(static_cast<uint32_t>(0x064E), result[1], "shapeText: Fatha preserved");
    runner.expectEq(static_cast<uint32_t>(0xFE91), result[2], "shapeText: Beh initial with diacritic");
  }

  // ============================================
  // Mixed BiDi text tests
  // ============================================

  // Test 24: Mixed Arabic + Latin (no brackets)
  // Logical: أحمد Ahmed  (Alef-Hamza-Above + Ha + Meem + Dal + space + A + h + m + e + d)
  // Visual (RTL base): Latin run first (LTR kept), then Arabic run (reversed)
  {
    auto result = shapeText("\xD8\xA3\xD8\xAD\xD9\x85\xD8\xAF Ahmed");
    // Should have: 'A','h','m','e','d',' ', then 4 shaped Arabic chars (Dal Meem Ha Alef-Hamza)
    runner.expectEq(static_cast<size_t>(10), result.size(), "mixed: Arabic+Latin count");
    // LTR run comes first in visual (reversed run order)
    runner.expectEq(static_cast<uint32_t>('A'), result[0], "mixed: Latin 'A' first");
    runner.expectEq(static_cast<uint32_t>('h'), result[1], "mixed: Latin 'h'");
    runner.expectEq(static_cast<uint32_t>('m'), result[2], "mixed: Latin 'm'");
    runner.expectEq(static_cast<uint32_t>('e'), result[3], "mixed: Latin 'e'");
    runner.expectEq(static_cast<uint32_t>('d'), result[4], "mixed: Latin 'd'");
    // Space between runs resolves to RTL (adjacent to Arabic on right)
    // Arabic run reversed: Dal-final, Meem-medial, Ha-medial, Alef-Hamza-initial
  }

  // Test 25: Mixed Arabic + Latin in parentheses
  // Logical: أحمد (Ahmed)
  // Brackets should attach to Latin content
  {
    auto result = shapeText("\xD8\xA3\xD8\xAD\xD9\x85\xD8\xAF (Ahmed)");
    // Visual: "(Ahmed) " then Arabic reversed
    // Find the '(' and ')' to verify they surround the Latin text
    bool foundOpenParen = false;
    bool foundCloseParen = false;
    int openIdx = -1, closeIdx = -1;
    for (size_t i = 0; i < result.size(); i++) {
      if (result[i] == '(') { foundOpenParen = true; openIdx = static_cast<int>(i); }
      if (result[i] == ')') { foundCloseParen = true; closeIdx = static_cast<int>(i); }
    }
    runner.expectTrue(foundOpenParen && foundCloseParen, "mixed parens: both parens present");
    runner.expectTrue(openIdx < closeIdx, "mixed parens: ( before ) in visual order");
    // 'A' should be right after '('
    if (openIdx >= 0 && openIdx + 1 < static_cast<int>(result.size())) {
      runner.expectEq(static_cast<uint32_t>('A'), result[openIdx + 1], "mixed parens: A after (");
    }
  }

  // Test 26: Arabic with embedded digits
  // Logical: صفحة 42  (Sad + Fa + Ha + Teh-Marbuta + space + 4 + 2)
  // Digits are neutral, resolve based on neighbors
  {
    auto result = shapeText("\xD8\xB5\xD9\x81\xD8\xAD\xD8\xA9 42");
    runner.expectEq(static_cast<size_t>(7), result.size(), "digits: count");
    // Digits should appear before Arabic in visual order (they're at the end logically,
    // and get reversed with the RTL context), digits keep internal order
    // Find '4' and '2' positions
    int pos4 = -1, pos2 = -1;
    for (size_t i = 0; i < result.size(); i++) {
      if (result[i] == '4') pos4 = static_cast<int>(i);
      if (result[i] == '2') pos2 = static_cast<int>(i);
    }
    runner.expectTrue(pos4 >= 0 && pos2 >= 0, "digits: both digits present");
    runner.expectTrue(pos4 < pos2, "digits: 4 before 2 (LTR digit order preserved)");
  }

  // Test 27: Pure Arabic regression — same as Test 18 but verifies BiDi doesn't break it
  // Logical: Beh + Seen + Meem → visual: Meem-final, Seen-medial, Beh-initial
  {
    auto result = shapeText("\xD8\xA8\xD8\xB3\xD9\x85");
    runner.expectEq(static_cast<size_t>(3), result.size(), "bidi regression: three chars count");
    runner.expectEq(static_cast<uint32_t>(0xFEE2), result[0], "bidi regression: Meem final");
    runner.expectEq(static_cast<uint32_t>(0xFEB4), result[1], "bidi regression: Seen medial");
    runner.expectEq(static_cast<uint32_t>(0xFE91), result[2], "bidi regression: Beh initial");
  }

  // Test 28: The motivating bug — author name with Latin in parens
  // Logical: لويس كارول (Lewis Carroll)
  // Visual should have "(Lewis Carroll)" on the left, Arabic on the right
  {
    auto result = shapeText("\xD9\x84\xD9\x88\xD9\x8A\xD8\xB3 "
                            "\xD9\x83\xD8\xA7\xD8\xB1\xD9\x88\xD9\x84 "
                            "(Lewis Carroll)");
    // Verify Latin text is not reversed: find 'L','e','w','i','s' in order
    int prevPos = -1;
    bool latinOrdered = true;
    const char* lewis = "Lewis";
    for (int ci = 0; ci < 5 && latinOrdered; ci++) {
      bool found = false;
      for (size_t i = (prevPos < 0 ? 0 : prevPos + 1); i < result.size(); i++) {
        if (result[i] == static_cast<uint32_t>(lewis[ci])) {
          prevPos = static_cast<int>(i);
          found = true;
          break;
        }
      }
      if (!found) latinOrdered = false;
    }
    runner.expectTrue(latinOrdered, "bug fix: 'Lewis' chars in correct LTR order");
  }

  // ============================================
  // Bidi_Mirrored + AN-digit tests (issue #103)
  // ============================================

  // Test 29: Parens mirrored in pure Arabic (issue #103 case 1)
  // Logical: (جدًا)  — paren codepoints resolve to RTL, glyphs must be swapped
  // Expected visual: '(' on left, ')' on right
  {
    auto result = shapeText("(\xD8\xAC\xD8\xAF\xD9\x8B\xD8\xA7)");
    runner.expectEq(static_cast<size_t>(6), result.size(), "mirror parens: count");
    if (result.size() == 6) {
      runner.expectEq(static_cast<uint32_t>('('), result[0], "mirror parens: ( on visual left");
      runner.expectEq(static_cast<uint32_t>(')'), result[5], "mirror parens: ) on visual right");
    }
  }

  // Test 30: Guillemets mirrored in Arabic (issue #103 case 2)
  // Logical: «ا»  — expect « on left, » on right after mirroring
  {
    auto result = shapeText("\xC2\xAB\xD8\xA7\xC2\xBB");
    runner.expectEq(static_cast<size_t>(3), result.size(), "mirror guillemets: count");
    if (result.size() == 3) {
      runner.expectEq(static_cast<uint32_t>(0x00AB), result[0], "mirror guillemets: « on visual left");
      runner.expectEq(static_cast<uint32_t>(0x00BB), result[2], "mirror guillemets: » on visual right");
    }
  }

  // Test 31: Arabic-Indic digit run preserves LTR order (issue #103 case 3)
  // Logical: ١٨٩٤-١٩٦٢ (Arabic-Indic digits for "1894-1962")
  // Digits (AN) and '-' form an LTR run — must NOT be reversed
  {
    auto result = shapeText("\xD9\xA1\xD9\xA8\xD9\xA9\xD9\xA4-\xD9\xA1\xD9\xA9\xD9\xA6\xD9\xA2");
    runner.expectEq(static_cast<size_t>(9), result.size(), "AN digits: count");
    if (result.size() == 9) {
      runner.expectEq(static_cast<uint32_t>(0x0661), result[0], "AN digits: ١ at 0");
      runner.expectEq(static_cast<uint32_t>(0x0668), result[1], "AN digits: ٨ at 1");
      runner.expectEq(static_cast<uint32_t>(0x0669), result[2], "AN digits: ٩ at 2");
      runner.expectEq(static_cast<uint32_t>(0x0664), result[3], "AN digits: ٤ at 3");
      runner.expectEq(static_cast<uint32_t>('-'), result[4], "AN digits: - at 4");
      runner.expectEq(static_cast<uint32_t>(0x0661), result[5], "AN digits: ١ at 5");
      runner.expectEq(static_cast<uint32_t>(0x0669), result[6], "AN digits: ٩ at 6");
      runner.expectEq(static_cast<uint32_t>(0x0666), result[7], "AN digits: ٦ at 7");
      runner.expectEq(static_cast<uint32_t>(0x0662), result[8], "AN digits: ٢ at 8");
    }
  }

  // Test 32: Arabic-Indic digits embedded with Arabic letter
  // Logical: ص١٢ — digits keep logical order, Arabic letter on the right visually
  {
    auto result = shapeText("\xD8\xB5\xD9\xA1\xD9\xA2");
    runner.expectEq(static_cast<size_t>(3), result.size(), "AN embedded: count");
    if (result.size() == 3) {
      // LTR digit run appears first in visual order (base RTL reverses run order)
      runner.expectEq(static_cast<uint32_t>(0x0661), result[0], "AN embedded: ١ before ٢");
      runner.expectEq(static_cast<uint32_t>(0x0662), result[1], "AN embedded: ٢ second");
      // Sad is the only Arabic letter; its shaped form lives in U+FE70..U+FEFF
      runner.expectTrue(result[2] >= 0x0600 && result[2] <= 0xFEFF, "AN embedded: Sad on visual right");
    }
  }

  // Test 33: Square brackets mirrored in pure Arabic
  // Logical: [ا] — bracket codepoints resolve to RTL, glyphs must be swapped
  {
    auto result = shapeText("[\xD8\xA7]");
    runner.expectEq(static_cast<size_t>(3), result.size(), "mirror brackets: count");
    if (result.size() == 3) {
      runner.expectEq(static_cast<uint32_t>('['), result[0], "mirror brackets: [ on visual left");
      runner.expectEq(static_cast<uint32_t>(']'), result[2], "mirror brackets: ] on visual right");
    }
  }

  // Test 34: Curly braces mirrored in pure Arabic
  // Logical: {ا} — brace codepoints resolve to RTL, glyphs must be swapped
  {
    auto result = shapeText("{\xD8\xA7}");
    runner.expectEq(static_cast<size_t>(3), result.size(), "mirror braces: count");
    if (result.size() == 3) {
      runner.expectEq(static_cast<uint32_t>('{'), result[0], "mirror braces: { on visual left");
      runner.expectEq(static_cast<uint32_t>('}'), result[2], "mirror braces: } on visual right");
    }
  }

  // Test 35: Single guillemets and angle brackets mirrored in pure Arabic
  {
    // Logical: ‹ا›  (U+2039, U+0627, U+203A)
    auto result = shapeText("\xE2\x80\xB9\xD8\xA7\xE2\x80\xBA");
    runner.expectEq(static_cast<size_t>(3), result.size(), "mirror single guillemets: count");
    if (result.size() == 3) {
      runner.expectEq(static_cast<uint32_t>(0x2039), result[0], "mirror single guillemets: ‹ on visual left");
      runner.expectEq(static_cast<uint32_t>(0x203A), result[2], "mirror single guillemets: › on visual right");
    }

    // Logical: <ا>
    auto result2 = shapeText("<\xD8\xA7>");
    runner.expectEq(static_cast<size_t>(3), result2.size(), "mirror angle brackets: count");
    if (result2.size() == 3) {
      runner.expectEq(static_cast<uint32_t>('<'), result2[0], "mirror angle brackets: < on visual left");
      runner.expectEq(static_cast<uint32_t>('>'), result2[2], "mirror angle brackets: > on visual right");
    }
  }

  // Test 36: Extended Arabic-Indic (Persian/Urdu) digits keep LTR order
  // Logical: ۱۲۳ (U+06F1, U+06F2, U+06F3) — must form an LTR run, not be reversed
  {
    auto result = shapeText("\xDB\xB1\xDB\xB2\xDB\xB3");
    runner.expectEq(static_cast<size_t>(3), result.size(), "Persian digits: count");
    if (result.size() == 3) {
      runner.expectEq(static_cast<uint32_t>(0x06F1), result[0], "Persian digits: ۱ at 0");
      runner.expectEq(static_cast<uint32_t>(0x06F2), result[1], "Persian digits: ۲ at 1");
      runner.expectEq(static_cast<uint32_t>(0x06F3), result[2], "Persian digits: ۳ at 2");
    }

    // Logical: ص۱۲ — Persian digits embedded with Arabic letter
    auto result2 = shapeText("\xD8\xB5\xDB\xB1\xDB\xB2");
    runner.expectEq(static_cast<size_t>(3), result2.size(), "Persian embedded: count");
    if (result2.size() == 3) {
      // LTR digit run appears first in visual order (base RTL reverses run order)
      runner.expectEq(static_cast<uint32_t>(0x06F1), result2[0], "Persian embedded: ۱ before ۲");
      runner.expectEq(static_cast<uint32_t>(0x06F2), result2[1], "Persian embedded: ۲ second");
      runner.expectTrue(result2[2] >= 0x0600 && result2[2] <= 0xFEFF, "Persian embedded: Sad on visual right");
    }
  }

  // Test 37: Parens in LTR-only context are NOT mirrored (regression guard)
  // Input has no Arabic, so the entire run is LTR — bidiMirror() must not be applied
  {
    auto result = shapeText("(Hi)");
    runner.expectEq(static_cast<size_t>(4), result.size(), "LTR parens: count");
    if (result.size() == 4) {
      runner.expectEq(static_cast<uint32_t>('('), result[0], "LTR parens: ( unchanged at 0");
      runner.expectEq(static_cast<uint32_t>('H'), result[1], "LTR parens: H at 1");
      runner.expectEq(static_cast<uint32_t>('i'), result[2], "LTR parens: i at 2");
      runner.expectEq(static_cast<uint32_t>(')'), result[3], "LTR parens: ) unchanged at 3");
    }
  }

  return runner.allPassed() ? 0 : 1;
}
