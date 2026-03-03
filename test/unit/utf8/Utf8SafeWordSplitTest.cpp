#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Inline the UTF-8 safe word split algorithm from ChapterHtmlSlimParser.cpp:402-428
// This tests the core logic without firmware dependencies.

static constexpr int MAX_WORD_SIZE = 200;

struct SplitResult {
  std::string flushed;   // portion that was flushed (up to safeIdx)
  std::string overflow;  // bytes carried over to next word
  bool usedElseBranch;   // true if safeIdx == 0 (all continuation bytes)
};

// Simulates the char-buffer split from ChapterHtmlSlimParser::characterData()
// Buffer must be at least MAX_WORD_SIZE bytes filled.
static SplitResult splitCharBuffer(const char* buffer, int bufferLen) {
  SplitResult result;
  result.usedElseBranch = false;

  int safeIdx = bufferLen;
  while (safeIdx > 0 && (static_cast<unsigned char>(buffer[safeIdx - 1]) & 0xC0) == 0x80) {
    safeIdx--;
  }
  if (safeIdx > 0 && static_cast<unsigned char>(buffer[safeIdx - 1]) >= 0xC0) {
    safeIdx--;
  }

  if (safeIdx > 0) {
    int overflowLen = bufferLen - safeIdx;
    result.flushed.assign(buffer, safeIdx);
    if (overflowLen > 0) {
      result.overflow.assign(buffer + safeIdx, overflowLen);
    }
  } else {
    result.flushed.assign(buffer, bufferLen);
    result.usedElseBranch = true;
  }

  return result;
}

// Simulates the std::string split from PlainTextParser.cpp:174-196
// Same algorithm, but with std::string and threshold 100.
struct StringSplitResult {
  std::string flushed;
  std::string overflow;
  bool usedElseBranch;
};

static StringSplitResult splitString(const std::string& partialWord) {
  StringSplitResult result;
  result.usedElseBranch = false;

  size_t safeLen = partialWord.length();
  while (safeLen > 0 && (static_cast<unsigned char>(partialWord[safeLen - 1]) & 0xC0) == 0x80) {
    safeLen--;
  }
  if (safeLen > 0 && static_cast<unsigned char>(partialWord[safeLen - 1]) >= 0xC0) {
    safeLen--;
  }

  if (safeLen > 0) {
    result.overflow = partialWord.substr(safeLen);
    result.flushed = partialWord.substr(0, safeLen);
  } else {
    result.flushed = partialWord;
    result.usedElseBranch = true;
  }

  return result;
}

