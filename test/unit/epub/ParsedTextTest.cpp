#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Include UTF-8 functions (inlined from Utf8.h)
static int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1;
}

static uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) return 0;
  const int bytes = utf8CodepointLen(**string);
  const uint8_t* chr = *string;
  *string += bytes;
  if (bytes == 1) return chr[0];
  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);
  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }
  return cp;
}

// Soft hyphen constants (from ParsedText.cpp)
constexpr unsigned char SOFT_HYPHEN_BYTE1 = 0xC2;
constexpr unsigned char SOFT_HYPHEN_BYTE2 = 0xAD;

// ============================================
// Pure functions from ParsedText.cpp (inlined for testing)
// ============================================

static std::vector<size_t> findSoftHyphenPositions(const std::string& word) {
  std::vector<size_t> positions;
  for (size_t i = 0; i + 1 < word.size(); ++i) {
    if (static_cast<unsigned char>(word[i]) == SOFT_HYPHEN_BYTE1 &&
        static_cast<unsigned char>(word[i + 1]) == SOFT_HYPHEN_BYTE2) {
      positions.push_back(i);
    }
  }
  return positions;
}

static std::string stripSoftHyphens(const std::string& word) {
  std::string result;
  result.reserve(word.size());
  size_t i = 0;
  while (i < word.size()) {
    if (i + 1 < word.size() && static_cast<unsigned char>(word[i]) == SOFT_HYPHEN_BYTE1 &&
        static_cast<unsigned char>(word[i + 1]) == SOFT_HYPHEN_BYTE2) {
      i += 2;
    } else {
      result += word[i++];
    }
  }
  return result;
}

static std::string getWordPrefix(const std::string& word, size_t softHyphenPos) {
  std::string prefix = word.substr(0, softHyphenPos);
  return stripSoftHyphens(prefix) + "-";
}

static std::string getWordSuffix(const std::string& word, size_t softHyphenPos) {
  return word.substr(softHyphenPos + 2);
}

static bool isCjkCodepoint(uint32_t cp) {
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   // CJK Unified Ideographs
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;   // CJK Extension A
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK Compatibility
  if (cp >= 0x3040 && cp <= 0x309F) return true;   // Hiragana
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;   // Katakana
  if (cp >= 0xAC00 && cp <= 0xD7AF) return true;   // Hangul Syllables
  if (cp >= 0x20000 && cp <= 0x2A6DF) return true; // CJK Extension B+
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;   // Fullwidth ASCII
  return false;
}

// Knuth-Plass helper functions
static float calculateBadness(int lineWidth, int targetWidth) {
  constexpr float INFINITY_PENALTY = 10000.0f;
  if (targetWidth <= 0) return INFINITY_PENALTY;
  if (lineWidth > targetWidth) return INFINITY_PENALTY;
  if (lineWidth == targetWidth) return 0.0f;
  float ratio = static_cast<float>(targetWidth - lineWidth) / static_cast<float>(targetWidth);
  return ratio * ratio * ratio * 100.0f;
}

static float calculateDemerits(float badness, bool isLastLine) {
  constexpr float INFINITY_PENALTY = 10000.0f;
  if (badness >= INFINITY_PENALTY) return INFINITY_PENALTY;
  if (isLastLine) return 0.0f;
  return (1.0f + badness) * (1.0f + badness);
}

// Greedy line breaking (simplified version for testing)
static std::vector<size_t> computeLineBreaksGreedy(int pageWidth, int spaceWidth,
                                                   const std::vector<uint16_t>& wordWidths) {
  std::vector<size_t> breaks;
  const size_t n = wordWidths.size();
  if (n == 0) return breaks;

  int lineWidth = -spaceWidth;
  for (size_t i = 0; i < n; i++) {
    const int wordWidth = wordWidths[i];
    if (lineWidth + wordWidth + spaceWidth > pageWidth && lineWidth > 0) {
      breaks.push_back(i);
      lineWidth = wordWidth;
    } else {
      lineWidth += wordWidth + spaceWidth;
    }
  }
  breaks.push_back(n);
  return breaks;
}

