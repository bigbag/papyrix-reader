#include "test_utils.h"

#include <EncodingDetector.h>

#include <cstring>

int main() {
  TestUtils::TestRunner runner("Encoding Detector");
  size_t bomBytes = 0;

  // Pure ASCII
  {
    const uint8_t data[] = "Hello, world! This is plain ASCII text.";
    Encoding enc = detectEncoding(data, sizeof(data) - 1, bomBytes);
    runner.expectTrue(enc == Encoding::Utf8, "Pure ASCII detected as UTF-8");
    runner.expectTrue(bomBytes == 0, "No BOM in ASCII");
  }

  // Valid UTF-8 with Cyrillic
  {
    const uint8_t data[] = u8"Привет, мир! Hello world.";
    Encoding enc = detectEncoding(data, sizeof(data) - 1, bomBytes);
    runner.expectTrue(enc == Encoding::Utf8, "Valid UTF-8 Cyrillic detected as UTF-8");
  }

  // UTF-8 BOM
  {
    const uint8_t data[] = {0xEF, 0xBB, 0xBF, 'H', 'e', 'l', 'l', 'o'};
    Encoding enc = detectEncoding(data, sizeof(data), bomBytes);
    runner.expectTrue(enc == Encoding::Utf8, "UTF-8 BOM detected as UTF-8");
    runner.expectTrue(bomBytes == 3, "BOM skip = 3 bytes");
  }

  // Windows-1251 Cyrillic text
  {
    // "Война и мир" in CP1251: В=0xC2 о=0xEE й=0xE9 н=0xED а=0xE0
    // In CP1251, lowercase is 0xE0-0xFF, uppercase is 0xC0-0xDF
    // Natural Russian text has more lowercase -> more bytes in 0xE0-0xFF
    const uint8_t data[] = {
        0xC2, 0xEE, 0xE9, 0xED, 0xE0, 0x20,  // "Война "
        0xE8, 0x20,                            // "и "
        0xEC, 0xE8, 0xF0,                      // "мир"
    };
    Encoding enc = detectEncoding(data, sizeof(data), bomBytes);
    runner.expectTrue(enc == Encoding::Windows1251, "CP1251 Cyrillic detected");
    runner.expectTrue(bomBytes == 0, "No BOM in CP1251");
  }

  // KOI8-R Cyrillic text
  {
    // "Война и мир" in KOI8-R: В=0xF7 о=0xCF й=0xCA н=0xCE а=0xC1
    // In KOI8-R, lowercase is 0xC0-0xDF, uppercase is 0xE0-0xFF
    // Natural Russian text has more lowercase -> more bytes in 0xC0-0xDF
    const uint8_t data[] = {
        0xF7, 0xCF, 0xCA, 0xCE, 0xC1, 0x20,  // "Война "
        0xC9, 0x20,                            // "и "
        0xCD, 0xC9, 0xD2,                      // "мир"
    };
    Encoding enc = detectEncoding(data, sizeof(data), bomBytes);
    runner.expectTrue(enc == Encoding::Koi8R, "KOI8-R Cyrillic detected");
  }

  // codepointToUtf8: ASCII
  {
    char buf[4];
    int len = codepointToUtf8('A', buf);
    runner.expectTrue(len == 1, "codepointToUtf8: ASCII -> 1 byte");
    runner.expectTrue(buf[0] == 'A', "codepointToUtf8: ASCII codepoint preserved");
  }

  // codepointToUtf8: 2-byte (Cyrillic A = U+0410)
  {
    char buf[4];
    int len = codepointToUtf8(0x0410, buf);
    runner.expectTrue(len == 2, "codepointToUtf8: Cyrillic -> 2 bytes");
    runner.expectTrue(static_cast<uint8_t>(buf[0]) == 0xD0, "codepointToUtf8: first byte 0xD0");
    runner.expectTrue(static_cast<uint8_t>(buf[1]) == 0x90, "codepointToUtf8: second byte 0x90");
  }

  // codepointToUtf8: 3-byte (Euro sign = U+20AC)
  {
    char buf[4];
    int len = codepointToUtf8(0x20AC, buf);
    runner.expectTrue(len == 3, "codepointToUtf8: Euro sign -> 3 bytes");
    runner.expectTrue(static_cast<uint8_t>(buf[0]) == 0xE2, "codepointToUtf8: first byte 0xE2");
    runner.expectTrue(static_cast<uint8_t>(buf[1]) == 0x82, "codepointToUtf8: second byte 0x82");
    runner.expectTrue(static_cast<uint8_t>(buf[2]) == 0xAC, "codepointToUtf8: third byte 0xAC");
  }

  // getEncodingTable
  {
    runner.expectTrue(getEncodingTable(Encoding::Utf8) == nullptr, "getEncodingTable: UTF-8 -> nullptr");
    runner.expectTrue(getEncodingTable(Encoding::Windows1251) == kWindows1251High, "getEncodingTable: CP1251");
    runner.expectTrue(getEncodingTable(Encoding::Koi8R) == kKoi8RHigh, "getEncodingTable: KOI8-R");
    runner.expectTrue(getEncodingTable(Encoding::Iso8859_1) == kIso8859_1High, "getEncodingTable: ISO-8859-1");
    runner.expectTrue(getEncodingTable(Encoding::Cp1252) == kCp1252High, "getEncodingTable: CP1252");
  }

  // CP1251 table spot checks
  {
    runner.expectTrue(kWindows1251High[0xC0 - 0x80] == 0x0410, "CP1251 0xC0 -> U+0410 (A)");
    runner.expectTrue(kWindows1251High[0xE0 - 0x80] == 0x0430, "CP1251 0xE0 -> U+0430 (a)");
  }

  // KOI8-R table spot checks
  {
    runner.expectTrue(kKoi8RHigh[0xE1 - 0x80] == 0x0410, "KOI8-R 0xE1 -> U+0410 (A)");
    runner.expectTrue(kKoi8RHigh[0xC1 - 0x80] == 0x0430, "KOI8-R 0xC1 -> U+0430 (a)");
  }

  // Empty buffer
  {
    Encoding enc = detectEncoding(nullptr, 0, bomBytes);
    runner.expectTrue(enc == Encoding::Utf8, "Empty buffer -> Utf8");
    runner.expectTrue(bomBytes == 0, "No BOM in empty buffer");
  }

  // Non-Cyrillic high bytes
  {
    const uint8_t data[] = {0xC4, 0xE4, 0xD6, 0xF6, 0xDC, 0xFC, 0xDF};
    Encoding enc = detectEncoding(data, sizeof(data), bomBytes);
    runner.expectTrue(enc != Encoding::Utf8, "High-byte content not detected as UTF-8");
  }

  return runner.allPassed() ? 0 : 1;
}
