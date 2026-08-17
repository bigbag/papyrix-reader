#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>

class Print;

// Helper functions
uint8_t quantize(int gray, int x, int y);
uint8_t quantizeSimple(int gray);
uint8_t quantize1bit(int gray, int x, int y);
int adjustPixel(int gray);
bool write1BitBmpHeader(Print& output, int width, int height);
bool write2BitBmpHeader(Print& output, int width, int height);

// Streaming area-average row scaler shared by image converters. Accumulates
// grayscale source rows and emits one mean output row whenever an output row
// boundary is crossed (handles both up- and downscaling, same fixed-point
// semantics as the original PNG converter). When the source already fits the
// maximum dimensions, accumulate() passes rows through unchanged.
class GrayscaleRowScaler {
 public:
  GrayscaleRowScaler(int srcWidth, int srcHeight, int maxW, int maxH);

  int outWidth() const { return outW_; }
  int outHeight() const { return outH_; }
  bool needsScaling() const { return needsScaling_; }
  // False when scaling accumulators failed to allocate.
  bool valid() const { return valid_; }

  // Feed one source row (srcWidth bytes). Invokes emit once per completed
  // output row with a meanRow of outWidth() bytes. Returns false when emit
  // fails or the scaler is invalid.
  bool accumulate(const uint8_t* grayRow, const std::function<bool(const uint8_t* meanRow)>& emit);

 private:
  int srcW_ = 0;
  int srcH_ = 0;
  int outW_ = 0;
  int outH_ = 0;
  bool needsScaling_ = false;
  bool valid_ = true;
  uint32_t scaleX_fp_ = 65536;
  uint32_t scaleY_fp_ = 65536;
  uint32_t nextOutY_srcStart_ = 65536;
  uint32_t currentSrcY_ = 0;
  uint32_t currentOutY_ = 0;
  std::unique_ptr<uint32_t[]> rowAccum_;
  std::unique_ptr<uint16_t[]> rowCount_;
  std::unique_ptr<uint8_t[]> meanRow_;
};

// RGB to grayscale conversion using BT.601 coefficients via lookup tables.
// Avoids 3 multiplications per pixel on ESP32-C3 (no FPU).
// Note: Sum of max values is 254 (not 255) due to integer truncation of coefficients.
// This is expected behavior - pure white (255,255,255) maps to 254.
uint8_t rgbToGray(uint8_t r, uint8_t g, uint8_t b);

// 1-bit Atkinson dithering - better quality than noise dithering for monochrome images
// Error distribution pattern (same as 2-bit but quantizes to 2 levels):
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
class Atkinson1BitDitherer {
 public:
  explicit Atkinson1BitDitherer(int width) : width(width) {
    errorRow0 = new (std::nothrow) int16_t[width + 4]();
    errorRow1 = new (std::nothrow) int16_t[width + 4]();
    errorRow2 = new (std::nothrow) int16_t[width + 4]();
    if (!errorRow0 || !errorRow1 || !errorRow2) {
      delete[] errorRow0;
      delete[] errorRow1;
      delete[] errorRow2;
      errorRow0 = errorRow1 = errorRow2 = nullptr;
    }
  }

  ~Atkinson1BitDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  bool valid() const { return errorRow0 != nullptr; }

  // EXPLICITLY DELETE THE COPY CONSTRUCTOR
  Atkinson1BitDitherer(const Atkinson1BitDitherer& other) = delete;

  // EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR
  Atkinson1BitDitherer& operator=(const Atkinson1BitDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) { return processPixelWithoutAdjustment(adjustPixel(gray), x); }

  uint8_t processPixelWithoutAdjustment(int gray, int x) {
    assert(x >= 0 && x < width);
    if (gray < 0) gray = 0;
    if (gray > 255) gray = 255;
    if (!valid()) return gray >= 128 ? 1 : 0;

    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    const uint8_t quantized = adjusted < 128 ? 0 : 1;
    const int quantizedValue = quantized ? 255 : 0;
    const int error = (adjusted - quantizedValue) >> 3;

    errorRow0[x + 3] += error;
    errorRow0[x + 4] += error;
    errorRow1[x + 1] += error;
    errorRow1[x + 2] += error;
    errorRow1[x + 3] += error;
    errorRow2[x + 2] += error;

    return quantized;
  }

