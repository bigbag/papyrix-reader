#include "test_utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

// Inline the natural sort comparator from FileListState.cpp for testing
static bool naturalLess(const std::string& a, const std::string& b) {
  const char* s1 = a.c_str();
  const char* s2 = b.c_str();

  while (*s1 && *s2) {
    const auto uc = [](char c) { return static_cast<unsigned char>(c); };
    if (std::isdigit(uc(*s1)) && std::isdigit(uc(*s2))) {
      while (*s1 == '0') s1++;
      while (*s2 == '0') s2++;

      int len1 = 0, len2 = 0;
      while (std::isdigit(uc(s1[len1]))) len1++;
      while (std::isdigit(uc(s2[len2]))) len2++;
      if (len1 != len2) return len1 < len2;

      for (int i = 0; i < len1; i++) {
        if (s1[i] != s2[i]) return s1[i] < s2[i];
      }
      s1 += len1;
      s2 += len2;
    } else {
      char c1 = std::tolower(uc(*s1));
      char c2 = std::tolower(uc(*s2));
      if (c1 != c2) return c1 < c2;
      s1++;
      s2++;
    }
  }
  return *s1 == '\0' && *s2 != '\0';
}

// Helper: verify strict weak ordering properties for a pair
static void verifyPair(TestUtils::TestRunner& t, const std::string& lesser, const std::string& greater,
                       const std::string& label) {
  t.expectTrue(naturalLess(lesser, greater), label + ": " + lesser + " < " + greater);
  t.expectFalse(naturalLess(greater, lesser), label + ": !(" + greater + " < " + lesser + ")");
}

static void verifyEqual(TestUtils::TestRunner& t, const std::string& a, const std::string& b,
                         const std::string& label) {
  t.expectFalse(naturalLess(a, b), label + ": !(" + a + " < " + b + ")");
  t.expectFalse(naturalLess(b, a), label + ": !(" + b + " < " + a + ")");
}

int main() {
  TestUtils::TestRunner t("Natural Sort");

  // --- Basic alphabetical ---
  verifyPair(t, "apple", "banana", "alpha order");
  verifyPair(t, "a", "b", "single char");
  verifyEqual(t, "same", "same", "identical strings");

  // --- Case insensitive ---
  verifyEqual(t, "abc", "ABC", "case insensitive equal");
  verifyPair(t, "abc", "BCD", "case insensitive order");
  verifyEqual(t, "File", "file", "mixed case equal");

  // --- Numeric comparison ---
  verifyPair(t, "file1", "file2", "single digit");
  verifyPair(t, "file2", "file10", "2 < 10 (natural)");
  verifyPair(t, "file9", "file10", "9 < 10 (natural)");
  verifyPair(t, "file10", "file20", "10 < 20");
  verifyPair(t, "file99", "file100", "99 < 100");

  // --- Leading zeros ---
  verifyEqual(t, "file01", "file1", "leading zero equal");
  verifyEqual(t, "file001", "file01", "more leading zeros equal");
  verifyPair(t, "file01", "file2", "01 < 2");
  verifyPair(t, "file09", "file10", "09 < 10");

  // --- All zeros ---
  verifyEqual(t, "f0", "f00", "zero with different padding");
  verifyEqual(t, "f0", "f000", "zero vs triple zero");
  verifyPair(t, "f0", "f1", "0 < 1");
  verifyPair(t, "f00", "f1", "00 < 1");

  // --- Multiple numeric segments ---
  verifyPair(t, "v1.2.3", "v1.2.10", "version: 3 < 10");
  verifyPair(t, "v1.9", "v1.10", "version: 9 < 10");
  verifyPair(t, "v2.0", "v10.0", "version: 2 < 10");

  // --- Prefix ordering ---
  verifyPair(t, "file", "file1", "prefix < prefix+digit");
  verifyPair(t, "file", "filea", "prefix < prefix+alpha");
  verifyPair(t, "a", "ab", "short prefix < longer");
  verifyPair(t, "", "a", "empty < non-empty");
  verifyEqual(t, "", "", "empty == empty");

  // --- Mixed content ---
  verifyPair(t, "a1b", "a2b", "embedded digit comparison");
  verifyPair(t, "a1b", "a1c", "same digit, different suffix");
  verifyPair(t, "a01c", "a1d", "leading zero then different suffix");
  verifyPair(t, "1a", "2a", "digit-first strings");

  // --- Digits vs letters at same position ---
  verifyPair(t, "1x", "ax", "digit char < letter char");
  verifyPair(t, "9z", "az", "digit 9 < letter a");

  // --- Chapter/book style filenames ---
  {
    std::vector<std::string> input = {"Chapter 10", "Chapter 1", "Chapter 20", "Chapter 2", "Chapter 3"};
    std::sort(input.begin(), input.end(), [](const std::string& a, const std::string& b) {
      return naturalLess(a, b);
    });
    std::vector<std::string> expected = {"Chapter 1", "Chapter 2", "Chapter 3", "Chapter 10", "Chapter 20"};
    t.expectTrue(input == expected, "chapter sort order");
  }

  // --- Typical filenames ---
  {
    std::vector<std::string> input = {"img100.jpg", "img2.jpg", "img1.jpg", "img10.jpg", "img20.jpg"};
    std::sort(input.begin(), input.end(), [](const std::string& a, const std::string& b) {
      return naturalLess(a, b);
    });
    std::vector<std::string> expected = {"img1.jpg", "img2.jpg", "img10.jpg", "img20.jpg", "img100.jpg"};
    t.expectTrue(input == expected, "image filename sort order");
  }

  // --- Irreflexivity (strict weak ordering) ---
  t.expectFalse(naturalLess("abc", "abc"), "irreflexivity: abc");
  t.expectFalse(naturalLess("file10", "file10"), "irreflexivity: file10");
  t.expectFalse(naturalLess("", ""), "irreflexivity: empty");

  // --- Transitivity spot check ---
  {
    bool a_lt_b = naturalLess("a1", "a5");
    bool b_lt_c = naturalLess("a5", "a10");
    bool a_lt_c = naturalLess("a1", "a10");
    t.expectTrue(a_lt_b && b_lt_c && a_lt_c, "transitivity: a1 < a5 < a10");
  }

  return t.allPassed() ? 0 : 1;
}
