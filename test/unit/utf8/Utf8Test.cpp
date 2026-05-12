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
  if (str.empty() || numChars == 0) return;

  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(str.c_str());
  size_t totalCp = 0;
  while (*ptr) {
    utf8NextCodepoint(&ptr);
    totalCp++;
  }

  if (numChars >= totalCp) {
    str.clear();
    return;
  }

  const size_t keepCp = totalCp - numChars;
  ptr = reinterpret_cast<const unsigned char*>(str.c_str());
  for (size_t i = 0; i < keepCp; i++) {
    utf8NextCodepoint(&ptr);
  }
  str.resize(static_cast<size_t>(ptr - reinterpret_cast<const unsigned char*>(str.c_str())));
}

static size_t utf8SafeCopy(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return 0;
  if (!src) {
    dst[0] = '\0';
    return 0;
  }
  size_t maxCopy = dstSize - 1;
  size_t srcLen = 0;
  while (srcLen < maxCopy && src[srcLen] != '\0') {
    ++srcLen;
  }
  if (srcLen == maxCopy && src[srcLen] != '\0') {
    while (srcLen > 0 && (static_cast<unsigned char>(src[srcLen]) & 0xC0) == 0x80) {
      --srcLen;
    }
  }
  if (srcLen > 0) {
    memcpy(dst, src, srcLen);
  }
  dst[srcLen] = '\0';
  return srcLen;
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

  // ============================================
  // utf8SafeCopy() tests
  // ============================================

  // Test 26: ASCII fits entirely
  {
    char dst[16];
    size_t n = utf8SafeCopy(dst, sizeof(dst), "Hello");
    runner.expectEq(static_cast<size_t>(5), n, "utf8SafeCopy: ASCII fits - returns 5");
    runner.expectEqual("Hello", std::string(dst), "utf8SafeCopy: ASCII fits - content");
  }

  // Test 27: ASCII truncated
  {
    char dst[4];
    size_t n = utf8SafeCopy(dst, sizeof(dst), "Hello");
    runner.expectEq(static_cast<size_t>(3), n, "utf8SafeCopy: ASCII truncated - returns 3");
    runner.expectEqual("Hel", std::string(dst), "utf8SafeCopy: ASCII truncated - content");
  }

  // Test 28: 2-byte char at boundary - don't split
  {
    char dst[4];  // room for 3 bytes + null
    // "Aé" = 'A' (1 byte) + é (2 bytes) = 3 bytes total, fits exactly
    size_t n = utf8SafeCopy(dst, sizeof(dst), "A\xC3\xA9");
    runner.expectEq(static_cast<size_t>(3), n, "utf8SafeCopy: 2-byte fits exactly");
    runner.expectEqual("A\xC3\xA9", std::string(dst), "utf8SafeCopy: 2-byte fits exactly - content");
  }

  // Test 29: 2-byte char would be split - back up
  {
    char dst[3];  // room for 2 bytes + null
    // "Aé" needs 3 bytes, only 2 available → 'A' only
    size_t n = utf8SafeCopy(dst, sizeof(dst), "A\xC3\xA9");
    runner.expectEq(static_cast<size_t>(1), n, "utf8SafeCopy: 2-byte won't split - backs up to 1");
    runner.expectEqual("A", std::string(dst), "utf8SafeCopy: 2-byte won't split - content");
  }

  // Test 30: 3-byte CJK at boundary
  {
    char dst[5];  // room for 4 bytes + null
    // "A中" = 'A' (1) + 中 (3) = 4 bytes, fits exactly
    size_t n = utf8SafeCopy(dst, sizeof(dst), "A\xE4\xB8\xAD");
    runner.expectEq(static_cast<size_t>(4), n, "utf8SafeCopy: 3-byte CJK fits exactly");
  }

  // Test 31: 3-byte CJK would be split at byte 2
  {
    char dst[4];  // room for 3 bytes + null → would split 中 after byte 1
    size_t n = utf8SafeCopy(dst, sizeof(dst), "A\xE4\xB8\xAD");
    runner.expectEq(static_cast<size_t>(1), n, "utf8SafeCopy: 3-byte CJK won't split");
    runner.expectEqual("A", std::string(dst), "utf8SafeCopy: 3-byte CJK won't split - content");
  }

  // Test 32: 4-byte emoji would be split
  {
    char dst[4];  // room for 3 bytes + null → can't fit 4-byte emoji
    size_t n = utf8SafeCopy(dst, sizeof(dst), "\xF0\x9F\x98\x80");
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: 4-byte emoji won't split - returns 0");
    runner.expectEqual("", std::string(dst), "utf8SafeCopy: 4-byte emoji won't split - empty");
  }

  // Test 33: Cyrillic string truncation (the real-world case)
  {
    char dst[8];
    // "Лев" = 3 Cyrillic chars × 2 bytes = 6 bytes
    const char* src = "\xD0\x9B\xD0\xB5\xD0\xB2";
    size_t n = utf8SafeCopy(dst, sizeof(dst), src);
    runner.expectEq(static_cast<size_t>(6), n, "utf8SafeCopy: Cyrillic 'Лев' fits in 8");
    runner.expectEqual(std::string(src), std::string(dst), "utf8SafeCopy: Cyrillic content");
  }

  // Test 34: Cyrillic truncation at char boundary
  {
    char dst[6];  // room for 5 bytes + null → fits 2 Cyrillic chars (4 bytes)
    const char* src = "\xD0\x9B\xD0\xB5\xD0\xB2";  // "Лев" = 6 bytes
    size_t n = utf8SafeCopy(dst, sizeof(dst), src);
    runner.expectEq(static_cast<size_t>(4), n, "utf8SafeCopy: Cyrillic truncated - backs up");
    runner.expectEqual("\xD0\x9B\xD0\xB5", std::string(dst), "utf8SafeCopy: Cyrillic truncated - 2 chars");
  }

  // Test 35: Null src
  {
    char dst[8] = "dirty";
    size_t n = utf8SafeCopy(dst, sizeof(dst), nullptr);
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: null src returns 0");
    runner.expectEqual("", std::string(dst), "utf8SafeCopy: null src writes empty");
  }

  // Test 36: Empty src
  {
    char dst[8] = "dirty";
    size_t n = utf8SafeCopy(dst, sizeof(dst), "");
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: empty src returns 0");
    runner.expectEqual("", std::string(dst), "utf8SafeCopy: empty src writes empty");
  }

  // Test 37: Buffer size 1 (only null fits)
  {
    char dst[1];
    size_t n = utf8SafeCopy(dst, sizeof(dst), "Hello");
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: size 1 buffer returns 0");
    runner.expectEq('\0', dst[0], "utf8SafeCopy: size 1 buffer is null terminated");
  }

  // Test 38: Buffer size 0
  {
    char dst[1] = {'X'};
    size_t n = utf8SafeCopy(dst, 0, "Hello");
    runner.expectEq(static_cast<size_t>(0), n, "utf8SafeCopy: size 0 buffer returns 0");
  }

  // Test 39: Source shorter than buffer
  {
    char dst[256];
    size_t n = utf8SafeCopy(dst, sizeof(dst), "Hi");
    runner.expectEq(static_cast<size_t>(2), n, "utf8SafeCopy: short src returns 2");
    runner.expectEqual("Hi", std::string(dst), "utf8SafeCopy: short src content");
  }

  // Test 40: Mixed ASCII + multi-byte truncation
  {
    char dst[6];  // room for 5 + null
    // "Hé中" = H(1) + é(2) + 中(3) = 6 bytes, only 5 available
    // → back up from byte 5 (middle of 中) to byte 3 = "Hé"
    size_t n = utf8SafeCopy(dst, sizeof(dst), "H\xC3\xA9\xE4\xB8\xAD");
    runner.expectEq(static_cast<size_t>(3), n, "utf8SafeCopy: mixed truncation - 3 bytes");
    runner.expectEqual("H\xC3\xA9", std::string(dst), "utf8SafeCopy: mixed truncation - content");
  }

  return runner.allPassed() ? 0 : 1;
}
