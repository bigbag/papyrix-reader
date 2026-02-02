#include "test_utils.h"

#include <cstdint>

// Inline the BitmapHelpers functions directly for testing (avoiding linker issues with ESP32 dependencies)

// Precomputed RGB to grayscale lookup tables (BT.601 coefficients)
// gray = LUT_R[r] + LUT_G[g] + LUT_B[b] instead of (77*r + 150*g + 29*b) >> 8
// Note: Max sum is 76+149+28=253 (not 255) due to integer truncation.
// clang-format off
static const uint8_t LUT_R[256] = {
    0,  0,  0,  0,  1,  1,  1,  2,  2,  2,  3,  3,  3,  3,  4,  4,
    4,  5,  5,  5,  6,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,
    9,  9, 10, 10, 10, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 14,
   14, 14, 15, 15, 15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18,
   19, 19, 19, 20, 20, 20, 21, 21, 21, 21, 22, 22, 22, 23, 23, 23,
   24, 24, 24, 24, 25, 25, 25, 26, 26, 26, 27, 27, 27, 27, 28, 28,
   28, 29, 29, 29, 30, 30, 30, 30, 31, 31, 31, 32, 32, 32, 33, 33,
   33, 33, 34, 34, 34, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 38,
   38, 38, 39, 39, 39, 39, 40, 40, 40, 41, 41, 41, 42, 42, 42, 42,
   43, 43, 43, 44, 44, 44, 45, 45, 45, 45, 46, 46, 46, 47, 47, 47,
   48, 48, 48, 48, 49, 49, 49, 50, 50, 50, 51, 51, 51, 51, 52, 52,
   52, 53, 53, 53, 54, 54, 54, 54, 55, 55, 55, 56, 56, 56, 57, 57,
   57, 57, 58, 58, 58, 59, 59, 59, 60, 60, 60, 60, 61, 61, 61, 62,
   62, 62, 63, 63, 63, 63, 64, 64, 64, 65, 65, 65, 66, 66, 66, 66,
   67, 67, 67, 68, 68, 68, 69, 69, 69, 69, 70, 70, 70, 71, 71, 71,
   72, 72, 72, 72, 73, 73, 73, 74, 74, 74, 75, 75, 75, 75, 76, 76
};
static const uint8_t LUT_G[256] = {
    0,  0,  1,  1,  2,  2,  3,  4,  4,  5,  5,  6,  7,  7,  8,  8,
    9, 10, 10, 11, 11, 12, 12, 13, 14, 14, 15, 15, 16, 17, 17, 18,
   18, 19, 19, 20, 21, 21, 22, 22, 23, 24, 24, 25, 25, 26, 26, 27,
   28, 28, 29, 29, 30, 31, 31, 32, 32, 33, 33, 34, 35, 35, 36, 36,
   37, 38, 38, 39, 39, 40, 41, 41, 42, 42, 43, 43, 44, 45, 45, 46,
   46, 47, 48, 48, 49, 49, 50, 50, 51, 52, 52, 53, 53, 54, 55, 55,
   56, 56, 57, 57, 58, 59, 59, 60, 60, 61, 62, 62, 63, 63, 64, 64,
   65, 66, 66, 67, 67, 68, 69, 69, 70, 70, 71, 71, 72, 73, 73, 74,
   75, 75, 76, 76, 77, 78, 78, 79, 79, 80, 80, 81, 82, 82, 83, 83,
   84, 85, 85, 86, 86, 87, 87, 88, 89, 89, 90, 90, 91, 92, 92, 93,
   93, 94, 95, 95, 96, 96, 97, 97, 98, 99, 99,100,100,101,102,102,
  103,103,104,104,105,106,106,107,107,108,109,109,110,110,111,111,
  112,113,113,114,114,115,116,116,117,117,118,118,119,120,120,121,
  121,122,123,123,124,124,125,125,126,127,127,128,128,129,130,130,
  131,131,132,132,133,134,134,135,135,136,137,137,138,138,139,139,
  140,141,141,142,142,143,144,144,145,145,146,146,147,148,148,149
};
static const uint8_t LUT_B[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  3,  3,  3,  3,  3,
    3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  5,  5,  5,  5,  6,  6,  6,  6,  6,  6,  6,  6,  6,  7,  7,
    7,  7,  7,  7,  7,  7,  7,  8,  8,  8,  8,  8,  8,  8,  8,  8,
    9,  9,  9,  9,  9,  9,  9,  9, 10, 10, 10, 10, 10, 10, 10, 10,
   10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12,
   12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14,
   14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16,
   16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 18, 18,
   18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19,
   20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21,
   21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23,
   23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25,
   25, 25, 25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 27, 27, 27,
   27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28
};
// clang-format on

static uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) { return LUT_R[r] + LUT_G[g] + LUT_B[b]; }

// Brightness/Contrast adjustments for e-ink display optimization:
constexpr int BRIGHTNESS_BOOST = 0;
constexpr float CONTRAST_FACTOR = 1.35f;
constexpr bool USE_GAMMA_CORRECTION = false;

// Apply contrast adjustment around midpoint (128)
static inline int applyContrast(int gray) {
  constexpr int factorNum = static_cast<int>(CONTRAST_FACTOR * 100);
  int adjusted = ((gray - 128) * factorNum) / 100 + 128;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return adjusted;
}

static int adjustPixel(int gray) {
  gray = applyContrast(gray);
  gray += BRIGHTNESS_BOOST;
  if (gray > 255) gray = 255;
  if (gray < 0) gray = 0;
  if (USE_GAMMA_CORRECTION) {
    // Gamma correction disabled
  }
  return gray;
}

static uint8_t quantizeSimple(int gray) {
  if (gray < 45) {
    return 0;
  } else if (gray < 70) {
    return 1;
  } else if (gray < 140) {
    return 2;
  } else {
    return 3;
  }
}

static uint8_t quantize1bit(int gray, int x, int y) {
  (void)x;
  (void)y;
  return gray < 128 ? 0 : 1;
}

// Convert 2-bit palette index to grayscale (0-255)
static inline uint8_t palette2bitToGray(uint8_t index) {
  static const uint8_t lut[4] = {0, 85, 170, 255};
  return lut[index & 0x03];
}

// Convert 1-bit palette index to grayscale (0-255)
static inline uint8_t palette1bitToGray(uint8_t index) { return (index & 0x01) ? 255 : 0; }

// Helper to extract 2-bit pixel from packed byte (4 pixels per byte, MSB first)
static inline uint8_t extract2bitPixel(uint8_t byte, int pixelIndex) {
  const int bitShift = 6 - (pixelIndex % 4) * 2;
  return (byte >> bitShift) & 0x03;
}

// Helper to extract 1-bit pixel from packed byte (8 pixels per byte, MSB first)
static inline uint8_t extract1bitPixel(uint8_t byte, int pixelIndex) {
  const int bitOffset = 7 - (pixelIndex % 8);
  return (byte >> bitOffset) & 0x01;
}

