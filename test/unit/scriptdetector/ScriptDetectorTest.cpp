#include "test_utils.h"

#include <cstdint>

#include "ScriptDetector.h"

int main() {
  TestUtils::TestRunner runner("ScriptDetector");

  // ============================================
  // isArabicCodepoint() tests
  // ============================================

  // Test 1: Arabic block (U+0600-U+06FF)
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0x0600), "isArabic: U+0600 start of Arabic block");
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0x0628), "isArabic: U+0628 Beh");
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0x064A), "isArabic: U+064A Yeh");
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0x06FF), "isArabic: U+06FF end of Arabic block");

  // Test 2: Arabic Supplement (U+0750-U+077F)
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0x0750), "isArabic: U+0750 start of Arabic Supplement");
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0x077F), "isArabic: U+077F end of Arabic Supplement");

  // Test 3: Arabic Presentation Forms-A (U+FB50-U+FDFF)
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0xFB50), "isArabic: U+FB50 start of Pres. Forms-A");
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0xFDFF), "isArabic: U+FDFF end of Pres. Forms-A");

  // Test 4: Arabic Presentation Forms-B (U+FE70-U+FEFE; U+FEFF excluded as BOM/ZWNBSP)
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0xFE70), "isArabic: U+FE70 start of Pres. Forms-B");
  runner.expectTrue(ScriptDetector::isArabicCodepoint(0xFEFE), "isArabic: U+FEFE end of Pres. Forms-B");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0xFEFF), "isArabic: U+FEFF (BOM/ZWNBSP) not Arabic");

  // Test 5: Boundary - not Arabic
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0x05FF), "isArabic: U+05FF before Arabic block");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0x0700), "isArabic: U+0700 after Arabic block");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0x074F), "isArabic: U+074F before Arabic Supplement");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0x0780), "isArabic: U+0780 after Arabic Supplement");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0xFB4F), "isArabic: U+FB4F before Pres. Forms-A");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0xFE00), "isArabic: U+FE00 after Pres. Forms-A");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0xFE6F), "isArabic: U+FE6F before Pres. Forms-B");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0xFF00), "isArabic: U+FF00 after Pres. Forms-B");

  // Test 6: ASCII and common scripts not Arabic
  runner.expectFalse(ScriptDetector::isArabicCodepoint('A'), "isArabic: ASCII 'A'");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0x4E2D), "isArabic: CJK char");
  runner.expectFalse(ScriptDetector::isArabicCodepoint(0x0E01), "isArabic: Thai char");

  // ============================================
  // containsArabic() tests
  // ============================================

  // Test 7: nullptr returns false
  runner.expectFalse(ScriptDetector::containsArabic(nullptr), "containsArabic: nullptr");

  // Test 8: Empty string
  runner.expectFalse(ScriptDetector::containsArabic(""), "containsArabic: empty string");

  // Test 9: Pure ASCII
  runner.expectFalse(ScriptDetector::containsArabic("Hello World"), "containsArabic: ASCII only");

  // Test 10: Arabic text (Beh U+0628 = 0xD8 0xA8)
  runner.expectTrue(ScriptDetector::containsArabic("\xD8\xA8"), "containsArabic: single Arabic char");

  // Test 11: Mixed ASCII + Arabic
  runner.expectTrue(ScriptDetector::containsArabic("Hello \xD8\xA8 World"), "containsArabic: mixed text");

  // Test 12: CJK text (not Arabic)
  runner.expectFalse(ScriptDetector::containsArabic("\xE4\xB8\xAD\xE6\x96\x87"), "containsArabic: CJK not Arabic");

  // Test 13: Thai text (not Arabic)
  runner.expectFalse(ScriptDetector::containsArabic("\xE0\xB8\x81"), "containsArabic: Thai not Arabic");

  // Test 14: Arabic Presentation Form-B char (Fathatan U+FE70 = 0xEF 0xB9 0xB0)
  runner.expectTrue(ScriptDetector::containsArabic("\xEF\xB9\xB0"), "containsArabic: Pres. Form-B char");

  // Test 14a: UTF-8 BOM alone must NOT be classified as Arabic (regression for issue #99 —
  // previously U+FEFF was in the Arabic range, making every UTF-8-with-BOM file render RTL).
  runner.expectFalse(ScriptDetector::containsArabic("\xEF\xBB\xBF"), "containsArabic: UTF-8 BOM alone");
  runner.expectFalse(ScriptDetector::containsArabic("\xEF\xBB\xBFHello"), "containsArabic: BOM + ASCII");
  // BOM + Cyrillic "Дуглас" (U+0414 U+0443 U+0433 U+043B U+0430 U+0441)
  runner.expectFalse(ScriptDetector::containsArabic("\xEF\xBB\xBF\xD0\x94\xD1\x83\xD0\xB3\xD0\xBB\xD0\xB0\xD1\x81"),
                     "containsArabic: BOM + Cyrillic");

  // ============================================
  // classify() tests (Arabic additions)
  // ============================================

  // Test 15: Arabic word classified as ARABIC
  runner.expectTrue(ScriptDetector::classify("\xD8\xA8\xD8\xB3\xD9\x85") == ScriptDetector::Script::ARABIC,
                    "classify: Arabic word");

  // Test 16: Mixed ASCII prefix + Arabic classified as ARABIC
  runner.expectTrue(ScriptDetector::classify("abc\xD8\xA8") == ScriptDetector::Script::ARABIC,
                    "classify: ASCII+Arabic = ARABIC");

  // Test 17: Pure ASCII classified as LATIN
  runner.expectTrue(ScriptDetector::classify("Hello") == ScriptDetector::Script::LATIN,
                    "classify: pure ASCII = LATIN");

  // Test 18: Empty/null classified as OTHER
  runner.expectTrue(ScriptDetector::classify("") == ScriptDetector::Script::OTHER,
                    "classify: empty = OTHER");
  runner.expectTrue(ScriptDetector::classify(nullptr) == ScriptDetector::Script::OTHER,
                    "classify: nullptr = OTHER");

  // Test 19: Thai classified as THAI (before Arabic in check order)
  runner.expectTrue(ScriptDetector::classify("\xE0\xB8\x81") == ScriptDetector::Script::THAI,
                    "classify: Thai char = THAI");

  // Test 20: CJK classified as CJK
  runner.expectTrue(ScriptDetector::classify("\xE4\xB8\xAD") == ScriptDetector::Script::CJK,
                    "classify: CJK char = CJK");

  // ============================================
  // isCjkCodepoint() boundary tests
  // ============================================

  // Test 21: CJK boundaries
  runner.expectTrue(ScriptDetector::isCjkCodepoint(0x4E00), "isCjk: U+4E00 start of CJK Unified");
  runner.expectTrue(ScriptDetector::isCjkCodepoint(0x9FFF), "isCjk: U+9FFF end of CJK Unified");
  runner.expectFalse(ScriptDetector::isCjkCodepoint(0x4DFF), "isCjk: U+4DFF before CJK Unified");
  runner.expectFalse(ScriptDetector::isCjkCodepoint(0xA000), "isCjk: U+A000 after CJK Unified");

  // Test 22: isThaiCodepoint inline
  runner.expectTrue(ScriptDetector::isThaiCodepoint(0x0E00), "isThai: U+0E00 start");
  runner.expectTrue(ScriptDetector::isThaiCodepoint(0x0E7F), "isThai: U+0E7F end");
  runner.expectFalse(ScriptDetector::isThaiCodepoint(0x0DFF), "isThai: U+0DFF before");
  runner.expectFalse(ScriptDetector::isThaiCodepoint(0x0E80), "isThai: U+0E80 after");

  return runner.allPassed() ? 0 : 1;
}
