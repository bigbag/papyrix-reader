#include "test_utils.h"

// Include mocks before the library
#include "LittleFS.h"
#include "SDCardManager.h"
#include "SdFat.h"
#include "platform_stubs.h"

// Test data generator
#include "test_font_data.h"

// Include the library under test
#include "EpdFontLoader.cpp"

int main() {
  TestUtils::TestRunner runner("EpdFontLoaderStreaming");

  // ============================================
  // loadForStreaming() Tests
  // ============================================

  // Test 1: loadForStreaming_success
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");

    runner.expectTrue(result.success, "loadForStreaming_success: returns true for valid file");
    runner.expectTrue(result.glyphs != nullptr, "loadForStreaming_success: glyphs allocated");
    runner.expectTrue(result.intervals != nullptr, "loadForStreaming_success: intervals allocated");
    runner.expectTrue(result.glyphCount > 0, "loadForStreaming_success: glyphCount > 0");
    runner.expectEq(static_cast<uint8_t>(20), result.fontData.advanceY, "loadForStreaming_success: advanceY correct");

    EpdFontLoader::freeStreamingResult(result);
  }

  // Test 2: loadForStreaming_failure_file_not_found
  {
    SdMan.clearFiles();

    auto result = EpdFontLoader::loadForStreaming("/fonts/nonexistent.epdfont");

    runner.expectFalse(result.success, "loadForStreaming_failure_file_not_found: returns false");
    runner.expectTrue(result.glyphs == nullptr, "loadForStreaming_failure_file_not_found: glyphs is nullptr");
    runner.expectTrue(result.intervals == nullptr, "loadForStreaming_failure_file_not_found: intervals is nullptr");
  }

  // Test 3: loadForStreaming_bitmap_offset_correct
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");

    runner.expectTrue(result.success, "loadForStreaming_bitmap_offset: load succeeded");

    // Bitmap offset should be after header + metrics + intervals + glyphs
    // Header = 16, Metrics = 18, then intervals and glyphs
    size_t expectedMinOffset = TestFontData::HEADER_SIZE + TestFontData::METRICS_SIZE;
    runner.expectTrue(result.bitmapOffset >= expectedMinOffset,
                      "loadForStreaming_bitmap_offset: offset is past header/metrics");

    // The bitmap offset should point to where bitmap data starts
    // This is verified by the data structure being valid
    runner.expectTrue(result.bitmapOffset > 0, "loadForStreaming_bitmap_offset: offset > 0");

    EpdFontLoader::freeStreamingResult(result);
  }

  // Test 4: loadForStreaming_glyphCount_matches
  {
    SdMan.clearFiles();

    // Generate a font with known number of glyphs
    // Basic ASCII font has: 26 uppercase + 26 lowercase + space + '?' = 54 glyphs
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");

    runner.expectTrue(result.success, "loadForStreaming_glyphCount_matches: load succeeded");
    runner.expectEq(static_cast<uint32_t>(54), result.glyphCount,
                    "loadForStreaming_glyphCount_matches: correct glyph count");

    EpdFontLoader::freeStreamingResult(result);
  }

  // Test 5: loadForStreaming_multi_interval
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateMultiIntervalFont();
    SdMan.registerFile("/fonts/multi.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/multi.epdfont");

    runner.expectTrue(result.success, "loadForStreaming_multi_interval: load succeeded");
    // Multi-interval font: 10 digits + 26 uppercase + 26 lowercase = 62 glyphs
    runner.expectEq(static_cast<uint32_t>(62), result.glyphCount,
                    "loadForStreaming_multi_interval: correct glyph count");
    // Should have 3 intervals (digits, uppercase, lowercase)
    runner.expectEq(static_cast<uint32_t>(3), result.fontData.intervalCount,
                    "loadForStreaming_multi_interval: correct interval count");

    EpdFontLoader::freeStreamingResult(result);
  }

  // ============================================
  // freeStreamingResult() Tests
  // ============================================

  // Test 6: freeStreamingResult_cleans_up
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");
    runner.expectTrue(result.success, "freeStreamingResult_cleans_up: load succeeded");
    runner.expectTrue(result.glyphs != nullptr, "freeStreamingResult_cleans_up: glyphs not null before free");

    EpdFontLoader::freeStreamingResult(result);

    runner.expectTrue(result.glyphs == nullptr, "freeStreamingResult_cleans_up: glyphs nullptr after free");
    runner.expectTrue(result.intervals == nullptr, "freeStreamingResult_cleans_up: intervals nullptr after free");
    runner.expectFalse(result.success, "freeStreamingResult_cleans_up: success is false after free");
  }

  // Test 7: freeStreamingResult_idempotent
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");
    runner.expectTrue(result.success, "freeStreamingResult_idempotent: load succeeded");

    EpdFontLoader::freeStreamingResult(result);
    // Double-free should be safe
    EpdFontLoader::freeStreamingResult(result);

    runner.expectTrue(result.glyphs == nullptr, "freeStreamingResult_idempotent: still nullptr after double-free");
    runner.expectTrue(result.intervals == nullptr, "freeStreamingResult_idempotent: intervals still nullptr");
  }

  // ============================================
  // Edge Cases
  // ============================================

  // Test 8: loadForStreaming_invalid_magic
  {
    SdMan.clearFiles();
    std::string badData = "NOTAFONT" + std::string(100, '\0');
    SdMan.registerFile("/fonts/bad.epdfont", badData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/bad.epdfont");

    runner.expectFalse(result.success, "loadForStreaming_invalid_magic: rejects invalid magic");
  }

  // Test 9: loadForStreaming_truncated_file
  {
    SdMan.clearFiles();
    // Only write the magic and part of version
    std::string truncated;
    truncated.push_back(0x45);  // E
    truncated.push_back(0x50);  // P
    truncated.push_back(0x44);  // D
    truncated.push_back(0x46);  // F
    truncated.push_back(0x01);  // Version low byte
    // Missing: rest of header
    SdMan.registerFile("/fonts/truncated.epdfont", truncated);

    auto result = EpdFontLoader::loadForStreaming("/fonts/truncated.epdfont");

    runner.expectFalse(result.success, "loadForStreaming_truncated_file: rejects truncated file");
  }

  // Test 10: loadForStreaming_single_glyph
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateSingleGlyphFont('X', 10, 14);
    SdMan.registerFile("/fonts/single.epdfont", fontData);

    auto result = EpdFontLoader::loadForStreaming("/fonts/single.epdfont");

    runner.expectTrue(result.success, "loadForStreaming_single_glyph: load succeeded");
    runner.expectEq(static_cast<uint32_t>(1), result.glyphCount,
                    "loadForStreaming_single_glyph: exactly 1 glyph");
    runner.expectEq(static_cast<uint32_t>(1), result.fontData.intervalCount,
                    "loadForStreaming_single_glyph: exactly 1 interval");

    if (result.glyphs) {
      runner.expectEq(static_cast<uint8_t>(10), result.glyphs[0].width,
                      "loadForStreaming_single_glyph: glyph width correct");
      runner.expectEq(static_cast<uint8_t>(14), result.glyphs[0].height,
                      "loadForStreaming_single_glyph: glyph height correct");
    }

    EpdFontLoader::freeStreamingResult(result);
  }

  // ============================================
  // Retry Logic Tests
  // ============================================

  // Test 11: loadForStreaming retries on transient open failure
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);
    SdMan.setOpenFailCount(2);  // First 2 opens fail, 3rd succeeds

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");

    runner.expectTrue(result.success, "loadForStreaming_retry_open: succeeds after transient failures");
    runner.expectTrue(result.glyphs != nullptr, "loadForStreaming_retry_open: glyphs allocated");

    EpdFontLoader::freeStreamingResult(result);
  }

  // Test 12: loadForStreaming fails after all retries exhausted
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);
    SdMan.setOpenFailCount(3);  // All 3 attempts fail

    auto result = EpdFontLoader::loadForStreaming("/fonts/test.epdfont");

    runner.expectFalse(result.success, "loadForStreaming_retry_exhausted: fails after 3 attempts");
    runner.expectTrue(result.glyphs == nullptr, "loadForStreaming_retry_exhausted: no leak");

    SdMan.setOpenFailCount(0);  // Reset
  }

  // Test 13: loadFromFile retries on transient open failure
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);
    SdMan.setOpenFailCount(1);  // First open fails, 2nd succeeds

    auto result = EpdFontLoader::loadFromFile("/fonts/test.epdfont");

    runner.expectTrue(result.success, "loadFromFile_retry_open: succeeds after 1 transient failure");
    runner.expectTrue(result.bitmap != nullptr, "loadFromFile_retry_open: bitmap allocated");
    runner.expectTrue(result.glyphs != nullptr, "loadFromFile_retry_open: glyphs allocated");

    EpdFontLoader::freeLoadResult(result);
  }

  // Test 14: loadFromFile fails after all retries exhausted
  {
    SdMan.clearFiles();
    std::string fontData = TestFontData::generateBasicAsciiFont(20);
    SdMan.registerFile("/fonts/test.epdfont", fontData);
    SdMan.setOpenFailCount(3);

    auto result = EpdFontLoader::loadFromFile("/fonts/test.epdfont");

    runner.expectFalse(result.success, "loadFromFile_retry_exhausted: fails after 3 attempts");
    runner.expectTrue(result.bitmap == nullptr, "loadFromFile_retry_exhausted: no leak");

    SdMan.setOpenFailCount(0);
  }

  // Test 15: loadForStreaming invalid magic does NOT retry (non-transient)
  {
    SdMan.clearFiles();
    std::string badData = "NOTAFONT" + std::string(100, '\0');
    SdMan.registerFile("/fonts/bad.epdfont", badData);
    SdMan.setOpenFailCount(0);

    auto result = EpdFontLoader::loadForStreaming("/fonts/bad.epdfont");

    runner.expectFalse(result.success, "loadForStreaming_no_retry_bad_magic: fails immediately");
  }

  return runner.allPassed() ? 0 : 1;
}