int main() {
  TestUtils::TestRunner runner("BitmapHelpers");

  // ============================================
  // rgbToGray() tests - BT.601 LUT-based conversion
  // Formula: gray = (77*r + 150*g + 29*b) >> 8
  // ============================================

  // Test 1: Pure black (0,0,0) -> 0
  {
    uint8_t gray = rgbToGray(0, 0, 0);
    runner.expectEq(static_cast<uint8_t>(0), gray, "rgbToGray: black (0,0,0) -> 0");
  }

  // Test 2: Pure white (255,255,255) -> 253 (documented truncation)
  // Max sum is 76+149+28=253 due to integer truncation of BT.601 coefficients
  {
    uint8_t gray = rgbToGray(255, 255, 255);
    runner.expectEq(static_cast<uint8_t>(253), gray, "rgbToGray: white (255,255,255) -> 253 (truncation)");
  }

  // Test 3: Pure red (255,0,0) -> LUT_R[255] = 76
  {
    uint8_t gray = rgbToGray(255, 0, 0);
    runner.expectEq(static_cast<uint8_t>(76), gray, "rgbToGray: red (255,0,0) -> 76");
  }

  // Test 4: Pure green (0,255,0) -> LUT_G[255] = 149
  {
    uint8_t gray = rgbToGray(0, 255, 0);
    runner.expectEq(static_cast<uint8_t>(149), gray, "rgbToGray: green (0,255,0) -> 149");
  }

  // Test 5: Pure blue (0,0,255) -> LUT_B[255] = 28
  {
    uint8_t gray = rgbToGray(0, 0, 255);
    runner.expectEq(static_cast<uint8_t>(28), gray, "rgbToGray: blue (0,0,255) -> 28");
  }

  // Test 6: Gray (128,128,128) - verify against actual LUT values
  // LUT_R[128] = 38, LUT_G[128] = 75, LUT_B[128] = 14 => 38+75+14 = 127 (but actually 128 due to rounding)
  // (77*128)>>8 = 38, (150*128)>>8 = 75, (29*128)>>8 = 14 => 38+75+14 = 127... wait let me verify
  // Actually: 38 + 75 + 14 = 127. The true BT.601 value for gray 128 is ~128, but due to integer math it's 127.
  {
    uint8_t gray = rgbToGray(128, 128, 128);
    runner.expectEq(static_cast<uint8_t>(127), gray, "rgbToGray: gray (128,128,128) -> 127");
  }

  // Test 7: Red component - verify LUT value
  // (77*100) >> 8 = 7700 >> 8 = 30
  {
    uint8_t gray = rgbToGray(100, 0, 0);
    runner.expectEq(static_cast<uint8_t>(30), gray, "rgbToGray: red component (100,0,0) -> 30");
  }

  // Test 8: Green component - (150*100) >> 8 = 58
  {
    uint8_t gray = rgbToGray(0, 100, 0);
    runner.expectEq(static_cast<uint8_t>(58), gray, "rgbToGray: green component (0,100,0) -> 58");
  }

  // Test 9: Blue component - LUT_B[100] = 11
  {
    uint8_t gray = rgbToGray(0, 0, 100);
    runner.expectEq(static_cast<uint8_t>(11), gray, "rgbToGray: blue component (0,0,100) -> 11");
  }

  // Test 10: Combined color - verify components add correctly
  {
    uint8_t r_only = rgbToGray(50, 0, 0);
    uint8_t g_only = rgbToGray(0, 100, 0);
    uint8_t b_only = rgbToGray(0, 0, 150);
    uint8_t combined = rgbToGray(50, 100, 150);
    runner.expectEq(static_cast<uint8_t>(r_only + g_only + b_only), combined,
                    "rgbToGray: components are additive (50,100,150)");
  }

  // ============================================
  // quantizeSimple() tests - 4-level quantization
  // Thresholds: <45 -> 0, <70 -> 1, <140 -> 2, else -> 3
  // ============================================

  // Test 11: Below first threshold
  {
    runner.expectEq(static_cast<uint8_t>(0), quantizeSimple(0), "quantizeSimple: 0 -> level 0");
    runner.expectEq(static_cast<uint8_t>(0), quantizeSimple(44), "quantizeSimple: 44 -> level 0");
  }

  // Test 12: First threshold boundary
  {
    runner.expectEq(static_cast<uint8_t>(1), quantizeSimple(45), "quantizeSimple: 45 -> level 1");
    runner.expectEq(static_cast<uint8_t>(1), quantizeSimple(69), "quantizeSimple: 69 -> level 1");
  }

  // Test 13: Second threshold boundary
  {
    runner.expectEq(static_cast<uint8_t>(2), quantizeSimple(70), "quantizeSimple: 70 -> level 2");
    runner.expectEq(static_cast<uint8_t>(2), quantizeSimple(139), "quantizeSimple: 139 -> level 2");
  }

  // Test 14: Above last threshold
  {
    runner.expectEq(static_cast<uint8_t>(3), quantizeSimple(140), "quantizeSimple: 140 -> level 3");
    runner.expectEq(static_cast<uint8_t>(3), quantizeSimple(255), "quantizeSimple: 255 -> level 3");
  }

  // Test 15: Mid-range values
  {
    runner.expectEq(static_cast<uint8_t>(0), quantizeSimple(22), "quantizeSimple: 22 -> level 0");
    runner.expectEq(static_cast<uint8_t>(1), quantizeSimple(57), "quantizeSimple: 57 -> level 1");
    runner.expectEq(static_cast<uint8_t>(2), quantizeSimple(100), "quantizeSimple: 100 -> level 2");
    runner.expectEq(static_cast<uint8_t>(3), quantizeSimple(200), "quantizeSimple: 200 -> level 3");
  }

  // ============================================
  // adjustPixel() tests - contrast adjustment
  // With CONTRAST_FACTOR=1.35 and BRIGHTNESS_BOOST=0
  // ============================================

  // Test 16: Midpoint (128) stays at 128 (contrast centered at 128)
  {
    int adjusted = adjustPixel(128);
    runner.expectEq(128, adjusted, "adjustPixel: midpoint 128 stays at 128");
  }

  // Test 17: Black (0) with contrast expansion
  // (0 - 128) * 1.35 + 128 = -172.8 + 128 = -44.8 -> clamped to 0
  {
    int adjusted = adjustPixel(0);
    runner.expectEq(0, adjusted, "adjustPixel: black 0 -> 0 (clamped)");
  }

  // Test 18: White (255) with contrast expansion
  // (255 - 128) * 1.35 + 128 = 171.45 + 128 = 299.45 -> clamped to 255
  {
    int adjusted = adjustPixel(255);
    runner.expectEq(255, adjusted, "adjustPixel: white 255 -> 255 (clamped)");
  }

  // Test 19: Dark gray (64) - contrast stretches towards black
  // (64 - 128) * 1.35 + 128 = -86.4 + 128 = 41.6 -> ~41 (integer truncation)
  {
    int adjusted = adjustPixel(64);
    runner.expectTrue(adjusted < 64, "adjustPixel: dark gray 64 becomes darker");
    runner.expectTrue(adjusted >= 0, "adjustPixel: dark gray 64 stays non-negative");
  }

  // Test 20: Light gray (192) - contrast stretches towards white
  // (192 - 128) * 1.35 + 128 = 86.4 + 128 = 214.4 -> ~214 (integer truncation)
  {
    int adjusted = adjustPixel(192);
    runner.expectTrue(adjusted > 192, "adjustPixel: light gray 192 becomes lighter");
    runner.expectTrue(adjusted <= 255, "adjustPixel: light gray 192 stays <= 255");
  }

  // ============================================
  // quantize1bit() tests - simple 1-bit threshold
  // ============================================

  // Test 21: Below threshold
  {
    runner.expectEq(static_cast<uint8_t>(0), quantize1bit(0, 0, 0), "quantize1bit: 0 -> black");
    runner.expectEq(static_cast<uint8_t>(0), quantize1bit(127, 0, 0), "quantize1bit: 127 -> black");
  }

  // Test 22: At and above threshold
  {
    runner.expectEq(static_cast<uint8_t>(1), quantize1bit(128, 0, 0), "quantize1bit: 128 -> white");
    runner.expectEq(static_cast<uint8_t>(1), quantize1bit(255, 0, 0), "quantize1bit: 255 -> white");
  }

  // Test 23: Position parameters don't affect simple 1-bit quantization
  {
    runner.expectEq(static_cast<uint8_t>(0), quantize1bit(100, 50, 50),
                    "quantize1bit: position doesn't affect result (dark)");
    runner.expectEq(static_cast<uint8_t>(1), quantize1bit(200, 100, 100),
                    "quantize1bit: position doesn't affect result (light)");
  }

  // ============================================
  // palette2bitToGray() tests - 2-bit palette conversion
  // Palette: 0=black(0), 1=dark gray(85), 2=light gray(170), 3=white(255)
  // ============================================

  // Test 24: All 4 palette entries
  {
    runner.expectEq(static_cast<uint8_t>(0), palette2bitToGray(0), "palette2bitToGray: 0 -> black (0)");
    runner.expectEq(static_cast<uint8_t>(85), palette2bitToGray(1), "palette2bitToGray: 1 -> dark gray (85)");
    runner.expectEq(static_cast<uint8_t>(170), palette2bitToGray(2), "palette2bitToGray: 2 -> light gray (170)");
    runner.expectEq(static_cast<uint8_t>(255), palette2bitToGray(3), "palette2bitToGray: 3 -> white (255)");
  }

  // Test 25: Masking - only lower 2 bits used
  {
    runner.expectEq(static_cast<uint8_t>(0), palette2bitToGray(0x04), "palette2bitToGray: 0x04 masked to 0");
    runner.expectEq(static_cast<uint8_t>(85), palette2bitToGray(0x05), "palette2bitToGray: 0x05 masked to 1");
    runner.expectEq(static_cast<uint8_t>(170), palette2bitToGray(0xFE), "palette2bitToGray: 0xFE masked to 2");
    runner.expectEq(static_cast<uint8_t>(255), palette2bitToGray(0xFF), "palette2bitToGray: 0xFF masked to 3");
  }

  // ============================================
  // palette1bitToGray() tests - 1-bit palette conversion
  // Palette: 0=black(0), 1=white(255)
  // ============================================

  // Test 26: Both palette entries
  {
    runner.expectEq(static_cast<uint8_t>(0), palette1bitToGray(0), "palette1bitToGray: 0 -> black (0)");
    runner.expectEq(static_cast<uint8_t>(255), palette1bitToGray(1), "palette1bitToGray: 1 -> white (255)");
  }

  // Test 27: Masking - only lower 1 bit used
  {
    runner.expectEq(static_cast<uint8_t>(0), palette1bitToGray(0x02), "palette1bitToGray: 0x02 masked to 0");
    runner.expectEq(static_cast<uint8_t>(255), palette1bitToGray(0x03), "palette1bitToGray: 0x03 masked to 1");
    runner.expectEq(static_cast<uint8_t>(0), palette1bitToGray(0xFE), "palette1bitToGray: 0xFE masked to 0");
    runner.expectEq(static_cast<uint8_t>(255), palette1bitToGray(0xFF), "palette1bitToGray: 0xFF masked to 1");
  }

  // ============================================
  // extract2bitPixel() tests - 2-bit BMP pixel extraction
  // 4 pixels per byte, MSB first
  // ============================================

  // Test 28: Extract each position from a byte
  {
    // Byte 0b11_10_01_00 = 0xE4 contains pixels: 3, 2, 1, 0
    const uint8_t byte = 0xE4;
    runner.expectEq(static_cast<uint8_t>(3), extract2bitPixel(byte, 0), "extract2bitPixel: pixel 0 (bits 7-6)");
    runner.expectEq(static_cast<uint8_t>(2), extract2bitPixel(byte, 1), "extract2bitPixel: pixel 1 (bits 5-4)");
    runner.expectEq(static_cast<uint8_t>(1), extract2bitPixel(byte, 2), "extract2bitPixel: pixel 2 (bits 3-2)");
    runner.expectEq(static_cast<uint8_t>(0), extract2bitPixel(byte, 3), "extract2bitPixel: pixel 3 (bits 1-0)");
  }

  // Test 29: All zeros and all ones
  {
    runner.expectEq(static_cast<uint8_t>(0), extract2bitPixel(0x00, 0), "extract2bitPixel: 0x00 pixel 0 -> 0");
    runner.expectEq(static_cast<uint8_t>(0), extract2bitPixel(0x00, 3), "extract2bitPixel: 0x00 pixel 3 -> 0");
    runner.expectEq(static_cast<uint8_t>(3), extract2bitPixel(0xFF, 0), "extract2bitPixel: 0xFF pixel 0 -> 3");
    runner.expectEq(static_cast<uint8_t>(3), extract2bitPixel(0xFF, 3), "extract2bitPixel: 0xFF pixel 3 -> 3");
  }

  // ============================================
  // extract1bitPixel() tests - 1-bit BMP pixel extraction
  // 8 pixels per byte, MSB first
  // ============================================

  // Test 30: Extract each position from a byte
  {
    // Byte 0b10101010 = 0xAA contains alternating 1,0,1,0,1,0,1,0
    const uint8_t byte = 0xAA;
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(byte, 0), "extract1bitPixel: pixel 0 (bit 7)");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(byte, 1), "extract1bitPixel: pixel 1 (bit 6)");
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(byte, 2), "extract1bitPixel: pixel 2 (bit 5)");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(byte, 3), "extract1bitPixel: pixel 3 (bit 4)");
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(byte, 4), "extract1bitPixel: pixel 4 (bit 3)");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(byte, 5), "extract1bitPixel: pixel 5 (bit 2)");
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(byte, 6), "extract1bitPixel: pixel 6 (bit 1)");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(byte, 7), "extract1bitPixel: pixel 7 (bit 0)");
  }

  // Test 31: All zeros and all ones
  {
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(0x00, 0), "extract1bitPixel: 0x00 pixel 0 -> 0");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(0x00, 7), "extract1bitPixel: 0x00 pixel 7 -> 0");
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(0xFF, 0), "extract1bitPixel: 0xFF pixel 0 -> 1");
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(0xFF, 7), "extract1bitPixel: 0xFF pixel 7 -> 1");
  }

  // Test 32: Single bit set at each position
  {
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(0x80, 0), "extract1bitPixel: 0x80 pixel 0 -> 1");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(0x80, 1), "extract1bitPixel: 0x80 pixel 1 -> 0");
    runner.expectEq(static_cast<uint8_t>(1), extract1bitPixel(0x01, 7), "extract1bitPixel: 0x01 pixel 7 -> 1");
    runner.expectEq(static_cast<uint8_t>(0), extract1bitPixel(0x01, 6), "extract1bitPixel: 0x01 pixel 6 -> 0");
  }

  return runner.allPassed() ? 0 : 1;
}
