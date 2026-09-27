#include "test_utils.h"

#include "LittleFS.h"
#include "SDCardManager.h"
#include "SdFat.h"
#include "platform_stubs.h"
#include "test_font_data.h"

#include "Utf8.cpp"
#include "EpdFontLoader.cpp"
#include "StreamingEpdFont.cpp"

int main() {
  TestUtils::TestRunner runner("StreamingEpdFontS3");
  std::vector<TestFontData::GlyphSpec> glyphs;
  for (uint32_t i = 0; i < 300; ++i) {
    TestFontData::GlyphSpec glyph;
    glyph.codepoint = 0x1000 + i;
    glyph.width = 40;
    glyph.height = 40;
    glyph.advanceX = 40;
    glyph.top = 40;
    glyph.bitmap.assign(400, static_cast<uint8_t>(i));
    glyphs.push_back(std::move(glyph));
  }
  SdMan.reset();
  SdMan.registerFile("/fonts/large.epdfont", TestFontData::generateFont(glyphs, 42, 40, 0, true));
  testSetHeapPool(MALLOC_CAP_SPIRAM, 256 * 1024, 256 * 1024);
  {
    StreamingEpdFont font;
    runner.expectTrue(font.load("/fonts/large.epdfont"), "large custom font loads");
    runner.expectEq(256, font.getCacheSize(), "PSRAM permits larger active cache");
    bool allSlabPixels = true;
    for (uint32_t i = 0; i < 220; ++i) {
      const EpdGlyph* glyph = font.getGlyph(0x1000 + i);
      const uint8_t* pixels = font.getGlyphBitmap(glyph);
      allSlabPixels &= pixels && pixels[0] == static_cast<uint8_t>(i) &&
                       pixels[399] == static_cast<uint8_t>(i);
    }
    runner.expectTrue(allSlabPixels, "220 glyphs retain correct PSRAM pixels");
    const size_t readsBefore = gMockBufferReadCalls;
    const uint8_t* hot = font.getGlyphBitmap(font.getGlyph(0x1000));
    runner.expectTrue(hot && hot[0] == 0 && gMockBufferReadCalls == readsBefore,
                      "glyph beyond legacy 192 entries stays cached");
    bool allCompactedPixels = true;
    for (uint32_t i = 220; i < 300; ++i) {
      const uint8_t* pixels = font.getGlyphBitmap(font.getGlyph(0x1000 + i));
      allCompactedPixels &= pixels && pixels[0] == static_cast<uint8_t>(i);
    }
    runner.expectTrue(allCompactedPixels, "evicted and compacted glyphs stay correct");
    const uint8_t* reloaded = font.getGlyphBitmap(font.getGlyph(0x1000));
    runner.expectTrue(reloaded && reloaded[399] == 0, "LRU eviction reloads correct pixels");
  }

  testSetPsramAllocationFailure(true);
  {
    StreamingEpdFont font;
    runner.expectTrue(font.load("/fonts/large.epdfont"), "font loads when PSRAM allocation fails");
    runner.expectEq(192, font.getCacheSize(), "PSRAM failure retains legacy capacity");
    bool allFallbackPixels = true;
    for (uint32_t i = 0; i < 220; ++i) {
      const uint8_t* pixels = font.getGlyphBitmap(font.getGlyph(0x1000 + i));
      allFallbackPixels &= pixels && pixels[0] == static_cast<uint8_t>(i);
    }
    runner.expectTrue(allFallbackPixels, "fallback reads correct glyph pixels");
    const size_t readsBefore = gMockBufferReadCalls;
    const uint8_t* reloaded = font.getGlyphBitmap(font.getGlyph(0x1000));
    runner.expectTrue(reloaded && gMockBufferReadCalls > readsBefore, "legacy eviction reads from SD again");
  }
  testResetLargestFreeBlock();
  return runner.allPassed() ? 0 : 1;
}
