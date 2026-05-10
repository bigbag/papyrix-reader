#include "test_utils.h"

#include <cstdint>
#include <cstring>

// Extract the identity palette detection logic from Bitmap::parseHeaders()
// and the fast memcpy path from Bitmap::readRow() for isolated testing.

static bool detectIdentityPalette(uint16_t bpp, uint32_t colorsUsed, const uint8_t* paletteLum) {
  return (bpp == 2 && colorsUsed >= 4 && paletteLum[0] == 0x00 && paletteLum[1] == 0x55 &&
          paletteLum[2] == 0xAA && paletteLum[3] == 0xFF);
}

// Mirrors paletteLum computation from Bitmap::parseHeaders()
static void computePaletteLum(const uint8_t* palBuf, uint32_t colorsUsed, uint8_t* paletteLum) {
  for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
  for (uint32_t i = 0; i < colorsUsed; i++) {
    const uint8_t* rgb = palBuf + i * 4;
    paletteLum[i] = (77u * rgb[2] + 150u * rgb[1] + 29u * rgb[0]) >> 8;
  }
}

// Mirrors the 2bpp readRow slow path (palette lookup + pack)
static void readRow2bpp_slow(const uint8_t* rowBuffer, uint8_t* data, int width, const uint8_t* paletteLum) {
  int outByte = 0;
  int bitPos = 6;
  for (int x = 0; x < width; x++) {
    uint8_t lum = paletteLum[(rowBuffer[x >> 2] >> (6 - ((x & 3) << 1))) & 0x03];
    uint8_t val;
    if (lum < 45)
      val = 0;
    else if (lum < 70)
      val = 1;
    else if (lum < 140)
      val = 2;
    else
      val = 3;
    data[outByte] |= (val << bitPos);
    bitPos -= 2;
    if (bitPos < 0) {
      outByte++;
      bitPos = 6;
    }
  }
}

// Mirrors the 2bpp identity palette fast path (memcpy)
static void readRow2bpp_fast(const uint8_t* rowBuffer, uint8_t* data, int width) {
  const int bytesIn = (width * 2 + 7) / 8;
  memcpy(data, rowBuffer, bytesIn);
}

