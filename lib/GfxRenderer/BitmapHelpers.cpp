#include "BitmapHelpers.h"

#include <cstdint>

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

uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b) { return LUT_R[r] + LUT_G[g] + LUT_B[b]; }

// Brightness/Contrast adjustments for e-ink display optimization:
constexpr int BRIGHTNESS_BOOST = 0;           // Brightness offset (0-50)
constexpr float CONTRAST_FACTOR = 1.35f;      // Contrast multiplier (1.0 = no change, >1 = more contrast)
constexpr bool USE_GAMMA_CORRECTION = false;  // Gamma brightens midtones - disable for more contrast
constexpr bool USE_NOISE_DITHERING = false;   // Hash-based noise dithering

// Integer approximation of gamma correction (brightens midtones)
// Uses a simple curve: out = 255 * sqrt(in/255) ≈ sqrt(in * 255)
// Kept for tuning - enable via USE_GAMMA_CORRECTION
[[maybe_unused]] static inline int applyGamma(int gray) {
  const int product = gray * 255;
  int x = gray;
  if (x > 0) {
    x = (x + product / x) >> 1;
    x = (x + product / x) >> 1;
  }
  return x > 255 ? 255 : x;
}

// Apply contrast adjustment around midpoint (128)
// factor > 1.0 increases contrast, < 1.0 decreases
static inline int applyContrast(int gray) {
  // Integer-based contrast: (gray - 128) * factor + 128
  // Using fixed-point: factor 1.15 ≈ 115/100
  constexpr int factorNum = static_cast<int>(CONTRAST_FACTOR * 100);
  int adjusted = ((gray - 128) * factorNum) / 100 + 128;
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return adjusted;
}
// Combined brightness/contrast/gamma adjustment
// Always applied to optimize images for e-ink display
int adjustPixel(int gray) {
  // Order: contrast first, then brightness, then gamma
  gray = applyContrast(gray);
  gray += BRIGHTNESS_BOOST;
  if (gray > 255) gray = 255;
  if (gray < 0) gray = 0;
  if (USE_GAMMA_CORRECTION) {
    gray = applyGamma(gray);
  }
  return gray;
}
// Simple quantization without dithering - divide into 4 levels
// The thresholds are fine-tuned to the X4 display
uint8_t quantizeSimple(int gray) {
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

// Hash-based noise dithering - survives downsampling without moiré artifacts
// Uses integer hash to generate pseudo-random threshold per pixel
static inline uint8_t quantizeNoise(int gray, int x, int y) {
  uint32_t hash = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  const int threshold = static_cast<int>(hash >> 24);

  const int scaled = gray * 3;
  if (scaled < 255) {
    return (scaled + threshold >= 255) ? 1 : 0;
  } else if (scaled < 510) {
    return ((scaled - 255) + threshold >= 255) ? 2 : 1;
  } else {
    return ((scaled - 510) + threshold >= 255) ? 3 : 2;
  }
}

// Main quantization function - selects between methods based on config
uint8_t quantize(int gray, int x, int y) {
  if (USE_NOISE_DITHERING) {
    return quantizeNoise(gray, x, y);
  } else {
    return quantizeSimple(gray);
  }
}

// Simple 1-bit quantization (black or white)
uint8_t quantize1bit(int gray, int x, int y) { return gray < 128 ? 0 : 1; }

// BMP scaling implementation
#include <HardwareSerial.h>
#include <SdFat.h>

#include "SDCardManager.h"

// Helper functions for BMP I/O
static uint16_t readLE16(FsFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  return static_cast<uint16_t>(c0 & 0xFF) | (static_cast<uint16_t>(c1 & 0xFF) << 8);
}

static uint32_t readLE32(FsFile& f) {
  const int c0 = f.read();
  const int c1 = f.read();
  const int c2 = f.read();
  const int c3 = f.read();
  return static_cast<uint32_t>(c0 & 0xFF) | (static_cast<uint32_t>(c1 & 0xFF) << 8) |
         (static_cast<uint32_t>(c2 & 0xFF) << 16) | (static_cast<uint32_t>(c3 & 0xFF) << 24);
}

static void writeLE16(Print& out, uint16_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
}

static void writeLE32(Print& out, uint32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

static void writeLE32Signed(Print& out, int32_t value) {
  out.write(value & 0xFF);
  out.write((value >> 8) & 0xFF);
  out.write((value >> 16) & 0xFF);
  out.write((value >> 24) & 0xFF);
}

static void writeBmpHeader1bit(Print& out, int width, int height) {
  const int bytesPerRow = (width + 31) / 32 * 4;
  const int imageSize = bytesPerRow * height;
  const uint32_t fileSize = 62 + imageSize;

  // BMP File Header (14 bytes)
  out.write('B');
  out.write('M');
  writeLE32(out, fileSize);
  writeLE32(out, 0);   // Reserved
  writeLE32(out, 62);  // Offset to pixel data (14 + 40 + 8)

  // DIB Header (40 bytes)
  writeLE32(out, 40);
  writeLE32Signed(out, width);
  writeLE32Signed(out, -height);  // Negative = top-down
  writeLE16(out, 1);              // Planes
  writeLE16(out, 1);              // BPP
  writeLE32(out, 0);              // No compression
  writeLE32(out, imageSize);
  writeLE32(out, 2835);  // X pixels/meter
  writeLE32(out, 2835);  // Y pixels/meter
  writeLE32(out, 2);     // Colors used
  writeLE32(out, 2);     // Colors important

  // Palette (8 bytes)
  const uint8_t palette[8] = {
      0x00, 0x00, 0x00, 0x00,  // Black
      0xFF, 0xFF, 0xFF, 0x00   // White
  };
  out.write(palette, 8);
}

// Convert 2-bit palette index to grayscale (0-255)
// Note: The source cover.bmp was already contrast-enhanced during JPEG conversion,
// so we use the actual palette values without additional adjustment.
static inline uint8_t palette2bitToGray(uint8_t index) {
  // 2-bit BMP palette: 0=black(0), 1=dark gray(85), 2=light gray(170), 3=white(255)
  static const uint8_t lut[4] = {0, 85, 170, 255};
  return lut[index & 0x03];
}

static inline uint8_t palette1bitToGray(uint8_t index) { return (index & 0x01) ? 255 : 0; }

// Simple 1-bit Atkinson ditherer without contrast adjustment
// Used when source is already contrast-enhanced (like cover.bmp)
class RawAtkinson1BitDitherer {
 public:
  explicit RawAtkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new int16_t[width + 4]();
    errorRow1 = new int16_t[width + 4]();
    errorRow2 = new int16_t[width + 4]();
  }

  ~RawAtkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  RawAtkinson1BitDitherer(const RawAtkinson1BitDitherer&) = delete;
  RawAtkinson1BitDitherer& operator=(const RawAtkinson1BitDitherer&) = delete;

  uint8_t processPixel(int gray, int x) {
    // NO adjustPixel() call - source is already contrast-enhanced
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    uint8_t quantized;
    int quantizedValue;
    if (adjusted < 128) {
      quantized = 0;
      quantizedValue = 0;
    } else {
      quantized = 1;
      quantizedValue = 255;
    }

    int error = (adjusted - quantizedValue) >> 3;
    errorRow0[x + 3] += error;
    errorRow0[x + 4] += error;
    errorRow1[x + 1] += error;
    errorRow1[x + 2] += error;
    errorRow1[x + 3] += error;
    errorRow2[x + 2] += error;

    return quantized;
  }

  void nextRow() {
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

bool bmpTo1BitBmpScaled(const char* srcPath, const char* dstPath, int targetMaxWidth, int targetMaxHeight) {
  FsFile srcFile;
  if (!SdMan.openFileForRead("BMP", srcPath, srcFile)) {
    Serial.printf("[%lu] [BMP] Failed to open source: %s\n", millis(), srcPath);
    return false;
  }

  // Parse BMP header
  if (readLE16(srcFile) != 0x4D42) {
    Serial.printf("[%lu] [BMP] Not a BMP file\n", millis());
    srcFile.close();
    return false;
  }

  srcFile.seekCur(8);  // Skip file size and reserved
  const uint32_t pixelOffset = readLE32(srcFile);

  const uint32_t dibSize = readLE32(srcFile);
  if (dibSize < 40) {
    Serial.printf("[%lu] [BMP] Unsupported DIB header\n", millis());
    srcFile.close();
    return false;
  }

  const int srcWidth = static_cast<int32_t>(readLE32(srcFile));
  const int32_t rawHeight = static_cast<int32_t>(readLE32(srcFile));

  // Negative height = top-down BMP (rows stored top to bottom)
  // Positive height = bottom-up BMP (rows stored bottom to top)
  // We only support top-down BMPs since that's what our cover.bmp generator produces
  if (rawHeight >= 0) {
    Serial.printf("[%lu] [BMP] Bottom-up BMP not supported, expected top-down\n", millis());
    srcFile.close();
    return false;
  }
  const int srcHeight = -rawHeight;

  srcFile.seekCur(2);  // Skip planes
  const uint16_t bpp = readLE16(srcFile);

  if (bpp != 1 && bpp != 2) {
    Serial.printf("[%lu] [BMP] Expected 1 or 2-bit BMP, got %d-bit\n", millis(), bpp);
    srcFile.close();
    return false;
  }

  Serial.printf("[%lu] [BMP] Scaling %dx%d %d-bit BMP to 1-bit thumbnail\n", millis(), srcWidth, srcHeight, bpp);

  // Calculate output dimensions (scale to fit target while maintaining aspect)
  int outWidth = srcWidth;
  int outHeight = srcHeight;

  if (srcWidth > targetMaxWidth || srcHeight > targetMaxHeight) {
    const float scaleW = static_cast<float>(targetMaxWidth) / srcWidth;
    const float scaleH = static_cast<float>(targetMaxHeight) / srcHeight;
    const float scale = (scaleW < scaleH) ? scaleW : scaleH;
    outWidth = static_cast<int>(srcWidth * scale);
    outHeight = static_cast<int>(srcHeight * scale);
    if (outWidth < 1) outWidth = 1;
    if (outHeight < 1) outHeight = 1;
  }

  // Calculate fixed-point scale factors (16.16 format) for accurate sub-pixel sampling
  // scaleX_fp = (srcWidth << 16) / outWidth = source pixels per output pixel
  const uint32_t scaleX_fp = (static_cast<uint32_t>(srcWidth) << 16) / outWidth;
  const uint32_t scaleY_fp = (static_cast<uint32_t>(srcHeight) << 16) / outHeight;

  // Calculate max source rows needed per output row (ceiling of scaleY)
  const int maxSrcRowsPerOut = ((scaleY_fp + 0xFFFF) >> 16) + 1;

  Serial.printf("[%lu] [BMP] Output: %dx%d, scale_fp: %lu x %lu\n", millis(), outWidth, outHeight,
                static_cast<unsigned long>(scaleX_fp), static_cast<unsigned long>(scaleY_fp));

  // Calculate row sizes
  const int srcRowBytes = (srcWidth * bpp + 31) / 32 * 4;  // bpp-bit source, 4-byte aligned
  const int outRowBytes = (outWidth + 31) / 32 * 4;        // 1-bit output, 4-byte aligned

  // Allocate buffers for source rows needed per output row
  auto* srcRows = static_cast<uint8_t*>(malloc(srcRowBytes * maxSrcRowsPerOut));
  auto* outRow = static_cast<uint8_t*>(malloc(outRowBytes));
  if (!srcRows || !outRow) {
    Serial.printf("[%lu] [BMP] Failed to allocate buffers\n", millis());
    free(srcRows);
    free(outRow);
    srcFile.close();
    return false;
  }

  // Open destination file
  FsFile dstFile;
  if (!SdMan.openFileForWrite("BMP", dstPath, dstFile)) {
    Serial.printf("[%lu] [BMP] Failed to open destination: %s\n", millis(), dstPath);
    free(srcRows);
    free(outRow);
    srcFile.close();
    return false;
  }

  writeBmpHeader1bit(dstFile, outWidth, outHeight);

  // Create 1-bit ditherer (raw version - no contrast adjustment since source is already processed)
  RawAtkinson1BitDitherer ditherer(outWidth);

  // Seek to pixel data
  if (!srcFile.seek(pixelOffset)) {
    Serial.printf("[%lu] [BMP] Failed to seek to pixel data\n", millis());
    free(srcRows);
    free(outRow);
    srcFile.close();
    dstFile.close();
    return false;
  }

  // Track which source rows we've read (for sequential file access)
  int lastSrcRowRead = -1;

  // Process output rows
  for (int outY = 0; outY < outHeight; outY++) {
    // Calculate source Y range for this output row using fixed-point
    const int srcYStart = (static_cast<uint32_t>(outY) * scaleY_fp) >> 16;
    int srcYEnd = (static_cast<uint32_t>(outY + 1) * scaleY_fp) >> 16;
    // Ensure at least one source row is sampled (guards against upscaling edge case)
    if (srcYEnd <= srcYStart) srcYEnd = srcYStart + 1;
    const int srcRowsNeeded = srcYEnd - srcYStart;

    // Read required source rows (handling sequential access)
    for (int srcY = srcYStart; srcY < srcYEnd && srcY < srcHeight; srcY++) {
      // Skip rows we've already read past (shouldn't happen with sequential access)
      if (srcY <= lastSrcRowRead) continue;

      // Skip any rows between last read and needed row
      while (lastSrcRowRead < srcY - 1) {
        srcFile.seekCur(srcRowBytes);
        lastSrcRowRead++;
      }

      // Read this row into the appropriate buffer slot
      const int bufferSlot = srcY - srcYStart;
      if (srcFile.read(srcRows + bufferSlot * srcRowBytes, srcRowBytes) != srcRowBytes) {
        Serial.printf("[%lu] [BMP] Failed to read row %d\n", millis(), srcY);
        free(srcRows);
        free(outRow);
        srcFile.close();
        dstFile.close();
        return false;
      }
      lastSrcRowRead = srcY;
    }

    memset(outRow, 0, outRowBytes);

    // Process each output pixel
    for (int outX = 0; outX < outWidth; outX++) {
      // Calculate source X range for this output pixel using fixed-point
      const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp) >> 16;
      int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp) >> 16;
      // Ensure at least one source pixel is sampled
      if (srcXEnd <= srcXStart) srcXEnd = srcXStart + 1;

      // Average all source pixels in this range
      int sum = 0;
      int count = 0;

      for (int dy = 0; dy < srcRowsNeeded && (srcYStart + dy) < srcHeight; dy++) {
        const uint8_t* row = srcRows + dy * srcRowBytes;
        for (int srcX = srcXStart; srcX < srcXEnd && srcX < srcWidth; srcX++) {
          uint8_t gray;
          if (bpp == 2) {
            // 2-bit: 4 pixels per byte, MSB first
            const int byteIdx = srcX / 4;
            const int bitShift = 6 - (srcX % 4) * 2;
            const uint8_t pixel = (row[byteIdx] >> bitShift) & 0x03;
            gray = palette2bitToGray(pixel);
          } else {
            // 1-bit: 8 pixels per byte, MSB first
            const int byteIdx = srcX / 8;
            const int bitOffset = 7 - (srcX % 8);
            const uint8_t pixel = (row[byteIdx] >> bitOffset) & 0x01;
            gray = palette1bitToGray(pixel);
          }
          sum += gray;
          count++;
        }
      }

      const uint8_t gray = (count > 0) ? (sum / count) : 0;
      const uint8_t bit = ditherer.processPixel(gray, outX);

      // Pack 1-bit value (MSB first, 8 pixels per byte)
      const int byteIdx = outX / 8;
      const int bitOffset = 7 - (outX % 8);
      outRow[byteIdx] |= (bit << bitOffset);
    }

    ditherer.nextRow();
    dstFile.write(outRow, outRowBytes);
  }

  free(srcRows);
  free(outRow);
  srcFile.close();
  dstFile.close();

  Serial.printf("[%lu] [BMP] Successfully created thumbnail: %s\n", millis(), dstPath);
  return true;
}
