#include "test_utils.h"

#include "platform_stubs.h"

// Include dependencies and source under test
#include "Utf8.cpp"
#include "EpdFont.cpp"
#include "EpdFontFamily.cpp"

int main() {
  TestUtils::TestRunner runner("EpdFontFamily");

  // Create distinct EpdFontData structs on the stack
  EpdFontData regularData = {};
  regularData.advanceY = 20;
  EpdFont regularFont(&regularData);

  EpdFontData boldData = {};
  boldData.advanceY = 24;
  EpdFont boldFont(&boldData);

  EpdFontData italicData = {};
  italicData.advanceY = 22;
  EpdFont italicFont(&italicData);

  EpdFontData boldItalicData = {};
  boldItalicData.advanceY = 26;
  EpdFont boldItalicFont(&boldItalicData);

  EpdFontData otherData = {};
  otherData.advanceY = 30;
  EpdFont otherFont(&otherData);

  // Const pointers for expectEq (getData returns const EpdFontData*)
  const EpdFontData* pRegular = &regularData;
  const EpdFontData* pBold = &boldData;
  const EpdFontData* pItalic = &italicData;
  const EpdFontData* pBoldItalic = &boldItalicData;

  // ============================================
  // Constructor and Fallback Tests
  // ============================================

  // Test 1: Regular-only family falls back to regular for all styles
  {
    EpdFontFamily family(&regularFont);
    runner.expectEq(pRegular, family.getData(EpdFontFamily::REGULAR),
                    "constructor_regular_only: getData(REGULAR) returns regular");
    runner.expectEq(pRegular, family.getData(EpdFontFamily::BOLD),
                    "constructor_regular_only: getData(BOLD) falls back to regular");
    runner.expectEq(pRegular, family.getData(EpdFontFamily::ITALIC),
                    "constructor_regular_only: getData(ITALIC) falls back to regular");
    runner.expectEq(pRegular, family.getData(EpdFontFamily::BOLD_ITALIC),
                    "constructor_regular_only: getData(BOLD_ITALIC) falls back to regular");
  }

  // ============================================
  // setFont Tests
  // ============================================

  // Test 2: setFont(BOLD) updates bold slot
  {
    EpdFontFamily family(&regularFont);
    family.setFont(EpdFontFamily::BOLD, &boldFont);
    runner.expectEq(pBold, family.getData(EpdFontFamily::BOLD), "setFont_bold: getData(BOLD) returns bold");
    runner.expectEq(pRegular, family.getData(EpdFontFamily::REGULAR),
                    "setFont_bold: getData(REGULAR) unchanged");
  }

  // Test 3: setFont(ITALIC) updates italic slot
  {
    EpdFontFamily family(&regularFont);
    family.setFont(EpdFontFamily::ITALIC, &italicFont);
    runner.expectEq(pItalic, family.getData(EpdFontFamily::ITALIC),
                    "setFont_italic: getData(ITALIC) returns italic");
    runner.expectEq(pRegular, family.getData(EpdFontFamily::REGULAR),
                    "setFont_italic: getData(REGULAR) unchanged");
  }

  // Test 4: setFont(BOLD_ITALIC) updates bold-italic slot
  {
    EpdFontFamily family(&regularFont);
    family.setFont(EpdFontFamily::BOLD_ITALIC, &boldItalicFont);
    runner.expectEq(pBoldItalic, family.getData(EpdFontFamily::BOLD_ITALIC),
                    "setFont_bold_italic: getData(BOLD_ITALIC) returns bold_italic");
  }

  // Test 5: setFont(REGULAR) is a no-op
  {
    EpdFontFamily family(&regularFont);
    family.setFont(EpdFontFamily::REGULAR, &otherFont);
    runner.expectEq(pRegular, family.getData(EpdFontFamily::REGULAR),
                    "setFont_regular_noop: getData(REGULAR) still returns original");
  }

  // Test 6: setFont(BOLD, nullptr) clears bold, falls back to regular
  {
    EpdFontFamily family(&regularFont, &boldFont);
    runner.expectEq(pBold, family.getData(EpdFontFamily::BOLD),
                    "setFont_nullptr_clears: bold initially set");
    family.setFont(EpdFontFamily::BOLD, nullptr);
    runner.expectEq(pRegular, family.getData(EpdFontFamily::BOLD),
                    "setFont_nullptr_clears: getData(BOLD) falls back to regular after clear");
  }

  // Test 7: BOLD_ITALIC fallback chain: boldItalic -> bold -> italic -> regular
  {
    EpdFontFamily family(&regularFont);
    family.setFont(EpdFontFamily::BOLD, &boldFont);

    // BOLD_ITALIC with no boldItalic set: falls back to bold
    runner.expectEq(pBold, family.getData(EpdFontFamily::BOLD_ITALIC),
                    "bold_italic_fallback: falls back to bold when no boldItalic");

    // Now set boldItalic explicitly
    family.setFont(EpdFontFamily::BOLD_ITALIC, &boldItalicFont);
    runner.expectEq(pBoldItalic, family.getData(EpdFontFamily::BOLD_ITALIC),
                    "bold_italic_fallback: returns boldItalic after setFont");
  }

  // ============================================
  // externalStyleIndex Tests
  // External/streaming fonts only have REGULAR + BOLD
  // ============================================

  // Test 8: externalStyleIndex maps REGULAR → REGULAR
  {
    runner.expectEq(static_cast<int>(EpdFontFamily::REGULAR),
                    EpdFontFamily::externalStyleIndex(EpdFontFamily::REGULAR),
                    "externalStyleIndex: REGULAR → REGULAR");
  }

  // Test 9: externalStyleIndex maps BOLD → BOLD
  {
    runner.expectEq(static_cast<int>(EpdFontFamily::BOLD),
                    EpdFontFamily::externalStyleIndex(EpdFontFamily::BOLD),
                    "externalStyleIndex: BOLD → BOLD");
  }

  // Test 10: externalStyleIndex maps ITALIC → REGULAR
  {
    runner.expectEq(static_cast<int>(EpdFontFamily::REGULAR),
                    EpdFontFamily::externalStyleIndex(EpdFontFamily::ITALIC),
                    "externalStyleIndex: ITALIC → REGULAR");
  }

  // Test 11: externalStyleIndex maps BOLD_ITALIC → BOLD
  {
    runner.expectEq(static_cast<int>(EpdFontFamily::BOLD),
                    EpdFontFamily::externalStyleIndex(EpdFontFamily::BOLD_ITALIC),
                    "externalStyleIndex: BOLD_ITALIC → BOLD");
  }

  // Test 12: kExternalStyleCount is 2 (REGULAR + BOLD)
  {
    runner.expectEq(2, EpdFontFamily::kExternalStyleCount, "kExternalStyleCount is 2");
  }

  return runner.allPassed() ? 0 : 1;
}