int main() {
  TestUtils::TestRunner runner("BitmapIdentityPalette");

  // ============================================
  // Identity palette detection tests
  // ============================================

  // Test 1: Standard 2bpp identity palette (black, dark gray, light gray, white)
  // BGRA: {0,0,0,0}, {85,85,85,0}, {170,170,170,0}, {255,255,255,0}
  {
    uint8_t palBuf[16] = {0, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};
    uint8_t paletteLum[256];
    computePaletteLum(palBuf, 4, paletteLum);
    runner.expectTrue(detectIdentityPalette(2, 4, paletteLum), "standard identity palette detected");
  }

  // Test 2: Non-identity palette (inverted)
  {
    uint8_t palBuf[16] = {255, 255, 255, 0, 170, 170, 170, 0, 85, 85, 85, 0, 0, 0, 0, 0};
    uint8_t paletteLum[256];
    computePaletteLum(palBuf, 4, paletteLum);
    runner.expectFalse(detectIdentityPalette(2, 4, paletteLum), "inverted palette not identity");
  }

  // Test 3: Wrong bpp (8-bit with same lum values)
  {
    uint8_t paletteLum[256];
    for (int i = 0; i < 256; i++) paletteLum[i] = static_cast<uint8_t>(i);
    paletteLum[0] = 0x00;
    paletteLum[1] = 0x55;
    paletteLum[2] = 0xAA;
    paletteLum[3] = 0xFF;
    runner.expectFalse(detectIdentityPalette(8, 256, paletteLum), "8bpp not identity (wrong bpp)");
  }

  // Test 4: 2bpp but fewer than 4 colors
  {
    uint8_t paletteLum[256] = {0x00, 0x55};
    runner.expectFalse(detectIdentityPalette(2, 2, paletteLum), "2bpp with <4 colors not identity");
  }

  // Test 5: Custom non-grey palette (colored entries map to specific lum values)
  {
    // B=200, G=0, R=0 → lum = (77*0 + 150*0 + 29*200)>>8 = 5800>>8 = 22
    // Not 0x00, so not identity
    uint8_t palBuf[16] = {200, 0, 0, 0, 85, 85, 85, 0, 170, 170, 170, 0, 255, 255, 255, 0};
    uint8_t paletteLum[256];
    computePaletteLum(palBuf, 4, paletteLum);
    runner.expectFalse(detectIdentityPalette(2, 4, paletteLum), "colored palette entry not identity");
  }

  // Test 6: Near-identity but off by one
  {
    uint8_t paletteLum[256] = {};
    paletteLum[0] = 0x00;
    paletteLum[1] = 0x54;  // 84 instead of 85
    paletteLum[2] = 0xAA;
    paletteLum[3] = 0xFF;
    runner.expectFalse(detectIdentityPalette(2, 4, paletteLum), "off-by-one not identity");
  }

  // ============================================
  // Fast path semantics: for identity palette, raw 2-bit indices ARE the
  // final display values (0=black, 1=dark, 2=light, 3=white).
  // The fast path (memcpy) preserves these indices without quantization.
  // ============================================

  // Test 7: Fast path is identity transform - output equals input
  {
    // pixels [0,1,2,3] = 0b00_01_10_11 = 0x1B
    uint8_t rowBuffer[4] = {0x1B, 0, 0, 0};
    uint8_t out[1] = {};
    readRow2bpp_fast(rowBuffer, out, 4);
    runner.expectEq(static_cast<uint8_t>(0x1B), out[0], "fast path: preserves raw 2bpp indices");
  }

  // Test 8: Fast path with 8 pixels (2 bytes)
  {
    uint8_t rowBuffer[4] = {0xE4, 0x1B, 0, 0};
    uint8_t out[2] = {};
    readRow2bpp_fast(rowBuffer, out, 8);
    runner.expectEq(static_cast<uint8_t>(0xE4), out[0], "fast path 8px: byte 0 preserved");
    runner.expectEq(static_cast<uint8_t>(0x1B), out[1], "fast path 8px: byte 1 preserved");
  }

  // Test 9: Fast path copies correct byte count for non-aligned widths
  {
    // 5 pixels → bytesIn = (5*2+7)/8 = 2
    uint8_t rowBuffer[4] = {0xFF, 0xC0, 0xAA, 0xBB};
    uint8_t out[4] = {};
    readRow2bpp_fast(rowBuffer, out, 5);
    runner.expectEq(static_cast<uint8_t>(0xFF), out[0], "fast path 5px: byte 0 copied");
    runner.expectEq(static_cast<uint8_t>(0xC0), out[1], "fast path 5px: byte 1 copied");
    runner.expectEq(static_cast<uint8_t>(0x00), out[2], "fast path 5px: byte 2 untouched");
  }

  // Test 10: All-black (index 0) preserved
  {
    uint8_t rowBuffer[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t out[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    readRow2bpp_fast(rowBuffer, out, 16);
    runner.expectEq(static_cast<uint8_t>(0x00), out[0], "fast path all-black: zeroed");
    runner.expectEq(static_cast<uint8_t>(0x00), out[3], "fast path all-black: last byte zeroed");
  }

  // Test 11: All-white (index 3) preserved
  {
    uint8_t rowBuffer[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t out[4] = {};
    readRow2bpp_fast(rowBuffer, out, 16);
    runner.expectEq(static_cast<uint8_t>(0xFF), out[0], "fast path all-white: byte 0 = 0xFF");
    runner.expectEq(static_cast<uint8_t>(0xFF), out[3], "fast path all-white: byte 3 = 0xFF");
  }

  // ============================================
  // Non-identity palette: slow path remaps through quantization.
  // Fast memcpy would be WRONG for non-identity palettes.
  // ============================================

  // Test 12: Inverted palette - slow path remaps, raw memcpy would not
  {
    uint8_t paletteLum[256] = {};
    paletteLum[0] = 0xFF;  // index 0 → white
    paletteLum[1] = 0xAA;  // index 1 → light gray (170)
    paletteLum[2] = 0x55;  // index 2 → dark gray (85)
    paletteLum[3] = 0x00;  // index 3 → black

    // pixels [0,1,2,3] = 0x1B in row buffer
    uint8_t rowBuffer[4] = {0x1B, 0, 0, 0};
    uint8_t outSlow[1] = {};
    uint8_t outFast[1] = {};

    readRow2bpp_slow(rowBuffer, outSlow, 4, paletteLum);
    readRow2bpp_fast(rowBuffer, outFast, 4);
    // Slow remaps: idx0→lum255→val3, idx1→lum170→val3, idx2→lum85→val2, idx3→lum0→val0
    // = 0b11_11_10_00 = 0xF8
    runner.expectEq(static_cast<uint8_t>(0xF8), outSlow[0], "inverted palette: slow remaps correctly");
    runner.expectTrue(outSlow[0] != outFast[0], "inverted palette: fast != slow (optimization not applicable)");
  }

  return runner.allPassed() ? 0 : 1;
}