  void nextRow() {
    if (!valid()) return;
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    if (!valid()) return;
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Atkinson dithering - distributes only 6/8 (75%) of error for cleaner results
// Error distribution pattern:
//     X  1/8 1/8
// 1/8 1/8 1/8
//     1/8
// Less error buildup = fewer artifacts than Floyd-Steinberg
class AtkinsonDitherer {
 public:
  explicit AtkinsonDitherer(int width) : width(width) {
    errorRow0 = new (std::nothrow) int16_t[width + 4]();
    errorRow1 = new (std::nothrow) int16_t[width + 4]();
    errorRow2 = new (std::nothrow) int16_t[width + 4]();
    if (!errorRow0 || !errorRow1 || !errorRow2) {
      delete[] errorRow0;
      delete[] errorRow1;
      delete[] errorRow2;
      errorRow0 = errorRow1 = errorRow2 = nullptr;
    }
  }

  ~AtkinsonDitherer() {
    delete[] errorRow0;
    delete[] errorRow1;
    delete[] errorRow2;
  }

  bool valid() const { return errorRow0 != nullptr; }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  AtkinsonDitherer(const AtkinsonDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  AtkinsonDitherer& operator=(const AtkinsonDitherer& other) = delete;

  uint8_t processPixel(int gray, int x) {
    assert(x >= 0 && x < width);
    if (!valid()) {
      if (gray < 30) return 0;
      if (gray < 50) return 1;
      if (gray < 140) return 2;
      return 3;
    }
    // Add accumulated error
    int adjusted = gray + errorRow0[x + 2];
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels
    uint8_t quantized;
    int quantizedValue;
    if (false) {  // original thresholds
      if (adjusted < 43) {
        quantized = 0;
        quantizedValue = 0;
      } else if (adjusted < 128) {
        quantized = 1;
        quantizedValue = 85;
      } else if (adjusted < 213) {
        quantized = 2;
        quantizedValue = 170;
      } else {
        quantized = 3;
        quantizedValue = 255;
      }
    } else {  // fine-tuned to X4 eink display
      if (adjusted < 30) {
        quantized = 0;
        quantizedValue = 15;
      } else if (adjusted < 50) {
        quantized = 1;
        quantizedValue = 30;
      } else if (adjusted < 140) {
        quantized = 2;
        quantizedValue = 80;
      } else {
        quantized = 3;
        quantizedValue = 210;
      }
    }

    // Calculate error (only distribute 6/8 = 75%)
    int error = (adjusted - quantizedValue) >> 3;  // error/8

    // Distribute 1/8 to each of 6 neighbors
    errorRow0[x + 3] += error;  // Right
    errorRow0[x + 4] += error;  // Right+1
    errorRow1[x + 1] += error;  // Bottom-left
    errorRow1[x + 2] += error;  // Bottom
    errorRow1[x + 3] += error;  // Bottom-right
    errorRow2[x + 2] += error;  // Two rows down

    return quantized;
  }

  void nextRow() {
    if (!valid()) return;
    int16_t* temp = errorRow0;
    errorRow0 = errorRow1;
    errorRow1 = errorRow2;
    errorRow2 = temp;
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

  void reset() {
    if (!valid()) return;
    memset(errorRow0, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow1, 0, (width + 4) * sizeof(int16_t));
    memset(errorRow2, 0, (width + 4) * sizeof(int16_t));
  }

 private:
  int width;
  int16_t* errorRow0;
  int16_t* errorRow1;
  int16_t* errorRow2;
};

// Floyd-Steinberg error diffusion dithering with serpentine scanning
// Serpentine scanning alternates direction each row to reduce "worm" artifacts
// Error distribution pattern (left-to-right):
//       X   7/16
// 3/16 5/16 1/16
// Error distribution pattern (right-to-left, mirrored):
// 1/16 5/16 3/16
//      7/16  X
class FloydSteinbergDitherer {
 public:
  explicit FloydSteinbergDitherer(int width) : width(width), rowCount(0) {
    errorCurRow = new (std::nothrow) int16_t[width + 2]();
    errorNextRow = new (std::nothrow) int16_t[width + 2]();
    if (!errorCurRow || !errorNextRow) {
      delete[] errorCurRow;
      delete[] errorNextRow;
      errorCurRow = errorNextRow = nullptr;
    }
  }

  ~FloydSteinbergDitherer() {
    delete[] errorCurRow;
    delete[] errorNextRow;
  }

  bool valid() const { return errorCurRow != nullptr; }

  // **1. EXPLICITLY DELETE THE COPY CONSTRUCTOR**
  FloydSteinbergDitherer(const FloydSteinbergDitherer& other) = delete;

  // **2. EXPLICITLY DELETE THE COPY ASSIGNMENT OPERATOR**
  FloydSteinbergDitherer& operator=(const FloydSteinbergDitherer& other) = delete;

  // Process a single pixel and return quantized 2-bit value
  // x is the logical x position (0 to width-1), direction handled internally
  uint8_t processPixel(int gray, int x) {
    assert(x >= 0 && x < width);
    if (!valid()) {
      if (gray < 30) return 0;
      if (gray < 50) return 1;
      if (gray < 140) return 2;
      return 3;
    }
    // Add accumulated error to this pixel
    int adjusted = gray + errorCurRow[x + 1];

    // Clamp to valid range
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 255) adjusted = 255;

    // Quantize to 4 levels (0, 85, 170, 255)
    uint8_t quantized;
    int quantizedValue;
    if (false) {  // original thresholds
      if (adjusted < 43) {
        quantized = 0;
        quantizedValue = 0;
      } else if (adjusted < 128) {
        quantized = 1;
        quantizedValue = 85;
      } else if (adjusted < 213) {
        quantized = 2;
        quantizedValue = 170;
      } else {
        quantized = 3;
        quantizedValue = 255;
      }
    } else {  // fine-tuned to X4 eink display
      if (adjusted < 30) {
        quantized = 0;
        quantizedValue = 15;
      } else if (adjusted < 50) {
        quantized = 1;
        quantizedValue = 30;
      } else if (adjusted < 140) {
        quantized = 2;
        quantizedValue = 80;
      } else {
        quantized = 3;
        quantizedValue = 210;
      }
    }

    // Calculate error
    int error = adjusted - quantizedValue;

    // Distribute error to neighbors (serpentine: direction-aware)
    if (!isReverseRow()) {
      // Left to right: standard distribution
      // Right: 7/16
      errorCurRow[x + 2] += (error * 7) >> 4;
      // Bottom-left: 3/16
      errorNextRow[x] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-right: 1/16
      errorNextRow[x + 2] += (error) >> 4;
    } else {
      // Right to left: mirrored distribution
      // Left: 7/16
      errorCurRow[x] += (error * 7) >> 4;
      // Bottom-right: 3/16
      errorNextRow[x + 2] += (error * 3) >> 4;
      // Bottom: 5/16
      errorNextRow[x + 1] += (error * 5) >> 4;
      // Bottom-left: 1/16
      errorNextRow[x] += (error) >> 4;
    }

    return quantized;
  }

  // Call at the end of each row to swap buffers
  void nextRow() {
    if (!valid()) {
      rowCount++;
      return;
    }
    // Swap buffers
    int16_t* temp = errorCurRow;
    errorCurRow = errorNextRow;
    errorNextRow = temp;
    // Clear the next row buffer
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount++;
  }

  // Check if current row should be processed in reverse
  bool isReverseRow() const { return (rowCount & 1) != 0; }

  // Reset for a new image or MCU block
  void reset() {
    if (!valid()) {
      rowCount = 0;
      return;
    }
    memset(errorCurRow, 0, (width + 2) * sizeof(int16_t));
    memset(errorNextRow, 0, (width + 2) * sizeof(int16_t));
    rowCount = 0;
  }

 private:
  int width;
  int rowCount;
  int16_t* errorCurRow;
  int16_t* errorNextRow;
};
