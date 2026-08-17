#include "BitmapHelpers.h"

#include <Print.h>

#include <climits>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

void put16(uint8_t* destination, const uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* destination, const uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
  destination[2] = static_cast<uint8_t>(value >> 16);
  destination[3] = static_cast<uint8_t>(value >> 24);
}

}  // namespace

bool write1BitBmpHeader(Print& output, const int width, const int height) {
  if (width <= 0 || height <= 0) return false;

  const uint64_t rowSize = (static_cast<uint64_t>(width) + 31U) / 32U * 4U;
  const uint64_t imageSize = rowSize * static_cast<uint64_t>(height);
  const uint64_t fileSize = 62U + imageSize;
  if (imageSize > UINT32_MAX || fileSize > UINT32_MAX) return false;

  uint8_t header[62] = {};
  header[0] = 'B';
  header[1] = 'M';
  put32(header + 2, static_cast<uint32_t>(fileSize));
  put32(header + 10, 62);
  put32(header + 14, 40);
  put32(header + 18, static_cast<uint32_t>(width));
  put32(header + 22, static_cast<uint32_t>(-height));
  put16(header + 26, 1);
  put16(header + 28, 1);
  put32(header + 34, static_cast<uint32_t>(imageSize));
  put32(header + 38, 2835);
  put32(header + 42, 2835);
  put32(header + 46, 2);
  put32(header + 50, 2);
  header[58] = 0xFF;
  header[59] = 0xFF;
  header[60] = 0xFF;
  return output.write(header, sizeof(header)) == sizeof(header);
}

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

bool write2BitBmpHeader(Print& output, const int width, const int height) {
  if (width <= 0 || height <= 0) return false;

  const uint64_t rowSize = (static_cast<uint64_t>(width) * 2 + 31U) / 32U * 4U;
  const uint64_t imageSize = rowSize * static_cast<uint64_t>(height);
  const uint64_t fileSize = 70U + imageSize;
  if (imageSize > UINT32_MAX || fileSize > UINT32_MAX) return false;

  uint8_t header[70] = {};
  header[0] = 'B';
  header[1] = 'M';
  put32(header + 2, static_cast<uint32_t>(fileSize));
  put32(header + 10, 70);
  put32(header + 14, 40);
  put32(header + 18, static_cast<uint32_t>(width));
  put32(header + 22, static_cast<uint32_t>(-height));
  put16(header + 26, 1);
  put16(header + 28, 2);
  put32(header + 34, static_cast<uint32_t>(imageSize));
  put32(header + 38, 2835);
  put32(header + 42, 2835);
  put32(header + 46, 4);
  put32(header + 50, 4);

  uint8_t palette[16] = {
      0x00, 0x00, 0x00, 0x00,  // Black
      0x55, 0x55, 0x55, 0x00,  // Dark gray
      0xAA, 0xAA, 0xAA, 0x00,  // Light gray
      0xFF, 0xFF, 0xFF, 0x00   // White
  };
  memcpy(header + 54, palette, sizeof(palette));
  return output.write(header, sizeof(header)) == sizeof(header);
}

GrayscaleRowScaler::GrayscaleRowScaler(const int srcWidth, const int srcHeight, const int maxW, const int maxH)
    : srcW_(srcWidth), srcH_(srcHeight), outW_(srcWidth), outH_(srcHeight) {
  if (srcW_ <= 0 || srcH_ <= 0) {
    valid_ = false;
    return;
  }

  if (maxW > 0 && maxH > 0 && (srcW_ > maxW || srcH_ > maxH)) {
    const float scaleToFitWidth = static_cast<float>(maxW) / srcW_;
    const float scaleToFitHeight = static_cast<float>(maxH) / srcH_;
    const float scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;

    outW_ = static_cast<int>(srcW_ * scale);
    outH_ = static_cast<int>(srcH_ * scale);
    if (outW_ < 1) outW_ = 1;
    if (outH_ < 1) outH_ = 1;

    scaleX_fp_ = (static_cast<uint32_t>(srcW_) << 16) / outW_;
    scaleY_fp_ = (static_cast<uint32_t>(srcH_) << 16) / outH_;
    nextOutY_srcStart_ = scaleY_fp_;
    needsScaling_ = true;

    rowAccum_.reset(new (std::nothrow) uint32_t[outW_]());
    rowCount_.reset(new (std::nothrow) uint16_t[outW_]());
    meanRow_.reset(new (std::nothrow) uint8_t[outW_]());
    if (!rowAccum_ || !rowCount_ || !meanRow_) {
      rowAccum_.reset();
      rowCount_.reset();
      meanRow_.reset();
      valid_ = false;
    }
  }
}

bool GrayscaleRowScaler::accumulate(const uint8_t* grayRow, const std::function<bool(const uint8_t* meanRow)>& emit) {
  if (!valid_ || currentOutY_ >= static_cast<uint32_t>(outH_)) return false;

  if (!needsScaling_) {
    if (!emit(grayRow)) return false;
    currentOutY_++;
    return true;
  }

  for (int outX = 0; outX < outW_; outX++) {
    const int srcXStart = (static_cast<uint32_t>(outX) * scaleX_fp_) >> 16;
    const int srcXEnd = (static_cast<uint32_t>(outX + 1) * scaleX_fp_) >> 16;

    uint32_t sum = 0;
    uint16_t count = 0;
    for (int srcX = srcXStart; srcX < srcXEnd && srcX < srcW_; srcX++) {
      sum += grayRow[srcX];
      count++;
    }
    if (count == 0 && srcXStart < srcW_) {
      sum = grayRow[srcXStart];
      count = 1;
    }
    rowAccum_[outX] += sum;
    rowCount_[outX] += count;
  }

  currentSrcY_++;
  const uint32_t srcY_fp = currentSrcY_ << 16;

  // Emit all rows whose boundaries we've crossed (handles both up- and
  // downscaling). For upscaling, one source row may produce multiple output
  // rows; accumulators are only reset once the next boundary is beyond this
  // source row.
  while (srcY_fp >= nextOutY_srcStart_ && currentOutY_ < static_cast<uint32_t>(outH_)) {
    for (int outX = 0; outX < outW_; outX++) {
      meanRow_[outX] = static_cast<uint8_t>((rowCount_[outX] > 0) ? (rowAccum_[outX] / rowCount_[outX]) : 0);
    }
    if (!emit(meanRow_.get())) return false;
    currentOutY_++;

    nextOutY_srcStart_ = (currentOutY_ + 1) * scaleY_fp_;

    if (srcY_fp >= nextOutY_srcStart_) {
      continue;  // upscaling: next output row reuses this source row's data
    }
    memset(rowAccum_.get(), 0, static_cast<size_t>(outW_) * sizeof(uint32_t));
    memset(rowCount_.get(), 0, static_cast<size_t>(outW_) * sizeof(uint16_t));
  }

  return true;
}
