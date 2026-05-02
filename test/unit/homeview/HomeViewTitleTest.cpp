#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>

// Mirror of HomeView::copyUtf8Safe (src/ui/views/HomeView.h).
// Inlined here because HomeView.h pulls graphics dependencies (GfxRenderer, Theme).
// Keep in sync with the helper in HomeView.h.
static void copyUtf8Safe(char* dst, const char* src, int cap) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
  int n = static_cast<int>(strlen(dst));
  if (n == 0) return;
  int leadIdx = n - 1;
  while (leadIdx > 0 && (static_cast<unsigned char>(dst[leadIdx]) & 0xC0) == 0x80) {
    --leadIdx;
  }
  const unsigned char lead = static_cast<unsigned char>(dst[leadIdx]);
  int expect;
  if (lead < 0x80)              expect = 1;
  else if ((lead >> 5) == 0x6)  expect = 2;
  else if ((lead >> 4) == 0xE)  expect = 3;
  else if ((lead >> 3) == 0x1E) expect = 4;
  else                          expect = 1;
  if (n - leadIdx < expect) {
    dst[leadIdx] = '\0';
  }
}

static bool endsWithCompleteUtf8(const char* s) {
  const int n = static_cast<int>(strlen(s));
  if (n == 0) return true;
  int leadIdx = n - 1;
  while (leadIdx > 0 && (static_cast<unsigned char>(s[leadIdx]) & 0xC0) == 0x80) {
    --leadIdx;
  }
  const unsigned char lead = static_cast<unsigned char>(s[leadIdx]);
  int expect;
  if (lead < 0x80)              expect = 1;
  else if ((lead >> 5) == 0x6)  expect = 2;
  else if ((lead >> 4) == 0xE)  expect = 3;
  else if ((lead >> 3) == 0x1E) expect = 4;
  else                          return false;
  return (n - leadIdx) == expect;
}

