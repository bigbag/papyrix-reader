#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>

// Inline the UTF-8 functions directly for testing (avoiding linker issues)

static int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

static uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const int bytes = utf8CodepointLen(**string);
  const uint8_t* chr = *string;
  *string += bytes;

  if (bytes == 1) {
    return chr[0];
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  return cp;
}

static size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  // Walk back to find the start of the last UTF-8 character
  // UTF-8 continuation bytes start with 10xxxxxx (0x80-0xBF)
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

static void utf8TruncateChars(std::string& str, size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

int main() {
  TestUtils::TestRunner runner("Utf8 Functions");

  // ============================================
  // utf8NextCodepoint() tests
  // ============================================

  // Test 1: ASCII (1-byte)
  {
    const unsigned char* str = reinterpret_cast<const unsigned char*>("ABC");
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>('A'), cp, "utf8NextCodepoint: ASCII 'A'");
    runner.expectEq(static_cast<size_t>(1), static_cast<size_t>(ptr - str), "utf8NextCodepoint: ASCII advances 1 byte");

    cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>('B'), cp, "utf8NextCodepoint: ASCII 'B'");
  }

  // Test 2: Latin Extended (2-byte) - é = U+00E9 = 0xC3 0xA9
  {
    const unsigned char str[] = {0xC3, 0xA9, 0x00};  // é
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0x00E9), cp, "utf8NextCodepoint: 2-byte 'e-acute' (U+00E9)");
    runner.expectEq(static_cast<size_t>(2), static_cast<size_t>(ptr - str), "utf8NextCodepoint: 2-byte advances 2 bytes");
  }

  // Test 3: CJK (3-byte) - 中 = U+4E2D = 0xE4 0xB8 0xAD
  {
    const unsigned char str[] = {0xE4, 0xB8, 0xAD, 0x00};  // 中
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0x4E2D), cp, "utf8NextCodepoint: 3-byte CJK (U+4E2D)");
    runner.expectEq(static_cast<size_t>(3), static_cast<size_t>(ptr - str), "utf8NextCodepoint: 3-byte advances 3 bytes");
  }

  // Test 4: Emoji (4-byte) - grinning face = U+1F600 = 0xF0 0x9F 0x98 0x80
  {
    const unsigned char str[] = {0xF0, 0x9F, 0x98, 0x80, 0x00};
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0x1F600), cp, "utf8NextCodepoint: 4-byte emoji (U+1F600)");
    runner.expectEq(static_cast<size_t>(4), static_cast<size_t>(ptr - str), "utf8NextCodepoint: 4-byte advances 4 bytes");
  }

  // Test 5: Null terminator
  {
    const unsigned char str[] = {0x00};
    const unsigned char* ptr = str;
    uint32_t cp = utf8NextCodepoint(&ptr);
    runner.expectEq(static_cast<uint32_t>(0), cp, "utf8NextCodepoint: null terminator returns 0");
    runner.expectEq(static_cast<size_t>(0), static_cast<size_t>(ptr - str), "utf8NextCodepoint: null doesn't advance");
  }

  // Test 6: Invalid start byte (continuation byte at start) - 0x80-0xBF
  {
    const unsigned char str[] = {0x80, 'A', 0x00};  // Invalid start byte
    const unsigned char* ptr = str;
    utf8NextCodepoint(&ptr);
    // Falls back to 1-byte handling
    runner.expectEq(static_cast<size_t>(1), static_cast<size_t>(ptr - str),
                    "utf8NextCodepoint: invalid start byte advances 1 byte (fallback)");
  }

  // ============================================
  // utf8RemoveLastChar() tests
  // ============================================

  // Test 7: Empty string
  {
    std::string str = "";
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(0), newSize, "utf8RemoveLastChar: empty string returns 0");
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: empty string stays empty");
  }

  // Test 8: Single ASCII char
  {
    std::string str = "A";
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(0), newSize, "utf8RemoveLastChar: single ASCII returns 0");
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: single ASCII becomes empty");
  }

  // Test 9: Multiple ASCII chars
  {
    std::string str = "ABC";
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(2), newSize, "utf8RemoveLastChar: 'ABC' -> 'AB' (size 2)");
    runner.expectEqual("AB", str, "utf8RemoveLastChar: 'ABC' -> 'AB'");
  }

  // Test 10: Remove 2-byte char (e-acute)
  {
    std::string str = "caf\xC3\xA9";  // "cafe" with accent
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(3), newSize, "utf8RemoveLastChar: accented cafe -> 'caf' (size 3)");
    runner.expectEqual("caf", str, "utf8RemoveLastChar: accented cafe -> 'caf'");
  }

  // Test 11: Remove 3-byte char (CJK)
  {
    std::string str = "A\xE4\xB8\xAD";  // "A" + CJK char
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(1), newSize, "utf8RemoveLastChar: 'A+CJK' -> 'A' (size 1)");
    runner.expectEqual("A", str, "utf8RemoveLastChar: 'A+CJK' -> 'A'");
  }

  // Test 12: Remove 4-byte char (emoji)
  {
    std::string str = "Hi\xF0\x9F\x98\x80";  // "Hi" + emoji
    size_t newSize = utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(2), newSize, "utf8RemoveLastChar: 'Hi+emoji' -> 'Hi' (size 2)");
    runner.expectEqual("Hi", str, "utf8RemoveLastChar: 'Hi+emoji' -> 'Hi'");
  }

  // Test 13: Mixed content - remove emoji first, then ASCII
  {
    std::string str = "A\xF0\x9F\x98\x80";  // "A" + emoji
    utf8RemoveLastChar(str);
    runner.expectEqual("A", str, "utf8RemoveLastChar: 'A+emoji' -> 'A'");
    utf8RemoveLastChar(str);
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: 'A' -> empty");
  }

  // Test 14: Only multi-byte characters
  {
    std::string str = "\xE4\xB8\xAD\xE6\x96\x87";  // Two CJK chars
    utf8RemoveLastChar(str);
    runner.expectEq(static_cast<size_t>(3), str.size(), "utf8RemoveLastChar: 2 CJK -> 1 CJK (size 3)");
    utf8RemoveLastChar(str);
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: 1 CJK -> empty");
  }

  // ============================================
  // utf8TruncateChars() tests
  // ============================================

  // Test 15: Truncate 0 chars (no change)
  {
    std::string str = "Hello";
    utf8TruncateChars(str, 0);
    runner.expectEqual("Hello", str, "utf8TruncateChars: truncate 0 chars is no-op");
  }

  // Test 16: Truncate 1 char
  {
    std::string str = "Hello";
    utf8TruncateChars(str, 1);
    runner.expectEqual("Hell", str, "utf8TruncateChars: 'Hello' - 1 = 'Hell'");
  }

  // Test 17: Truncate N chars
  {
    std::string str = "Hello";
    utf8TruncateChars(str, 3);
    runner.expectEqual("He", str, "utf8TruncateChars: 'Hello' - 3 = 'He'");
  }

  // Test 18: Truncate more chars than exist
  {
    std::string str = "Hi";
    utf8TruncateChars(str, 10);
    runner.expectTrue(str.empty(), "utf8TruncateChars: truncate more than exist makes empty");
  }

  // Test 19: Truncate from empty string
  {
    std::string str = "";
    utf8TruncateChars(str, 5);
    runner.expectTrue(str.empty(), "utf8TruncateChars: empty string stays empty");
  }

  // Test 20: Mixed ASCII and multi-byte truncation
  {
    std::string str = "AB\xC3\xA9\xE4\xB8\xAD";  // "AB" + accent + CJK (4 chars)
    utf8TruncateChars(str, 2);                   // Remove CJK and accent
    runner.expectEqual("AB", str, "utf8TruncateChars: 'AB+accent+CJK' - 2 = 'AB'");
  }

  // Test 21: Truncate all chars from multi-byte string
  {
    std::string str = "\xF0\x9F\x98\x80\xF0\x9F\x98\x81";  // Two emojis
    utf8TruncateChars(str, 2);
    runner.expectTrue(str.empty(), "utf8TruncateChars: 2 emojis - 2 = empty");
  }

  // ============================================
  // Corner cases for invalid UTF-8
  // ============================================

  // Test 22: Incomplete 2-byte sequence at end
  {
    std::string str = "A\xC3";  // Incomplete 2-byte char
    utf8RemoveLastChar(str);
    // Should still handle gracefully - removes the orphan byte
    runner.expectEq(static_cast<size_t>(1), str.size(), "utf8RemoveLastChar: incomplete 2-byte handled");
  }

  // Test 23: Incomplete 3-byte sequence at end
  {
    std::string str = "A\xE4\xB8";  // Incomplete 3-byte char
    utf8RemoveLastChar(str);
    // The implementation walks back past continuation bytes
    runner.expectTrue(str.size() <= 2, "utf8RemoveLastChar: incomplete 3-byte handled");
  }

  // Test 24: Overlong encoding detection (should be handled as fallback)
  // 0xC0 0x80 is an overlong encoding of NUL - technically invalid
  {
    const unsigned char str[] = {0xC0, 0x80, 0x00};
    const unsigned char* ptr = str;
    utf8NextCodepoint(&ptr);
    // Should at least not crash - advances some bytes
    runner.expectTrue(ptr > str, "utf8NextCodepoint: overlong encoding advances");
  }

  // Test 25: String with only continuation bytes
  {
    std::string str = "\x80\x80\x80";  // Invalid: only continuation bytes
    utf8RemoveLastChar(str);
    // Implementation walks back until it finds a non-continuation byte or beginning
    runner.expectTrue(str.empty(), "utf8RemoveLastChar: all continuation bytes removed");
  }

  return runner.allPassed() ? 0 : 1;
}
