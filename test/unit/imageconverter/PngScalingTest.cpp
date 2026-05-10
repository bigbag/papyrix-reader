#include "test_utils.h"

#include <algorithm>
#include <cmath>

// Mirrors the scaling logic from PngToBmpConverter.cpp pngInitCallback()
// Only scales DOWN (when source exceeds target), never up.
struct PngScalingResult {
  int outWidth;
  int outHeight;
  bool needsScaling;
};

PngScalingResult computePngScaledDimensions(int srcWidth, int srcHeight, int targetMaxWidth, int targetMaxHeight) {
  PngScalingResult result = {srcWidth, srcHeight, false};

  if (targetMaxWidth > 0 && targetMaxHeight > 0 &&
      (srcWidth > targetMaxWidth || srcHeight > targetMaxHeight)) {
    const float scaleToFitWidth = static_cast<float>(targetMaxWidth) / srcWidth;
    const float scaleToFitHeight = static_cast<float>(targetMaxHeight) / srcHeight;
    const float scale = (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight;

    result.outWidth = static_cast<int>(srcWidth * scale);
    result.outHeight = static_cast<int>(srcHeight * scale);
    if (result.outWidth < 1) result.outWidth = 1;
    if (result.outHeight < 1) result.outHeight = 1;
    result.needsScaling = true;
  }

  return result;
}

int main() {
  TestUtils::TestRunner runner("PngToBmpConverter Scaling");

  // ============================================
  // Downscale-only behavior (same as JPEG)
  // Target: 464x765 (device viewport)
  // ============================================

  // Test 1: Image larger than target - width exceeds
  {
    PngScalingResult r = computePngScaledDimensions(800, 400, 464, 765);
    runner.expectTrue(r.needsScaling, "800x400: needs scaling (width exceeds)");
    runner.expectEq(464, r.outWidth, "800x400: width = 464");
    runner.expectEq(232, r.outHeight, "800x400: height = 232");
  }

  // Test 2: Image larger than target - height exceeds
  {
    PngScalingResult r = computePngScaledDimensions(300, 1000, 464, 765);
    runner.expectTrue(r.needsScaling, "300x1000: needs scaling (height exceeds)");
    runner.expectEq(229, r.outWidth, "300x1000: width = 229");
    runner.expectEq(765, r.outHeight, "300x1000: height = 765");
  }

  // Test 3: Both dimensions exceed
  {
    PngScalingResult r = computePngScaledDimensions(928, 1530, 464, 765);
    runner.expectTrue(r.needsScaling, "928x1530: needs scaling (both exceed)");
    runner.expectTrue(r.outWidth <= 464, "928x1530: width fits");
    runner.expectTrue(r.outHeight <= 765, "928x1530: height fits");
  }

  // ============================================
  // NO upscaling: smaller images stay unchanged
  // (This is the key behavioral change being tested)
  // ============================================

  // Test 4: Image smaller than target in both dimensions - no scaling
  {
    PngScalingResult r = computePngScaledDimensions(200, 300, 464, 765);
    runner.expectFalse(r.needsScaling, "200x300: no scaling (both smaller)");
    runner.expectEq(200, r.outWidth, "200x300: width preserved");
    runner.expectEq(300, r.outHeight, "200x300: height preserved");
  }

  // Test 5: Image exactly at target - no scaling
  {
    PngScalingResult r = computePngScaledDimensions(464, 765, 464, 765);
    runner.expectFalse(r.needsScaling, "464x765: no scaling (exact match)");
    runner.expectEq(464, r.outWidth, "464x765: width preserved");
    runner.expectEq(765, r.outHeight, "464x765: height preserved");
  }

  // Test 6: One dimension at limit, other smaller - no scaling
  {
    PngScalingResult r = computePngScaledDimensions(464, 500, 464, 765);
    runner.expectFalse(r.needsScaling, "464x500: no scaling (within bounds)");
    runner.expectEq(464, r.outWidth, "464x500: width preserved");
    runner.expectEq(500, r.outHeight, "464x500: height preserved");
  }

  // Test 7: Width at limit, height at limit - no scaling
  {
    PngScalingResult r = computePngScaledDimensions(464, 765, 464, 765);
    runner.expectFalse(r.needsScaling, "exact target: no scaling");
  }

  // Test 8: Width 1px below target, height 1px below target - no scaling
  {
    PngScalingResult r = computePngScaledDimensions(463, 764, 464, 765);
    runner.expectFalse(r.needsScaling, "463x764: no scaling (just below target)");
  }

  // Test 9: Width 1px ABOVE target - scales down
  {
    PngScalingResult r = computePngScaledDimensions(465, 764, 464, 765);
    runner.expectTrue(r.needsScaling, "465x764: needs scaling (width exceeds by 1)");
    runner.expectTrue(r.outWidth <= 464, "465x764: fits after scaling");
  }

  // ============================================
  // Aspect ratio preservation
  // ============================================

  // Test 10: 16:9 image
  {
    PngScalingResult r = computePngScaledDimensions(1920, 1080, 464, 765);
    runner.expectTrue(r.needsScaling, "16:9: needs scaling");
    float originalRatio = 1920.0f / 1080.0f;
    float outputRatio = static_cast<float>(r.outWidth) / r.outHeight;
    runner.expectFloatEq(originalRatio, outputRatio, "16:9: aspect ratio preserved", 0.02f);
  }

  // Test 11: Square image exceeding width
  {
    PngScalingResult r = computePngScaledDimensions(600, 600, 464, 765);
    runner.expectTrue(r.needsScaling, "600x600: needs scaling");
    runner.expectEq(464, r.outWidth, "600x600: width = 464");
    runner.expectEq(464, r.outHeight, "600x600: height = 464 (square preserved)");
  }

  // ============================================
  // Edge cases
  // ============================================

  // Test 12: Zero target - no scaling
  {
    PngScalingResult r = computePngScaledDimensions(800, 600, 0, 0);
    runner.expectFalse(r.needsScaling, "zero target: no scaling");
    runner.expectEq(800, r.outWidth, "zero target: width preserved");
  }

  // Test 13: Extreme downscale
  {
    PngScalingResult r = computePngScaledDimensions(4000, 3000, 464, 765);
    runner.expectTrue(r.needsScaling, "4000x3000: needs scaling");
    runner.expectTrue(r.outWidth <= 464, "4000x3000: width fits");
    runner.expectTrue(r.outHeight <= 765, "4000x3000: height fits");
    runner.expectTrue(r.outWidth >= 1, "4000x3000: width >= 1");
    runner.expectTrue(r.outHeight >= 1, "4000x3000: height >= 1");
  }

  // Test 14: Contain mode - output never exceeds target
  {
    int testCases[][2] = {{800, 400}, {400, 900}, {1920, 1080}, {1080, 1920}, {2048, 2048}};
    bool allContained = true;
    for (auto& tc : testCases) {
      PngScalingResult r = computePngScaledDimensions(tc[0], tc[1], 464, 765);
      if (r.needsScaling && (r.outWidth > 464 || r.outHeight > 765)) {
        allContained = false;
        break;
      }
    }
    runner.expectTrue(allContained, "contain mode: all outputs within target bounds");
  }

  // Test 15: PNG uses min() not std::min (verify same behavior as ternary)
  // The PNG code uses: (scaleToFitWidth < scaleToFitHeight) ? scaleToFitWidth : scaleToFitHeight
  {
    // Case where width scale < height scale
    PngScalingResult r = computePngScaledDimensions(800, 400, 464, 765);
    // scaleW = 464/800 = 0.58, scaleH = 765/400 = 1.9125
    // min = 0.58, so width-constrained
    runner.expectEq(464, r.outWidth, "ternary min: width-constrained correct");

    // Case where height scale < width scale
    PngScalingResult r2 = computePngScaledDimensions(300, 1000, 464, 765);
    // scaleW = 464/300 = 1.547, scaleH = 765/1000 = 0.765
    // min = 0.765, so height-constrained
    runner.expectEq(765, r2.outHeight, "ternary min: height-constrained correct");
  }

  return runner.allPassed() ? 0 : 1;
}
