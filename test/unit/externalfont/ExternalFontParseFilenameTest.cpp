#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>

// Inline the parseFilename() logic from ExternalFont.cpp:35-121
// Tests filename parsing without SD card or firmware dependencies.

static constexpr uint8_t MAX_CHAR_DIM = 64;
static constexpr int MAX_GLYPH_BYTES = 256;

struct ParsedFont {
  char fontName[32] = {0};
  uint8_t fontSize = 0;
  uint8_t charWidth = 0;
  uint8_t charHeight = 0;
  uint8_t bytesPerRow = 0;
  uint16_t bytesPerChar = 0;
};

static bool parseFilename(const char* filepath, ParsedFont& out) {
  out = {};

  const char* filename = strrchr(filepath, '/');
  if (filename) {
    filename++;
  } else {
    filename = filepath;
  }

  char nameCopy[64];
  strncpy(nameCopy, filename, sizeof(nameCopy) - 1);
  nameCopy[sizeof(nameCopy) - 1] = '\0';

  char* ext = strstr(nameCopy, ".bin");
  if (!ext) {
    return false;
  }
  *ext = '\0';

  char* lastUnderscore = strrchr(nameCopy, '_');
  if (!lastUnderscore) {
    return false;
  }

  int w, h;
  if (sscanf(lastUnderscore + 1, "%dx%d", &w, &h) != 2) {
    return false;
  }
  out.charWidth = (uint8_t)w;
  out.charHeight = (uint8_t)h;

  if (out.charWidth > MAX_CHAR_DIM || out.charHeight > MAX_CHAR_DIM) {
    return false;
  }

  *lastUnderscore = '\0';

  lastUnderscore = strrchr(nameCopy, '_');
  if (!lastUnderscore) {
    return false;
  }

  int size;
  const char* sizeStr = lastUnderscore + 1;
  if (strncmp(sizeStr, "px", 2) == 0) {
    if (sscanf(sizeStr + 2, "%d", &size) != 1) {
      return false;
    }
  } else if (sscanf(sizeStr, "%d", &size) != 1) {
    return false;
  }
  out.fontSize = (uint8_t)size;
  *lastUnderscore = '\0';

  strncpy(out.fontName, nameCopy, sizeof(out.fontName) - 1);
  out.fontName[sizeof(out.fontName) - 1] = '\0';

  out.bytesPerRow = (out.charWidth + 7) / 8;
  out.bytesPerChar = out.bytesPerRow * out.charHeight;

  if (out.bytesPerChar > MAX_GLYPH_BYTES) {
    return false;
  }

  return true;
}