int main() {
  TestUtils::TestRunner runner("HomeViewTitleTest");

  // --- Short ASCII fits unchanged ---
  {
    char buf[64] = {};
    copyUtf8Safe(buf, "Sherlock Holmes", sizeof(buf));
    runner.expectTrue(strcmp(buf, "Sherlock Holmes") == 0, "ASCII short title preserved");
  }

  // --- Short Cyrillic author fits unchanged (real example from bug report) ---
  {
    char buf[96] = {};
    copyUtf8Safe(buf, "\xD0\x90\xD0\xB4\xD0\xB0\xD0\xBC\xD1\x81 \xD0\x94\xD1\x83\xD0\xB3\xD0\xBB\xD0\xB0\xD1\x81", sizeof(buf));
    runner.expectTrue(strcmp(buf, "\xD0\x90\xD0\xB4\xD0\xB0\xD0\xBC\xD1\x81 \xD0\x94\xD1\x83\xD0\xB3\xD0\xBB\xD0\xB0\xD1\x81") == 0,
                      "Cyrillic 'Адамс Дуглас' preserved");
  }

  // --- The bug-report title fits in 256-byte buffer ---
  {
    // "Путеводитель «Автостопом по Млечному Пути»"
    const char* full =
        "\xD0\x9F\xD1\x83\xD1\x82\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C "
        "\xC2\xAB"
        "\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD1\x81\xD1\x82\xD0\xBE\xD0\xBF\xD0\xBE\xD0\xBC \xD0\xBF\xD0\xBE "
        "\xD0\x9C\xD0\xBB\xD0\xB5\xD1\x87\xD0\xBD\xD0\xBE\xD0\xBC\xD1\x83 "
        "\xD0\x9F\xD1\x83\xD1\x82\xD0\xB8"
        "\xC2\xBB";
    char buf[256] = {};
    copyUtf8Safe(buf, full, sizeof(buf));
    runner.expectTrue(strcmp(buf, full) == 0, "Bug-report title fits in 256-byte buffer intact");
    runner.expectTrue(endsWithCompleteUtf8(buf), "Bug-report title ends on UTF-8 boundary");
  }

  // --- Old buffer would have truncated mid-codepoint; copyUtf8Safe drops the partial char ---
  {
    // Same title, but force an undersized 64-byte buffer (the original cap).
    const char* full =
        "\xD0\x9F\xD1\x83\xD1\x82\xD0\xB5\xD0\xB2\xD0\xBE\xD0\xB4\xD0\xB8\xD1\x82\xD0\xB5\xD0\xBB\xD1\x8C "
        "\xC2\xAB"
        "\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE\xD1\x81\xD1\x82\xD0\xBE\xD0\xBF\xD0\xBE\xD0\xBC \xD0\xBF\xD0\xBE "
        "\xD0\x9C\xD0\xBB\xD0\xB5\xD1\x87\xD0\xBD\xD0\xBE\xD0\xBC\xD1\x83 "
        "\xD0\x9F\xD1\x83\xD1\x82\xD0\xB8"
        "\xC2\xBB";
    char buf[64] = {};
    copyUtf8Safe(buf, full, sizeof(buf));
    runner.expectTrue(endsWithCompleteUtf8(buf), "Truncated Cyrillic title ends on UTF-8 boundary");
    runner.expectTrue(strlen(buf) < 64, "Truncated string is shorter than capacity");
  }

  // --- Cut between continuation bytes of a 3-byte codepoint: drops partial codepoint ---
  {
    // "«" = 0xC2 0xAB (2-byte). Capacity 2 → only the lead byte fits (1 useable byte + null) → drop.
    const char* s = "\xC2\xAB";
    char buf[2] = {};
    copyUtf8Safe(buf, s, sizeof(buf));
    runner.expectTrue(strlen(buf) == 0, "Lead-byte-only fragment is dropped");
  }

  // --- Cut keeps complete 2-byte codepoint when it fully fits ---
  {
    // "«" = 0xC2 0xAB. Capacity 3 → both bytes + null fit.
    const char* s = "\xC2\xAB";
    char buf[3] = {};
    copyUtf8Safe(buf, s, sizeof(buf));
    runner.expectTrue(strcmp(buf, "\xC2\xAB") == 0, "Complete 2-byte codepoint preserved at capacity");
    runner.expectTrue(endsWithCompleteUtf8(buf), "Capacity-fit 2-byte ends on boundary");
  }

  // --- 3-byte codepoint cut mid-stream: drops partial codepoint ---
  {
    // CJK 一 = 0xE4 0xB8 0x80 (3-byte). Capacity 3 = 2 bytes + null → partial, drop.
    const char* s = "\xE4\xB8\x80";
    char buf[3] = {};
    copyUtf8Safe(buf, s, sizeof(buf));
    runner.expectTrue(strlen(buf) == 0, "Partial 3-byte CJK codepoint dropped");
  }

  // --- 3-byte codepoint preceded by ASCII: ASCII kept, partial dropped ---
  {
    // "A" + "一". Capacity 4 = 3 bytes + null → "A" + 2/3 bytes of CJK → keep "A", drop CJK.
    const char* s = "A\xE4\xB8\x80";
    char buf[4] = {};
    copyUtf8Safe(buf, s, sizeof(buf));
    runner.expectTrue(strcmp(buf, "A") == 0, "ASCII prefix preserved when next codepoint is partial");
  }

  // --- 4-byte emoji cut: drops partial codepoint ---
  {
    // 😀 = 0xF0 0x9F 0x98 0x80 (4-byte). Capacity 4 = 3 bytes + null → partial, drop.
    const char* s = "\xF0\x9F\x98\x80";
    char buf[4] = {};
    copyUtf8Safe(buf, s, sizeof(buf));
    runner.expectTrue(strlen(buf) == 0, "Partial 4-byte emoji dropped");
  }

  // --- Empty input ---
  {
    char buf[16] = {'X'};
    copyUtf8Safe(buf, "", sizeof(buf));
    runner.expectTrue(strlen(buf) == 0, "Empty input yields empty buffer");
  }

  // --- ASCII overflow: cut at byte boundary, no codepoint cleanup needed ---
  {
    char buf[8] = {};
    copyUtf8Safe(buf, "abcdefghijklmnop", sizeof(buf));
    runner.expectTrue(strcmp(buf, "abcdefg") == 0, "ASCII overflow truncated at capacity-1");
  }

  return runner.allPassed() ? 0 : 1;
}
