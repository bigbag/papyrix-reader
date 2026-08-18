#include <cstring>
#include <string>
#include <vector>

#include "Utf8Nfc.h"
#include "test_utils.h"

// Pull in the implementation directly (same pattern as Utf8Test.cpp)
#include "Utf8Nfc.cpp"

int main() {
  TestUtils::TestRunner runner("UTF-8 NFC Normalization");

  // ============================================
  // ASCII passthrough
  // ============================================

  {
    char buf[] = "Hello, world!";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEq(static_cast<size_t>(13), len, "ASCII passthrough: length unchanged");
    runner.expectEqual("Hello, world!", std::string(buf, len), "ASCII passthrough: content unchanged");
  }

  {
    char buf[] = "";
    size_t len = utf8NormalizeNfc(buf, 0);
    runner.expectEq(static_cast<size_t>(0), len, "Empty string: length 0");
  }

  // ============================================
  // Already NFC (no change)
  // ============================================

  {
    // é (U+00E9) is already NFC
    char buf[] = "caf\xC3\xA9";  // "café"
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("caf\xC3\xA9", std::string(buf, len), "Already NFC: café unchanged");
  }

  {
    // Ấ (U+1EA4) already precomposed
    char buf[] = "\xE1\xBA\xA4";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xE1\xBA\xA4", std::string(buf, len), "Already NFC: Ấ unchanged");
  }

  // ============================================
  // Simple NFD → NFC (one combining mark)
  // ============================================

  {
    // e + combining acute = é  (U+0065 + U+0301 → U+00E9)
    char buf[] = "e\xCC\x81";  // e + combining acute accent
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xC3\xA9", std::string(buf, len), "NFD e+acute → NFC é");
  }

  {
    // A + combining grave = À  (U+0041 + U+0300 → U+00C0)
    char buf[] = "A\xCC\x80";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xC3\x80", std::string(buf, len), "NFD A+grave → NFC À");
  }

  {
    // o + combining tilde = õ  (U+006F + U+0303 → U+00F5)
    char buf[] = "o\xCC\x83";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xC3\xB5", std::string(buf, len), "NFD o+tilde → NFC õ");
  }

  // ============================================
  // Vietnamese: two combining marks (3-char NFD)
  // ============================================

  {
    // Ấ = A + circumflex + acute  (U+0041 + U+0302 + U+0301 → U+1EA4)
    // Step 1: A + circumflex → Â (U+00C2)
    // Step 2: Â + acute → Ấ (U+1EA4)
    char buf[] = "A\xCC\x82\xCC\x81";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xE1\xBA\xA4", std::string(buf, len), "Vietnamese Ấ: A+circumflex+acute → Ấ");
  }

  {
    // ề = e + circumflex + grave  (U+0065 + U+0302 + U+0300 → U+1EC1)
    // Step 1: e + circumflex → ê (U+00EA)
    // Step 2: ê + grave → ề (U+1EC1)
    char buf[] = "e\xCC\x82\xCC\x80";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xE1\xBB\x81", std::string(buf, len), "Vietnamese ề: e+circumflex+grave → ề");
  }

  {
    // ổ = o + circumflex + hook above (U+006F + U+0302 + U+0309 → U+1ED5)
    char buf[] = "o\xCC\x82\xCC\x89";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xE1\xBB\x95", std::string(buf, len), "Vietnamese ổ: o+circumflex+hook → ổ");
  }

  {
    // ữ = u + horn + tilde (U+0075 + U+031B + U+0303 → U+1EEF)
    // Step 1: u + horn → ư (U+01B0)
    // Step 2: ư + tilde → ữ (U+1EEF)
    char buf[] = "u\xCC\x9B\xCC\x83";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xE1\xBB\xAF", std::string(buf, len), "Vietnamese ữ: u+horn+tilde → ữ");
  }

  // ============================================
  // Vietnamese word
  // ============================================

  {
    // "Việt" in NFD: V + i + e + dot_below + circumflex + t
    // Canonical order: dot_below (CCC 220) before circumflex (CCC 230)
    // e + dot_below → ẹ (U+1EB9), ẹ + circumflex → ệ (U+1EC7)
    // NFD: V i e U+0323 U+0302 t
    char buf[] = "Vi\x65\xCC\xA3\xCC\x82t";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("Vi\xE1\xBB\x87t", std::string(buf, len), "Vietnamese word: Việt");
  }

  // ============================================
  // Mixed content
  // ============================================

  {
    // "café" with NFD é
    char buf[] =
        "caf"
        "e\xCC\x81";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("caf\xC3\xA9", std::string(buf, len), "Mixed: café with NFD e+acute");
  }

  {
    // Multiple words with accents
    char buf[] =
        "a\xCC\x80 "
        "e\xCC\x81";  // "à é" in NFD
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xC3\xA0 \xC3\xA9", std::string(buf, len), "Mixed: à é");
  }

  // ============================================
  // Combining mark with no composition (passthrough)
  // ============================================

  {
    // x + combining acute — no composition exists for x+acute
    char buf[] = "x\xCC\x81";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("x\xCC\x81", std::string(buf, len), "No composition: x+acute stays as-is");
  }

  // ============================================
  // Cyrillic
  // ============================================

  {
    // й = и + combining breve (U+0438 + U+0306 → U+0439)
    char buf[] = "\xD0\xB8\xCC\x86";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xD0\xB9", std::string(buf, len), "Cyrillic: и+breve → й");
  }

  // ============================================
  // Output length shrinks correctly
  // ============================================

  {
    // e + acute: 1 + 2 = 3 bytes NFD → 2 bytes NFC
    char buf[] = "e\xCC\x81";
    size_t origLen = strlen(buf);
    size_t newLen = utf8NormalizeNfc(buf, origLen);
    runner.expectTrue(newLen < origLen, "NFC output shorter than NFD input");
    runner.expectEq(static_cast<size_t>(2), newLen, "é is 2 bytes in UTF-8");
  }

  // ============================================
  // Null termination
  // ============================================

  {
    char buf[] = "e\xCC\x81 end";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEq('\0', buf[len], "Null terminator placed at new length");
  }

  // ============================================
  // Greek
  // ============================================

  {
    // Ά = Α + combining acute (U+0391 + U+0301 → U+0386)
    char buf[] = "\xCE\x91\xCC\x81";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("\xCE\x86", std::string(buf, len), "Greek: Α+acute → Ά");
  }

  // ============================================
  // Combining mark at start (orphan, no base to compose with)
  // ============================================

  {
    // Combining acute at start — should pass through unchanged
    char buf[] =
        "\xCC\x81"
        "abc";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual(
        "\xCC\x81"
        "abc",
        std::string(buf, len), "Orphan combining mark at start: unchanged");
  }

  // ============================================
  // Non-BMP codepoints (4-byte UTF-8, e.g. emoji)
  // ============================================

  {
    // U+1F600 (😀) should pass through unchanged
    char buf[] = "hi \xF0\x9F\x98\x80 ok";
    size_t len = utf8NormalizeNfc(buf, strlen(buf));
    runner.expectEqual("hi \xF0\x9F\x98\x80 ok", std::string(buf, len), "Non-BMP emoji: unchanged");
  }

  // ============================================
  // Malformed UTF-8 (truncated sequence)
  // ============================================

  {
    // Truncated 2-byte sequence at end
    char buf[] = "abc\xC3";
    size_t len = utf8NormalizeNfc(buf, 4);
    runner.expectEq(size_t(4), len, "Truncated UTF-8: output does not expand");
    runner.expectEqual("abc?", std::string(buf, len), "Truncated UTF-8: malformed byte is replaced safely");
  }

  // ============================================
  // Long string (exceeds the stack buffer, exercises heap path)
  // ============================================

  {
    // 300 'a' chars + NFD é at end
    std::string input(300, 'a');
    input += "e\xCC\x81";
    std::string expected(300, 'a');
    expected += "\xC3\xA9";

    // Need mutable buffer
    std::vector<char> buf(input.begin(), input.end());
    buf.push_back('\0');
    size_t len = utf8NormalizeNfc(buf.data(), input.size());
    runner.expectEqual(expected, std::string(buf.data(), len), "Heap path: 300+ codepoints with NFC at end");
  }

  return runner.allPassed() ? 0 : 1;
}