int main() {
  TestUtils::TestRunner runner("Utf8SafeWordSplit");

  // === Char-buffer variant (EPUB parser, MAX_WORD_SIZE=200) ===

  // 1. All ASCII at limit - no carry needed
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    // Last byte is 'A' (0x41) - not a continuation byte, not >= 0xC0
    // safeIdx stays at MAX_WORD_SIZE, overflow is empty
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE, "all ASCII: flushed == MAX_WORD_SIZE");
    runner.expectTrue(r.overflow.empty(), "all ASCII: no overflow");
    runner.expectFalse(r.usedElseBranch, "all ASCII: normal branch");
  }

  // 2. Buffer ends with incomplete 2-byte char (leader 0xC3, no continuation)
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    buf[MAX_WORD_SIZE - 1] = (char)0xC3;  // 2-byte leader at end
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE - 1, "incomplete 2-byte: flushed excludes leader");
    runner.expectEq((int)r.overflow.size(), 1, "incomplete 2-byte: 1 byte overflow");
    runner.expectEq((unsigned char)r.overflow[0], (unsigned char)0xC3, "incomplete 2-byte: overflow is the leader");
  }

  // 3. Buffer ends with incomplete 3-byte char (leader + 1 continuation)
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    buf[MAX_WORD_SIZE - 2] = (char)0xE4;  // 3-byte leader
    buf[MAX_WORD_SIZE - 1] = (char)0xB8;  // continuation
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE - 2, "incomplete 3-byte (1 cont): flushed excludes leader+cont");
    runner.expectEq((int)r.overflow.size(), 2, "incomplete 3-byte (1 cont): 2 bytes overflow");
  }

  // 4. Buffer ends with complete 3-byte CJK char - still carries over
  //    The algorithm always backs up past the last multi-byte leader.
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    // Place complete CJK char at end: E4 B8 AD = U+4E2D
    buf[MAX_WORD_SIZE - 3] = (char)0xE4;
    buf[MAX_WORD_SIZE - 2] = (char)0xB8;
    buf[MAX_WORD_SIZE - 1] = (char)0xAD;
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    // Walk back: [199]=0xAD cont, [198]=0xB8 cont, [197]=0xE4 >= 0xC0 -> safeIdx=197
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE - 3, "complete 3-byte CJK: still carries over (3 bytes)");
    runner.expectEq((int)r.overflow.size(), 3, "complete 3-byte CJK: 3 bytes overflow");
    runner.expectEq((unsigned char)r.overflow[0], (unsigned char)0xE4, "complete 3-byte CJK: overflow starts with leader");
  }

  // 5. Buffer ends with incomplete 4-byte emoji (leader + 1 continuation)
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    buf[MAX_WORD_SIZE - 2] = (char)0xF0;  // 4-byte leader
    buf[MAX_WORD_SIZE - 1] = (char)0x9F;  // continuation
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE - 2, "4-byte emoji (1 cont): 2 bytes overflow");
    runner.expectEq((int)r.overflow.size(), 2, "4-byte emoji (1 cont): overflow size");
  }

  // 6. Buffer ends with incomplete 4-byte emoji (leader + 2 continuations)
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    buf[MAX_WORD_SIZE - 3] = (char)0xF0;  // 4-byte leader
    buf[MAX_WORD_SIZE - 2] = (char)0x9F;  // continuation
    buf[MAX_WORD_SIZE - 1] = (char)0x98;  // continuation
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE - 3, "4-byte emoji (2 cont): 3 bytes overflow");
    runner.expectEq((int)r.overflow.size(), 3, "4-byte emoji (2 cont): overflow size");
  }

  // 7. Buffer ends with complete 4-byte emoji - still carries over
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'A', MAX_WORD_SIZE);
    buf[MAX_WORD_SIZE - 4] = (char)0xF0;
    buf[MAX_WORD_SIZE - 3] = (char)0x9F;
    buf[MAX_WORD_SIZE - 2] = (char)0x98;
    buf[MAX_WORD_SIZE - 1] = (char)0x80;
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE - 4, "complete 4-byte emoji: 4 bytes overflow");
    runner.expectEq((int)r.overflow.size(), 4, "complete 4-byte emoji: overflow size");
  }

  // 8. Repeating 3-byte CJK chars with lone leader at end
  {
    // Fill with 3-byte sequences, then a lone leader at position [199]
    char buf[MAX_WORD_SIZE];
    // Fill first 198 bytes with 66 CJK chars (E4 B8 AD)
    for (int i = 0; i < 198; i += 3) {
      buf[i] = (char)0xE4;
      buf[i + 1] = (char)0xB8;
      buf[i + 2] = (char)0xAD;
    }
    buf[198] = (char)0xE4;  // lone 3-byte leader
    buf[199] = (char)0xB8;  // one continuation
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    // Walk back: [199]=0xB8 cont, [198]=0xE4 >= 0xC0 -> safeIdx=198
    runner.expectEq((int)r.flushed.size(), 198, "CJK + lone leader: flushed at last complete boundary");
    runner.expectEq((int)r.overflow.size(), 2, "CJK + lone leader: 2 bytes overflow");
  }

  // 9. Buffer of only continuation bytes (0x80) - safeIdx reaches 0, else branch
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, (char)0x80, MAX_WORD_SIZE);
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    // All bytes are continuation: safeIdx walks to 0
    // Then check buf[safeIdx-1] is skipped because safeIdx==0
    runner.expectTrue(r.usedElseBranch, "all continuation bytes: else branch");
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE, "all continuation bytes: flushed everything");
    runner.expectTrue(r.overflow.empty(), "all continuation bytes: no overflow");
  }

  // 10. Single leader byte at position 0 with rest continuation bytes
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, (char)0x80, MAX_WORD_SIZE);
    buf[0] = (char)0xE4;  // leader at position 0
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    // Walk back: all [199..1] are 0x80 cont, [0] = 0xE4 >= 0xC0 -> safeIdx = 0
    // safeIdx == 0 -> else branch
    runner.expectTrue(r.usedElseBranch, "leader at pos 0: else branch");
  }

  // 11. Mixed ASCII + multi-byte ending cleanly on ASCII - no carry
  {
    char buf[MAX_WORD_SIZE];
    memset(buf, 'B', MAX_WORD_SIZE);
    // Place a complete CJK in the middle
    buf[100] = (char)0xE4;
    buf[101] = (char)0xB8;
    buf[102] = (char)0xAD;
    // Last byte is 'B' (ASCII)
    auto r = splitCharBuffer(buf, MAX_WORD_SIZE);
    runner.expectEq((int)r.flushed.size(), MAX_WORD_SIZE, "mixed ending on ASCII: full flush");
    runner.expectTrue(r.overflow.empty(), "mixed ending on ASCII: no overflow");
  }

  // 12. Single byte buffer (bufferLen=1, ASCII)
  {
    char buf[1] = {'X'};
    auto r = splitCharBuffer(buf, 1);
    runner.expectEq((int)r.flushed.size(), 1, "single ASCII byte: flushed 1");
    runner.expectTrue(r.overflow.empty(), "single ASCII byte: no overflow");
  }

  // === std::string variant (PlainText parser, threshold 100) ===

  // 13. String of 101 ASCII chars - no split needed at boundary
  {
    std::string word(101, 'z');
    auto r = splitString(word);
    // Last byte is 'z' (0x7A) - not continuation, not >= 0xC0
    // safeLen stays at 101
    runner.expectEq((int)r.flushed.size(), 101, "string ASCII 101: flushed all");
    runner.expectTrue(r.overflow.empty(), "string ASCII 101: no overflow");
  }

  // 14. String ends with incomplete 3-byte CJK
  {
    std::string word(99, 'a');
    word += (char)0xE4;  // 3-byte leader at position 99
    word += (char)0xB8;  // continuation at position 100
    auto r = splitString(word);
    runner.expectEq((int)r.flushed.size(), 99, "string incomplete 3-byte: flushed 99 ASCII");
    runner.expectEq((int)r.overflow.size(), 2, "string incomplete 3-byte: 2 bytes overflow");
  }

  // 15. String of all continuation bytes - else branch
  {
    std::string word(101, (char)0x80);
    auto r = splitString(word);
    runner.expectTrue(r.usedElseBranch, "string all continuations: else branch");
    runner.expectEq((int)r.flushed.size(), 101, "string all continuations: flushed all");
  }

  // 16. String with complete 2-byte char at end - carries over
  {
    std::string word(99, 'a');
    word += (char)0xC3;  // 2-byte leader
    word += (char)0xA9;  // continuation (complete: U+00E9)
    auto r = splitString(word);
    // Walk back: [100]=0xA9 cont, [99]=0xC3 >= 0xC0 -> safeLen=99
    runner.expectEq((int)r.flushed.size(), 99, "string complete 2-byte: flushed 99");
    runner.expectEq((int)r.overflow.size(), 2, "string complete 2-byte: 2 bytes overflow");
  }

  // 17. Empty string - no split (threshold not reached, but test the algorithm)
  {
    std::string word;
    auto r = splitString(word);
    // safeLen starts at 0, while loop doesn't execute, second if doesn't execute
    // safeLen == 0 -> else branch
    runner.expectTrue(r.usedElseBranch, "empty string: else branch");
    runner.expectTrue(r.flushed.empty(), "empty string: flushed empty");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