int main() {
  TestUtils::TestRunner runner("ExternalFontParseFilename");

  // 1. Standard format
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("KingHwaOldSong_38_33x39.bin", f), "standard: parses OK");
    runner.expectEqual("KingHwaOldSong", f.fontName, "standard: name");
    runner.expectEq((uint8_t)38, f.fontSize, "standard: size");
    runner.expectEq((uint8_t)33, f.charWidth, "standard: width");
    runner.expectEq((uint8_t)39, f.charHeight, "standard: height");
  }

  // 2. Standard format with path prefix
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("/fonts/KingHwaOldSong_38_33x39.bin", f), "with path: parses OK");
    runner.expectEqual("KingHwaOldSong", f.fontName, "with path: name");
    runner.expectEq((uint8_t)38, f.fontSize, "with path: size");
  }

  // 3. New px notation
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("noto-sans-jp_px30_35x45.bin", f), "px notation: parses OK");
    runner.expectEqual("noto-sans-jp", f.fontName, "px notation: name with hyphens");
    runner.expectEq((uint8_t)30, f.fontSize, "px notation: size=30");
    runner.expectEq((uint8_t)35, f.charWidth, "px notation: width");
    runner.expectEq((uint8_t)45, f.charHeight, "px notation: height");
  }

  // 4. px notation with path
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("/fonts/noto-sans-sc_px34_34x45.bin", f), "px with path: parses OK");
    runner.expectEqual("noto-sans-sc", f.fontName, "px with path: name");
    runner.expectEq((uint8_t)34, f.fontSize, "px with path: size=34");
  }

  // 5. Missing .bin extension
  {
    ParsedFont f;
    runner.expectFalse(parseFilename("Font_38_33x39.ttf", f), "no .bin: fails");
  }

  // 6. Missing dimensions (only name_size.bin)
  {
    ParsedFont f;
    runner.expectFalse(parseFilename("Font_38.bin", f), "no dimensions: fails");
  }

  // 7. Dimensions too large
  {
    ParsedFont f;
    runner.expectFalse(parseFilename("Font_38_65x65.bin", f), "dims too large: fails (65 > MAX_CHAR_DIM=64)");
  }

  // 8. Glyph too large (bytesPerChar > MAX_GLYPH_BYTES)
  //    48x48: bytesPerRow=(48+7)/8=6, bytesPerChar=6*48=288 > 256
  {
    ParsedFont f;
    runner.expectFalse(parseFilename("Font_38_48x48.bin", f), "glyph too large: 48x48 = 288 bytes > 256");
  }

  // 9. Valid large glyph (32x32: bytesPerRow=4, bytesPerChar=128 <= 256)
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("Font_38_32x32.bin", f), "valid large: 32x32 = 128 bytes");
    runner.expectEq((uint8_t)4, f.bytesPerRow, "valid large: bytesPerRow = 4");
    runner.expectEq((uint16_t)128, f.bytesPerChar, "valid large: bytesPerChar = 128");
  }

  // 10. Invalid px notation (non-numeric)
  {
    ParsedFont f;
    runner.expectFalse(parseFilename("Font_pxABC_33x39.bin", f), "invalid px: non-numeric fails");
  }

  // 11. Font name with multiple underscores
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("Source_Han_Sans_CN_20_20x20.bin", f), "multi-underscore: parses OK");
    runner.expectEqual("Source_Han_Sans_CN", f.fontName, "multi-underscore: full name preserved");
    runner.expectEq((uint8_t)20, f.fontSize, "multi-underscore: size=20");
    runner.expectEq((uint8_t)20, f.charWidth, "multi-underscore: width=20");
    runner.expectEq((uint8_t)20, f.charHeight, "multi-underscore: height=20");
  }

  // 12. bytesPerRow/bytesPerChar calculation
  //    33x39: bytesPerRow=(33+7)/8=5, bytesPerChar=5*39=195
  {
    ParsedFont f;
    parseFilename("Test_14_33x39.bin", f);
    runner.expectEq((uint8_t)5, f.bytesPerRow, "bytes calc: bytesPerRow=(33+7)/8=5");
    runner.expectEq((uint16_t)195, f.bytesPerChar, "bytes calc: bytesPerChar=5*39=195");
  }

  // 13. Single-char font name
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("X_14_10x12.bin", f), "single-char name: parses OK");
    runner.expectEqual("X", f.fontName, "single-char name: name=X");
    runner.expectEq((uint8_t)14, f.fontSize, "single-char name: size=14");
  }

  // 14. Width at MAX_CHAR_DIM boundary (64x1: bytesPerRow=8, bytesPerChar=8)
  {
    ParsedFont f;
    runner.expectTrue(parseFilename("Boundary_20_64x1.bin", f), "max dim boundary: 64x1 OK");
    runner.expectEq((uint8_t)64, f.charWidth, "max dim boundary: width=64");
    runner.expectEq((uint8_t)1, f.charHeight, "max dim boundary: height=1");
    runner.expectEq((uint8_t)8, f.bytesPerRow, "max dim boundary: bytesPerRow=8");
    runner.expectEq((uint16_t)8, f.bytesPerChar, "max dim boundary: bytesPerChar=8");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