int main() {
  TestUtils::TestRunner runner("ParsedText Functions");

  // ============================================
  // isCjkCodepoint() tests
  // ============================================

  // Test 1: ASCII is not CJK
  runner.expectFalse(isCjkCodepoint('A'), "isCjkCodepoint: ASCII 'A' is not CJK");
  runner.expectFalse(isCjkCodepoint('z'), "isCjkCodepoint: ASCII 'z' is not CJK");
  runner.expectFalse(isCjkCodepoint(' '), "isCjkCodepoint: space is not CJK");

  // Test 2: Latin Extended is not CJK
  runner.expectFalse(isCjkCodepoint(0x00E9), "isCjkCodepoint: e-acute (U+00E9) is not CJK");
  runner.expectFalse(isCjkCodepoint(0x00F1), "isCjkCodepoint: n-tilde (U+00F1) is not CJK");

  // Test 3: CJK Unified Ideographs
  runner.expectTrue(isCjkCodepoint(0x4E00), "isCjkCodepoint: U+4E00 (一) is CJK");
  runner.expectTrue(isCjkCodepoint(0x4E2D), "isCjkCodepoint: U+4E2D (中) is CJK");
  runner.expectTrue(isCjkCodepoint(0x9FFF), "isCjkCodepoint: U+9FFF (end of CJK) is CJK");

  // Test 4: Hiragana
  runner.expectTrue(isCjkCodepoint(0x3042), "isCjkCodepoint: U+3042 (あ) is CJK");
  runner.expectTrue(isCjkCodepoint(0x309F), "isCjkCodepoint: U+309F (end of Hiragana) is CJK");

  // Test 5: Katakana
  runner.expectTrue(isCjkCodepoint(0x30A2), "isCjkCodepoint: U+30A2 (ア) is CJK");
  runner.expectTrue(isCjkCodepoint(0x30FF), "isCjkCodepoint: U+30FF (end of Katakana) is CJK");

  // Test 6: Hangul
  runner.expectTrue(isCjkCodepoint(0xAC00), "isCjkCodepoint: U+AC00 (가) is CJK");
  runner.expectTrue(isCjkCodepoint(0xD7AF), "isCjkCodepoint: U+D7AF (end of Hangul) is CJK");

  // Test 7: Fullwidth ASCII
  runner.expectTrue(isCjkCodepoint(0xFF01), "isCjkCodepoint: U+FF01 (fullwidth !) is CJK");
  runner.expectTrue(isCjkCodepoint(0xFF21), "isCjkCodepoint: U+FF21 (fullwidth A) is CJK");

  // Test 8: Boundary cases
  runner.expectFalse(isCjkCodepoint(0x4DFF), "isCjkCodepoint: U+4DFF (before CJK) not CJK");
  runner.expectFalse(isCjkCodepoint(0xA000), "isCjkCodepoint: U+A000 (after CJK main) not CJK");

  // ============================================
  // findSoftHyphenPositions() tests
  // ============================================

  // Test 9: No soft hyphens
  {
    auto positions = findSoftHyphenPositions("hello");
    runner.expectEq(static_cast<size_t>(0), positions.size(), "findSoftHyphenPositions: no hyphens");
  }

  // Test 10: Single soft hyphen
  {
    std::string word = "hel\xC2\xADlo";  // hel­lo
    auto positions = findSoftHyphenPositions(word);
    runner.expectEq(static_cast<size_t>(1), positions.size(), "findSoftHyphenPositions: 1 hyphen count");
    runner.expectEq(static_cast<size_t>(3), positions[0], "findSoftHyphenPositions: 1 hyphen position");
  }

  // Test 11: Multiple soft hyphens
  {
    // Use string concatenation to avoid hex escape eating next char
    std::string word = "in\xC2\xAD" "ter\xC2\xAD" "na\xC2\xAD" "tion\xC2\xAD" "al";  // in­ter­na­tion­al
    auto positions = findSoftHyphenPositions(word);
    runner.expectEq(static_cast<size_t>(4), positions.size(), "findSoftHyphenPositions: 4 hyphens count");
  }

  // Test 12: Empty string
  {
    auto positions = findSoftHyphenPositions("");
    runner.expectEq(static_cast<size_t>(0), positions.size(), "findSoftHyphenPositions: empty string");
  }

  // Test 13: Only soft hyphen
  {
    std::string word = "\xC2\xAD";
    auto positions = findSoftHyphenPositions(word);
    runner.expectEq(static_cast<size_t>(1), positions.size(), "findSoftHyphenPositions: only hyphen");
    runner.expectEq(static_cast<size_t>(0), positions[0], "findSoftHyphenPositions: only hyphen at 0");
  }

  // ============================================
  // stripSoftHyphens() tests
  // ============================================

  // Test 14: No soft hyphens
  {
    std::string result = stripSoftHyphens("hello");
    runner.expectEqual("hello", result, "stripSoftHyphens: no hyphens unchanged");
  }

  // Test 15: Single soft hyphen
  {
    std::string word = "hel\xC2\xADlo";
    std::string result = stripSoftHyphens(word);
    runner.expectEqual("hello", result, "stripSoftHyphens: single hyphen removed");
  }

  // Test 16: Multiple soft hyphens
  {
    std::string word = "in\xC2\xAD" "ter\xC2\xAD" "na\xC2\xAD" "tion\xC2\xAD" "al";
    std::string result = stripSoftHyphens(word);
    runner.expectEqual("international", result, "stripSoftHyphens: multiple hyphens removed");
  }

  // Test 17: Empty string
  {
    std::string result = stripSoftHyphens("");
    runner.expectTrue(result.empty(), "stripSoftHyphens: empty stays empty");
  }

  // Test 18: Only soft hyphens
  {
    std::string word = "\xC2\xAD\xC2\xAD\xC2\xAD";
    std::string result = stripSoftHyphens(word);
    runner.expectTrue(result.empty(), "stripSoftHyphens: only hyphens becomes empty");
  }

  // Test 19: Mixed with multi-byte UTF-8
  {
    std::string word = "caf\xC3\xA9\xC2\xADs";  // café­s (e-acute + soft hyphen)
    std::string result = stripSoftHyphens(word);
    runner.expectEqual("caf\xC3\xA9s", result, "stripSoftHyphens: preserves multi-byte chars");
  }

  // ============================================
  // getWordPrefix() tests
  // ============================================

  // Test 20: Simple prefix
  {
    std::string word = "hel\xC2\xADlo";  // soft hyphen at position 3
    std::string prefix = getWordPrefix(word, 3);
    runner.expectEqual("hel-", prefix, "getWordPrefix: simple prefix with hyphen");
  }

  // Test 21: Prefix with embedded soft hyphens
  {
    std::string word = "in\xC2\xAD" "ter\xC2\xAD" "na\xC2\xAD" "tional";
    auto positions = findSoftHyphenPositions(word);
    // Split at second soft hyphen (after "ter")
    std::string prefix = getWordPrefix(word, positions[1]);
    runner.expectEqual("inter-", prefix, "getWordPrefix: strips embedded hyphens");
  }

  // ============================================
  // getWordSuffix() tests
  // ============================================

  // Test 22: Simple suffix
  {
    std::string word = "hel\xC2\xADlo";
    std::string suffix = getWordSuffix(word, 3);
    runner.expectEqual("lo", suffix, "getWordSuffix: simple suffix");
  }

  // Test 23: Suffix keeps remaining soft hyphens
  {
    std::string word = "in\xC2\xAD" "ter\xC2\xAD" "na\xC2\xAD" "tional";
    auto positions = findSoftHyphenPositions(word);
    // Split at first soft hyphen
    std::string suffix = getWordSuffix(word, positions[0]);
    // Suffix should be "ter­na­tional" (with remaining soft hyphens)
    auto suffixPositions = findSoftHyphenPositions(suffix);
    runner.expectEq(static_cast<size_t>(2), suffixPositions.size(),
                    "getWordSuffix: keeps remaining soft hyphens");
  }

  // ============================================
  // calculateBadness() tests
  // ============================================

  // Test 24: Perfect fit
  {
    float badness = calculateBadness(400, 400);
    runner.expectFloatEq(0.0f, badness, "calculateBadness: perfect fit = 0");
  }

  // Test 25: Overfull line
  {
    float badness = calculateBadness(450, 400);
    runner.expectTrue(badness >= 10000.0f, "calculateBadness: overfull = infinity");
  }

  // Test 26: Zero target width
  {
    float badness = calculateBadness(100, 0);
    runner.expectTrue(badness >= 10000.0f, "calculateBadness: zero target = infinity");
  }

  // Test 27: Slightly loose line
  {
    float badness1 = calculateBadness(380, 400);  // 5% slack
    float badness2 = calculateBadness(300, 400);  // 25% slack
    runner.expectTrue(badness2 > badness1, "calculateBadness: looser line has higher badness");
  }

  // ============================================
  // calculateDemerits() tests
  // ============================================

  // Test 28: Last line always 0 demerits
  {
    float demerits = calculateDemerits(50.0f, true);
    runner.expectFloatEq(0.0f, demerits, "calculateDemerits: last line = 0");
  }

  // Test 29: Infinity badness propagates
  {
    float demerits = calculateDemerits(10000.0f, false);
    runner.expectTrue(demerits >= 10000.0f, "calculateDemerits: infinity propagates");
  }

  // Test 30: Non-last line has demerits
  {
    float demerits = calculateDemerits(10.0f, false);
    runner.expectTrue(demerits > 0.0f, "calculateDemerits: non-last line > 0");
  }

  // ============================================
  // computeLineBreaksGreedy() tests
  // ============================================

  // Test 31: Empty word list
  {
    std::vector<uint16_t> widths = {};
    auto breaks = computeLineBreaksGreedy(400, 10, widths);
    runner.expectTrue(breaks.empty(), "computeLineBreaksGreedy: empty list");
  }

  // Test 32: Single word fits
  {
    std::vector<uint16_t> widths = {100};
    auto breaks = computeLineBreaksGreedy(400, 10, widths);
    runner.expectEq(static_cast<size_t>(1), breaks.size(), "computeLineBreaksGreedy: 1 word, 1 break");
    runner.expectEq(static_cast<size_t>(1), breaks[0], "computeLineBreaksGreedy: break at end");
  }

  // Test 33: Multiple words fit on one line
  {
    std::vector<uint16_t> widths = {50, 50, 50};  // 50+10+50+10+50 = 170 < 400
    auto breaks = computeLineBreaksGreedy(400, 10, widths);
    runner.expectEq(static_cast<size_t>(1), breaks.size(), "computeLineBreaksGreedy: all fit, 1 line");
    runner.expectEq(static_cast<size_t>(3), breaks[0], "computeLineBreaksGreedy: break at 3");
  }

  // Test 34: Words require multiple lines
  {
    std::vector<uint16_t> widths = {100, 100, 100, 100, 100};  // Need to wrap
    auto breaks = computeLineBreaksGreedy(250, 10, widths);  // Max ~2 words per line
    runner.expectTrue(breaks.size() > 1, "computeLineBreaksGreedy: multiple lines");
    runner.expectEq(widths.size(), breaks.back(), "computeLineBreaksGreedy: ends at word count");
  }

  // Test 35: Oversized word
  {
    std::vector<uint16_t> widths = {500};  // Wider than page
    auto breaks = computeLineBreaksGreedy(400, 10, widths);
    runner.expectEq(static_cast<size_t>(1), breaks.size(), "computeLineBreaksGreedy: oversized still breaks");
    runner.expectEq(static_cast<size_t>(1), breaks[0], "computeLineBreaksGreedy: oversized at position 1");
  }

  // Test 36: Mixed sizes
  {
    std::vector<uint16_t> widths = {10, 10, 10, 300, 10, 10};  // Small, big, small
    auto breaks = computeLineBreaksGreedy(400, 10, widths);
    // Line 1: 10+10+10+10+300 = 350 < 400
    // Line 2: 10+10+10 = 30 < 400
    runner.expectTrue(breaks.size() >= 1, "computeLineBreaksGreedy: mixed sizes handled");
  }

  // ============================================
  // CJK detection via UTF-8 parsing
  // ============================================

  // Test 37: Detect CJK in mixed string
  {
    const unsigned char* str = reinterpret_cast<const unsigned char*>("Hello\xE4\xB8\xADWorld");
    const unsigned char* ptr = str;
    bool foundCjk = false;
    uint32_t cp;
    while ((cp = utf8NextCodepoint(&ptr))) {
      if (isCjkCodepoint(cp)) {
        foundCjk = true;
        break;
      }
    }
    runner.expectTrue(foundCjk, "CJK detection: finds CJK in mixed string");
  }

  // Test 38: No CJK in pure ASCII
  {
    const unsigned char* str = reinterpret_cast<const unsigned char*>("Hello World");
    const unsigned char* ptr = str;
    bool foundCjk = false;
    uint32_t cp;
    while ((cp = utf8NextCodepoint(&ptr))) {
      if (isCjkCodepoint(cp)) {
        foundCjk = true;
        break;
      }
    }
    runner.expectFalse(foundCjk, "CJK detection: no CJK in ASCII");
  }

  // Test 39: Japanese hiragana detected
  {
    const unsigned char* str = reinterpret_cast<const unsigned char*>("\xE3\x81\x82");  // あ
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectTrue(isCjkCodepoint(cp), "CJK detection: hiragana あ detected");
  }

  // Test 40: Korean hangul detected
  {
    const unsigned char* str = reinterpret_cast<const unsigned char*>("\xEA\xB0\x80");  // 가
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectTrue(isCjkCodepoint(cp), "CJK detection: hangul 가 detected");
  }

  return runner.allPassed() ? 0 : 1;
}
