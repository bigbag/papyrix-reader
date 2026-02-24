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

// Known attaching punctuation (from ParsedText.cpp)
static const std::vector<std::string> punctuation = {
    ".",
    ",",
    "!",
    "?",
    ";",
    ":",
    "\"",
    "'",
    "\xE2\x80\x99",  // ' (U+2019 right single quote)
    "\xE2\x80\x9D"   // " (U+201D right double quote)
};

static bool isAttachingPunctuationWord(const std::string& word) {
  if (word.empty()) return false;
  size_t pos = 0;
  while (pos < word.size()) {
    bool matched = false;
    for (const auto& p : punctuation) {
      if (word.compare(pos, p.size(), p) == 0) {
        pos += p.size();
        matched = true;
        break;
      }
    }
    if (!matched) return false;
  }
  return true;
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

  // ============================================
  // isAttachingPunctuationWord() tests
  // ============================================

  // Test 41: Empty string is not punctuation
  runner.expectFalse(isAttachingPunctuationWord(""), "isAttachingPunctuation: empty string");

  // Test 42: Single ASCII punctuation marks
  runner.expectTrue(isAttachingPunctuationWord("."), "isAttachingPunctuation: period");
  runner.expectTrue(isAttachingPunctuationWord(","), "isAttachingPunctuation: comma");
  runner.expectTrue(isAttachingPunctuationWord("!"), "isAttachingPunctuation: exclamation");
  runner.expectTrue(isAttachingPunctuationWord("?"), "isAttachingPunctuation: question mark");
  runner.expectTrue(isAttachingPunctuationWord(";"), "isAttachingPunctuation: semicolon");
  runner.expectTrue(isAttachingPunctuationWord(":"), "isAttachingPunctuation: colon");
  runner.expectTrue(isAttachingPunctuationWord("\""), "isAttachingPunctuation: double quote");
  runner.expectTrue(isAttachingPunctuationWord("'"), "isAttachingPunctuation: single quote");

  // Test 43: Unicode curly quotes
  runner.expectTrue(isAttachingPunctuationWord("\xE2\x80\x99"), "isAttachingPunctuation: U+2019 right single quote");
  runner.expectTrue(isAttachingPunctuationWord("\xE2\x80\x9D"), "isAttachingPunctuation: U+201D right double quote");

  // Test 44: Multiple punctuation marks
  runner.expectTrue(isAttachingPunctuationWord(".."), "isAttachingPunctuation: double period");
  runner.expectTrue(isAttachingPunctuationWord("..."), "isAttachingPunctuation: ellipsis (dots)");
  runner.expectTrue(isAttachingPunctuationWord(",\""), "isAttachingPunctuation: comma + quote");
  runner.expectTrue(isAttachingPunctuationWord(".'"), "isAttachingPunctuation: period + single quote");
  runner.expectTrue(isAttachingPunctuationWord("?\xE2\x80\x9D"), "isAttachingPunctuation: question + curly quote");

  // Test 45: Regular words are not punctuation
  runner.expectFalse(isAttachingPunctuationWord("word"), "isAttachingPunctuation: regular word");
  runner.expectFalse(isAttachingPunctuationWord("Hello"), "isAttachingPunctuation: capitalized word");
  runner.expectFalse(isAttachingPunctuationWord("a"), "isAttachingPunctuation: single letter");

  // Test 46: Mixed content (word + punctuation) is not pure punctuation
  runner.expectFalse(isAttachingPunctuationWord("word."), "isAttachingPunctuation: word with trailing period");
  runner.expectFalse(isAttachingPunctuationWord(".word"), "isAttachingPunctuation: leading period with word");
  runner.expectFalse(isAttachingPunctuationWord("a."), "isAttachingPunctuation: letter with period");

  // Test 47: Whitespace is not punctuation
  runner.expectFalse(isAttachingPunctuationWord(" "), "isAttachingPunctuation: space");
  runner.expectFalse(isAttachingPunctuationWord(". "), "isAttachingPunctuation: period + space");

  // ============================================
  // Attaching punctuation gap counting tests
  // These test the logic used in extractLine() to calculate spacing
  // when punctuation becomes a separate token due to inline styles
  // ============================================

  // Helper: count actual gaps (mirrors extractLine logic)
  auto countActualGaps = [](const std::vector<std::string>& words) -> size_t {
    size_t actualGapCount = 0;
    for (size_t i = 1; i < words.size(); i++) {
      if (!isAttachingPunctuationWord(words[i])) {
        actualGapCount++;
      }
    }
    return actualGapCount;
  };

  // Test 48: Normal words - all gaps count
  // "Hello world today" -> 2 gaps (between each word)
  {
    std::vector<std::string> words = {"Hello", "world", "today"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(2), gaps, "gapCount: 3 normal words = 2 gaps");
  }

  // Test 49: Punctuation as separate token - gap excluded
  // "Hello ," -> should be 0 gaps (comma attaches to Hello)
  // This simulates: word<em>,</em>
  {
    std::vector<std::string> words = {"Hello", ","};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(0), gaps, "gapCount: word + punct = 0 gaps");
  }

  // Test 50: Word then punctuation then word
  // "Hello , world" -> should be 1 gap (comma attaches to Hello, space before world)
  // This simulates: Hello<em>,</em> world
  {
    std::vector<std::string> words = {"Hello", ",", "world"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(1), gaps, "gapCount: word + punct + word = 1 gap");
  }

  // Test 51: Multiple punctuation tokens in sequence
  // "Hello ." "'" -> should be 0 gaps (both attach)
  // This simulates: Hello<em>.'</em> split into tokens
  {
    std::vector<std::string> words = {"Hello", ".", "'"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(0), gaps, "gapCount: word + two punct = 0 gaps");
  }

  // Test 52: Quoted text pattern
  // "said " "'" "Hello" -> 1 gap (quote attaches to said, gap before Hello)
  // This simulates: said<em>'</em>Hello
  {
    std::vector<std::string> words = {"said", "'", "Hello"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(1), gaps, "gapCount: word + quote + word = 1 gap");
  }

  // Test 53: End quote pattern
  // "Hello" "'" "," "he" "said" -> 2 gaps
  // quote and comma attach to Hello, then gaps before "he" and "said"
  {
    std::vector<std::string> words = {"Hello", "'", ",", "he", "said"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(2), gaps, "gapCount: complex quote pattern = 2 gaps");
  }

  // Test 54: Single word - no gaps
  {
    std::vector<std::string> words = {"Hello"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(0), gaps, "gapCount: single word = 0 gaps");
  }

  // Test 55: Single punctuation - no gaps
  {
    std::vector<std::string> words = {"."};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(0), gaps, "gapCount: single punct = 0 gaps");
  }

  // Test 56: Empty word list - no gaps
  {
    std::vector<std::string> words = {};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(0), gaps, "gapCount: empty = 0 gaps");
  }

  // Test 57: Unicode curly quote as separate token
  // "word" + right double quote -> 0 gaps
  {
    std::vector<std::string> words = {"word", "\xE2\x80\x9D"};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(0), gaps, "gapCount: word + curly quote = 0 gaps");
  }

  // Test 58: Mixed punctuation and words
  // "The" "quick" "," "brown" "fox" "." -> 3 gaps
  // comma attaches to quick, period attaches to fox
  {
    std::vector<std::string> words = {"The", "quick", ",", "brown", "fox", "."};
    size_t gaps = countActualGaps(words);
    runner.expectEq(static_cast<size_t>(3), gaps, "gapCount: sentence with inline punct = 3 gaps");
  }

  // ============================================
  // Position calculation tests
  // Verify that punctuation doesn't get space before it
  // ============================================

  // Helper: calculate word positions (mirrors extractLine logic)
  auto calculatePositions = [](const std::vector<std::string>& words,
                               const std::vector<uint16_t>& widths,
                               int spacing) -> std::vector<uint16_t> {
    std::vector<uint16_t> positions;
    uint16_t xpos = 0;
    for (size_t i = 0; i < words.size(); i++) {
      positions.push_back(xpos);
      // Add spacing after this word, unless next word is attaching punctuation
      bool nextIsAttaching = (i + 1 < words.size()) && isAttachingPunctuationWord(words[i + 1]);
      xpos += widths[i] + (nextIsAttaching ? 0 : spacing);
    }
    return positions;
  };

  // Test 59: Normal words get even spacing
  {
    std::vector<std::string> words = {"Hello", "world"};
    std::vector<uint16_t> widths = {50, 50};
    auto positions = calculatePositions(words, widths, 10);
    runner.expectEq(static_cast<uint16_t>(0), positions[0], "positions: first word at 0");
    runner.expectEq(static_cast<uint16_t>(60), positions[1], "positions: second word at 50+10=60");
  }

  // Test 60: Punctuation attaches without space
  {
    std::vector<std::string> words = {"Hello", ","};
    std::vector<uint16_t> widths = {50, 5};
    auto positions = calculatePositions(words, widths, 10);
    runner.expectEq(static_cast<uint16_t>(0), positions[0], "positions: word at 0");
    runner.expectEq(static_cast<uint16_t>(50), positions[1], "positions: punct at 50 (no space)");
  }

  // Test 61: Word + punct + word pattern
  {
    std::vector<std::string> words = {"Hello", ",", "world"};
    std::vector<uint16_t> widths = {50, 5, 50};
    auto positions = calculatePositions(words, widths, 10);
    runner.expectEq(static_cast<uint16_t>(0), positions[0], "positions: Hello at 0");
    runner.expectEq(static_cast<uint16_t>(50), positions[1], "positions: comma at 50 (attached)");
    runner.expectEq(static_cast<uint16_t>(65), positions[2], "positions: world at 55+10=65");
  }

  // Test 62: Multiple punctuation in sequence attach
  {
    std::vector<std::string> words = {"word", ".", "\xE2\x80\x9D"};  // word."
    std::vector<uint16_t> widths = {40, 5, 8};
    auto positions = calculatePositions(words, widths, 10);
    runner.expectEq(static_cast<uint16_t>(0), positions[0], "positions: word at 0");
    runner.expectEq(static_cast<uint16_t>(40), positions[1], "positions: period at 40 (attached)");
    runner.expectEq(static_cast<uint16_t>(45), positions[2], "positions: quote at 45 (attached)");
  }

  // Test 63: Real-world dialog pattern
  // "said" + "'" + "Hello" + "," + "'" + "he" + "replied"
  {
    std::vector<std::string> words = {"said", "'", "Hello", ",", "'", "he", "replied"};
    std::vector<uint16_t> widths = {30, 3, 40, 5, 3, 15, 50};
    auto positions = calculatePositions(words, widths, 10);
    // said at 0
    runner.expectEq(static_cast<uint16_t>(0), positions[0], "dialog: said at 0");
    // ' attaches to said -> at 30
    runner.expectEq(static_cast<uint16_t>(30), positions[1], "dialog: quote at 30 (attached)");
    // Hello after quote+spacing -> 30+3+10=43
    runner.expectEq(static_cast<uint16_t>(43), positions[2], "dialog: Hello at 43");
    // , attaches to Hello -> 43+40=83
    runner.expectEq(static_cast<uint16_t>(83), positions[3], "dialog: comma at 83 (attached)");
    // ' attaches to comma -> 83+5=88
    runner.expectEq(static_cast<uint16_t>(88), positions[4], "dialog: end quote at 88 (attached)");
    // he after quote+spacing -> 88+3+10=101
    runner.expectEq(static_cast<uint16_t>(101), positions[5], "dialog: he at 101");
    // replied after he+spacing -> 101+15+10=126
    runner.expectEq(static_cast<uint16_t>(126), positions[6], "dialog: replied at 126");
  }

  // ============================================
  // Indentation logic tests
  // These test the condition used in calculateWordWidths() to decide
  // whether to prepend Unicode space characters for paragraph indentation
  // ============================================

  // Block style enum values (from TextBlock.h)
  enum BLOCK_STYLE : uint8_t {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
  };

  // Helper: applies indentation logic (mirrors calculateWordWidths)
  auto applyIndent = [](std::string& firstWord, uint8_t indentLevel, uint8_t style) {
    if (indentLevel > 0 && !firstWord.empty() && style != CENTER_ALIGN) {
      switch (indentLevel) {
        case 2:  // Normal - em-space (U+2003)
          firstWord.insert(0, "\xe2\x80\x83");
          break;
        case 3:  // Large - em-space + en-space (U+2003 + U+2002)
          firstWord.insert(0, "\xe2\x80\x83\xe2\x80\x82");
          break;
        default:  // Fallback: single en-space (U+2002)
          firstWord.insert(0, "\xe2\x80\x82");
          break;
      }
    }
  };

  // Test: JUSTIFIED text gets indented
  {
    std::string word = "Hello";
    applyIndent(word, 2, JUSTIFIED);
    runner.expectEqual("\xe2\x80\x83Hello", word, "indent: JUSTIFIED gets em-space");
  }

  // Test: LEFT_ALIGN text gets indented
  {
    std::string word = "Hello";
    applyIndent(word, 2, LEFT_ALIGN);
    runner.expectEqual("\xe2\x80\x83Hello", word, "indent: LEFT_ALIGN gets em-space");
  }

  // Test: CENTER_ALIGN text does NOT get indented
  {
    std::string word = "Hello";
    applyIndent(word, 2, CENTER_ALIGN);
    runner.expectEqual("Hello", word, "indent: CENTER_ALIGN skips indentation");
  }

  // Test: RIGHT_ALIGN text gets indented
  {
    std::string word = "Hello";
    applyIndent(word, 2, RIGHT_ALIGN);
    runner.expectEqual("\xe2\x80\x83Hello", word, "indent: RIGHT_ALIGN gets em-space");
  }

  // Test: CENTER_ALIGN with large indent level still skips
  {
    std::string word = "Title";
    applyIndent(word, 3, CENTER_ALIGN);
    runner.expectEqual("Title", word, "indent: CENTER_ALIGN skips large indent too");
  }

  // Test: indentLevel 0 never indents regardless of style
  {
    std::string word = "Hello";
    applyIndent(word, 0, JUSTIFIED);
    runner.expectEqual("Hello", word, "indent: level 0 JUSTIFIED no indent");
  }

  // Test: indent level 3 produces em-space + en-space
  {
    std::string word = "Hello";
    applyIndent(word, 3, JUSTIFIED);
    runner.expectEqual("\xe2\x80\x83\xe2\x80\x82Hello", word, "indent: level 3 = em+en space");
  }

  // Test: indent level 1 (fallback) produces en-space
  {
    std::string word = "Hello";
    applyIndent(word, 1, JUSTIFIED);
    runner.expectEqual("\xe2\x80\x82Hello", word, "indent: level 1 fallback = en-space");
  }

  // ============================================
  // RTL position calculation tests
  // These test the logic used in extractLine() for RTL word positioning
  // ============================================

  // Helper: calculate RTL word positions (mirrors extractLine RTL logic)
  auto calculateRtlPositions = [](const std::vector<std::string>& words,
                                  const std::vector<uint16_t>& widths,
                                  int pageWidth, int spacing) -> std::vector<uint16_t> {
    std::vector<uint16_t> positions;
    uint16_t xpos = pageWidth;
    for (size_t i = 0; i < words.size(); i++) {
      xpos -= widths[i];
      positions.push_back(xpos);
      // Subtract spacing after this word, unless next is attaching punctuation
      bool nextIsAttaching = (i + 1 < words.size()) && isAttachingPunctuationWord(words[i + 1]);
      xpos -= (nextIsAttaching ? 0 : spacing);
    }
    return positions;
  };

  // Test 64: RTL two words positioned right-to-left
  {
    std::vector<std::string> words = {"Hello", "world"};
    std::vector<uint16_t> widths = {50, 50};
    auto positions = calculateRtlPositions(words, widths, 400, 10);
    // First word: xpos = 400 - 50 = 350
    runner.expectEq(static_cast<uint16_t>(350), positions[0], "RTL positions: first word at 350");
    // Second word: xpos = 350 - 10 - 50 = 290
    runner.expectEq(static_cast<uint16_t>(290), positions[1], "RTL positions: second word at 290");
  }

  // Test 65: RTL single word at right edge
  {
    std::vector<std::string> words = {"Hello"};
    std::vector<uint16_t> widths = {50};
    auto positions = calculateRtlPositions(words, widths, 400, 10);
    runner.expectEq(static_cast<uint16_t>(350), positions[0], "RTL positions: single word at right edge");
  }

  // Test 66: RTL punctuation attaches without gap
  {
    std::vector<std::string> words = {"Hello", ","};
    std::vector<uint16_t> widths = {50, 5};
    auto positions = calculateRtlPositions(words, widths, 400, 10);
    // Hello: xpos = 400 - 50 = 350
    runner.expectEq(static_cast<uint16_t>(350), positions[0], "RTL punct: Hello at 350");
    // Comma attaches: xpos = 350 - 0 - 5 = 345
    runner.expectEq(static_cast<uint16_t>(345), positions[1], "RTL punct: comma at 345 (no gap)");
  }

  // Test 67: RTL word + punct + word pattern
  {
    std::vector<std::string> words = {"Hello", ",", "world"};
    std::vector<uint16_t> widths = {50, 5, 50};
    auto positions = calculateRtlPositions(words, widths, 400, 10);
    // Hello: xpos = 400 - 50 = 350
    runner.expectEq(static_cast<uint16_t>(350), positions[0], "RTL w+p+w: Hello at 350");
    // Comma attaches: xpos = 350 - 0 - 5 = 345
    runner.expectEq(static_cast<uint16_t>(345), positions[1], "RTL w+p+w: comma at 345");
    // world: xpos = 345 - 10 - 50 = 285
    runner.expectEq(static_cast<uint16_t>(285), positions[2], "RTL w+p+w: world at 285");
  }

  // Test 68: RTL words fill from right to left
  {
    std::vector<std::string> words = {"A", "B", "C"};
    std::vector<uint16_t> widths = {30, 40, 50};
    auto positions = calculateRtlPositions(words, widths, 200, 10);
    // A: 200 - 30 = 170
    runner.expectEq(static_cast<uint16_t>(170), positions[0], "RTL fill: A at 170");
    // B: 170 - 10 - 40 = 120
    runner.expectEq(static_cast<uint16_t>(120), positions[1], "RTL fill: B at 120");
    // C: 120 - 10 - 50 = 60
    runner.expectEq(static_cast<uint16_t>(60), positions[2], "RTL fill: C at 60");
  }

  // ============================================
  // hasTrailingSoftHyphen() tests
  // ============================================

  // Helper: check if word ends with soft hyphen marker (U+00AD = 0xC2 0xAD)
  auto hasTrailingSoftHyphen = [](const std::string& word) -> bool {
    return word.size() >= 2 && static_cast<unsigned char>(word[word.size() - 2]) == SOFT_HYPHEN_BYTE1 &&
           static_cast<unsigned char>(word[word.size() - 1]) == SOFT_HYPHEN_BYTE2;
  };

  // Helper: replace trailing soft hyphen with visible ASCII hyphen
  auto replaceTrailingSoftHyphen = [&hasTrailingSoftHyphen](std::string word) -> std::string {
    if (hasTrailingSoftHyphen(word)) {
      word.resize(word.size() - 2);
      word += '-';
    }
    return word;
  };

  // Test: word without soft hyphen
  runner.expectFalse(hasTrailingSoftHyphen("hello"), "hasTrailingSoftHyphen: plain word");
  runner.expectFalse(hasTrailingSoftHyphen("hello-"), "hasTrailingSoftHyphen: trailing ASCII hyphen");
  runner.expectFalse(hasTrailingSoftHyphen(""), "hasTrailingSoftHyphen: empty string");

  // Test: word with trailing soft hyphen marker
  {
    std::string word = "hel\xC2\xAD";
    runner.expectTrue(hasTrailingSoftHyphen(word), "hasTrailingSoftHyphen: trailing U+00AD");
  }

  // Test: word with embedded soft hyphen (not trailing) is false
  {
    std::string word = "hel\xC2\xADlo";
    runner.expectFalse(hasTrailingSoftHyphen(word), "hasTrailingSoftHyphen: embedded U+00AD");
  }

  // ============================================
  // replaceTrailingSoftHyphen() tests
  // ============================================

  // Test: no trailing soft hyphen - unchanged
  runner.expectEqual("hello", replaceTrailingSoftHyphen("hello"),
                     "replaceTrailingSoftHyphen: plain word unchanged");
  runner.expectEqual("hello-", replaceTrailingSoftHyphen("hello-"),
                     "replaceTrailingSoftHyphen: ASCII hyphen unchanged");

  // Test: trailing soft hyphen replaced with visible hyphen
  {
    std::string word = "hel\xC2\xAD";
    runner.expectEqual("hel-", replaceTrailingSoftHyphen(word),
                       "replaceTrailingSoftHyphen: U+00AD becomes -");
  }

  // Test: Cyrillic word with trailing soft hyphen
  {
    // "зда" in UTF-8 + soft hyphen marker
    std::string word = "\xD0\xB7\xD0\xB4\xD0\xB0\xC2\xAD";
    std::string result = replaceTrailingSoftHyphen(word);
    runner.expectEqual("\xD0\xB7\xD0\xB4\xD0\xB0-", result,
                       "replaceTrailingSoftHyphen: Cyrillic зда + U+00AD becomes зда-");
  }

  // ============================================
  // Rejoin logic tests
  // These test the rejoin step that runs at the start of layoutAndExtractLines
  // to undo splits from a previous interrupted greedy pass
  // ============================================

  // Helper: rejoin words with trailing soft hyphen markers (mirrors layoutAndExtractLines logic)
  auto rejoinSplitWords = [&hasTrailingSoftHyphen](std::vector<std::string>& words) {
    auto it = words.begin();
    while (it != words.end()) {
      auto nextIt = std::next(it);
      if (nextIt != words.end() && hasTrailingSoftHyphen(*it)) {
        it->resize(it->size() - 2);  // Remove trailing U+00AD
        *it += *nextIt;              // Rejoin with suffix
        words.erase(nextIt);
        // Don't advance - check for nested splits
      } else {
        ++it;
      }
    }
  };

  // Test: no splits - words unchanged
  {
    std::vector<std::string> words = {"hello", "world"};
    rejoinSplitWords(words);
    runner.expectEq(static_cast<size_t>(2), words.size(), "rejoin: no splits, size unchanged");
    runner.expectEqual("hello", words[0], "rejoin: no splits, word 0 unchanged");
    runner.expectEqual("world", words[1], "rejoin: no splits, word 1 unchanged");
  }

  // Test: single split pair rejoined
  {
    std::vector<std::string> words = {"hel\xC2\xAD", "lo", "world"};
    rejoinSplitWords(words);
    runner.expectEq(static_cast<size_t>(2), words.size(), "rejoin: single split, size = 2");
    runner.expectEqual("hello", words[0], "rejoin: single split, rejoined word");
    runner.expectEqual("world", words[1], "rejoin: single split, next word intact");
  }

  // Test: Cyrillic split pair rejoined (the actual bug scenario)
  {
    // "зда" + U+00AD marker, then "ний"
    std::vector<std::string> words = {"\xD0\xB7\xD0\xB4\xD0\xB0\xC2\xAD", "\xD0\xBD\xD0\xB8\xD0\xB9"};
    rejoinSplitWords(words);
    runner.expectEq(static_cast<size_t>(1), words.size(), "rejoin: Cyrillic split, size = 1");
    runner.expectEqual("\xD0\xB7\xD0\xB4\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB9", words[0],
                       "rejoin: Cyrillic зда+ний = зданий");
  }

  // Test: multiple consecutive splits rejoined
  {
    std::vector<std::string> words = {"in\xC2\xAD", "ter\xC2\xAD", "national"};
    rejoinSplitWords(words);
    runner.expectEq(static_cast<size_t>(1), words.size(), "rejoin: nested splits, size = 1");
    runner.expectEqual("international", words[0], "rejoin: nested splits rejoined");
  }

  // Test: split at end of word list
  {
    std::vector<std::string> words = {"before", "hel\xC2\xAD", "lo"};
    rejoinSplitWords(words);
    runner.expectEq(static_cast<size_t>(2), words.size(), "rejoin: split at end, size = 2");
    runner.expectEqual("before", words[0], "rejoin: split at end, first word intact");
    runner.expectEqual("hello", words[1], "rejoin: split at end, rejoined");
  }

  // Test: empty word list
  {
    std::vector<std::string> words = {};
    rejoinSplitWords(words);
    runner.expectEq(static_cast<size_t>(0), words.size(), "rejoin: empty list stays empty");
  }

  return runner.allPassed() ? 0 : 1;
}
