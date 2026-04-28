#include "test_utils.h"

#include <cstring>

#include "EncodingDetector.h"
#include "ScriptDetector.h"

static bool isRtlForBuffer(const uint8_t* data, size_t len) {
  size_t bomBytes = 0;
  Encoding enc = detectEncoding(data, len, bomBytes);
  if (enc == Encoding::Utf8) {
    return ScriptDetector::containsArabic(reinterpret_cast<const char*>(data));
  }
  return false;
}

int main() {
  TestUtils::TestRunner runner("ScriptDetector Encoding Gate");

  // Windows-1251 Cyrillic must NOT be detected as RTL.
  // Raw bytes 0xD9 0xC5 (ЩЕ in CP1251) would decode as U+0645 (Arabic MEEM)
  // if fed to utf8NextCodepoint, which doesn't validate continuation bytes.
  {
    // "ВОЗВРАЩЕНИЕ" in CP1251
    const uint8_t data[] = {0xC2, 0xCE, 0xC7, 0xC2, 0xD0, 0xC0, 0xD9, 0xC5, 0xCD, 0xC8, 0xC5};
    runner.expectFalse(isRtlForBuffer(data, sizeof(data)), "CP1251 Cyrillic: not RTL");
  }

  // KOI8-R Cyrillic must NOT be detected as RTL.
  {
    // "Война и мир" in KOI8-R
    const uint8_t data[] = {0xF7, 0xCF, 0xCA, 0xCE, 0xC1, 0x20, 0xC9, 0x20, 0xCD, 0xC9, 0xD2};
    runner.expectFalse(isRtlForBuffer(data, sizeof(data)), "KOI8-R Cyrillic: not RTL");
  }

  // UTF-8 Arabic text MUST be detected as RTL.
  {
    // "بسم" (Bism) in UTF-8: U+0628 U+0633 U+0645
    const uint8_t data[] = {0xD8, 0xA8, 0xD8, 0xB3, 0xD9, 0x85};
    runner.expectTrue(isRtlForBuffer(data, sizeof(data)), "UTF-8 Arabic: RTL detected");
  }

  // UTF-8 Cyrillic must NOT be detected as RTL.
  {
    const uint8_t data[] = u8"Привет мир";
    runner.expectFalse(isRtlForBuffer(data, sizeof(data) - 1), "UTF-8 Cyrillic: not RTL");
  }

  // Pure ASCII must NOT be detected as RTL.
  {
    const uint8_t data[] = "Hello world";
    runner.expectFalse(isRtlForBuffer(data, sizeof(data) - 1), "ASCII: not RTL");
  }

  // UTF-8 BOM + Arabic must be detected as RTL.
  {
    const uint8_t data[] = {0xEF, 0xBB, 0xBF, 0xD8, 0xA8};
    runner.expectTrue(isRtlForBuffer(data, sizeof(data)), "BOM + Arabic: RTL detected");
  }

  // UTF-8 BOM + Cyrillic must NOT be detected as RTL.
  {
    // BOM + "Дуглас"
    const uint8_t data[] = {0xEF, 0xBB, 0xBF, 0xD0, 0x94, 0xD1, 0x83, 0xD0, 0xB3, 0xD0, 0xBB, 0xD0, 0xB0, 0xD1, 0x81};
    runner.expectFalse(isRtlForBuffer(data, sizeof(data)), "BOM + Cyrillic: not RTL");
  }

  // CP1252 Western European must NOT be detected as RTL.
  {
    // "café" in CP1252: c=0x63 a=0x61 f=0x66 é=0xE9
    const uint8_t data[] = {0x63, 0x61, 0x66, 0xE9};
    runner.expectFalse(isRtlForBuffer(data, sizeof(data)), "CP1252: not RTL");
  }

  // Realistic CP1251 FB2 header with XML declaration must NOT be RTL.
  // This mimics what Fb2Parser sees in the first chunk.
  {
    const char xml[] = "<?xml version=\"1.0\" encoding=\"windows-1251\"?>\r\n"
                       "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">\r\n"
                       " <description><title-info>\r\n"
                       "  <book-title>\xC2\xCE\xC7\xC2\xD0\xC0\xD9\xC5\xCD\xC8\xC5</book-title>\r\n"
                       " </title-info></description></FictionBook>";
    runner.expectFalse(isRtlForBuffer(reinterpret_cast<const uint8_t*>(xml), strlen(xml)),
                       "CP1251 FB2 header: not RTL");
  }

  // Confirm raw containsArabic would false-positive on CP1251 bytes (documents the bug).
  // 0xD9 0xC5 = ЩЕ in CP1251, but utf8NextCodepoint decodes it as U+0645 (Arabic MEEM).
  {
    const char cp1251[] = {'\xD9', '\xC5', '\0'};
    runner.expectTrue(ScriptDetector::containsArabic(cp1251),
                      "Raw CP1251 0xD9 0xC5 false-positives as Arabic (documents bug)");
  }

  return runner.allPassed() ? 0 : 1;
}
