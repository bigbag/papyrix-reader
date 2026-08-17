#include "test_utils.h"

#include <functional>
#include <string>

// Replicate ImageConvertConfig from lib/ImageConverter/ImageConverter.h
// to test struct defaults and routing logic without hardware dependencies
struct TestImageConvertConfig {
  int maxWidth = 450;
  int maxHeight = 750;
  bool oneBit = false;
  bool requireDithering = false;
  const char* logTag = "IMG";
  std::function<bool()> shouldAbort = nullptr;
  std::function<bool(const std::string&)> validateOutput = nullptr;
};

// Routing decision logic extracted from JpegImageConverter::convert()
// Returns which code path the JPEG converter would take
enum class JpegRoute {
  FastPath,            // jpegFileToBmpStream (no scaling, no abort support)
  FastPath1Bit,        // jpegFileTo1BitBmpStream (no scaling, no abort support)
  WithSize,            // jpegFileToBmpStreamWithSize (supports abort)
  WithSize1Bit,        // jpegFileTo1BitBmpStreamWithSize
};

JpegRoute getJpegRoute(const TestImageConvertConfig& config) {
  if (config.maxWidth == 450 && config.maxHeight == 750 && !config.shouldAbort) {
    return config.oneBit ? JpegRoute::FastPath1Bit : JpegRoute::FastPath;
  }
  return config.oneBit ? JpegRoute::WithSize1Bit : JpegRoute::WithSize;
}

bool jpegRouteSupportsAbort(const JpegRoute route) {
  return route == JpegRoute::WithSize || route == JpegRoute::WithSize1Bit;
}

// Routing decision logic extracted from PngImageConverter::convert()
enum class PngRoute {
  WithSize,
};

PngRoute getPngRoute(const TestImageConvertConfig&) { return PngRoute::WithSize; }

bool pngRouteSupportsAbort(const PngRoute route) { return route == PngRoute::WithSize; }

int main() {
  TestUtils::TestRunner runner("ImageConvertConfig");

  // ============================================
  // Default values
  // ============================================

  // Test 1: Default config values
  {
    TestImageConvertConfig config;
    runner.expectEq(450, config.maxWidth, "default_maxWidth");
    runner.expectEq(750, config.maxHeight, "default_maxHeight");
    runner.expectFalse(config.oneBit, "default_oneBit");
    runner.expectFalse(config.requireDithering, "default_requireDithering");
    runner.expectTrue(config.shouldAbort == nullptr, "default_shouldAbort_null");
    runner.expectTrue(config.validateOutput == nullptr, "default_validateOutput_null");
  }

  // Test 2: shouldAbort can be set to a callback
  {
    TestImageConvertConfig config;
    bool abortRequested = false;
    config.shouldAbort = [&abortRequested]() { return abortRequested; };

    runner.expectTrue(config.shouldAbort != nullptr, "shouldAbort_set_not_null");
    runner.expectFalse(config.shouldAbort(), "shouldAbort_returns_false_initially");

    abortRequested = true;
    runner.expectTrue(config.shouldAbort(), "shouldAbort_returns_true_after_set");
  }

  // Test 3: shouldAbort bool conversion for routing checks
  {
    TestImageConvertConfig config;
    // nullptr converts to false in boolean context
    runner.expectFalse(static_cast<bool>(config.shouldAbort), "null_shouldAbort_is_falsy");

    config.shouldAbort = []() { return false; };
    // Non-null function converts to true in boolean context
    runner.expectTrue(static_cast<bool>(config.shouldAbort), "set_shouldAbort_is_truthy");
  }

  // ============================================
  // JPEG routing logic
  // ============================================

  // Test 4: Default config (480x800, no abort) -> FastPath
  {
    TestImageConvertConfig config;
    runner.expectTrue(getJpegRoute(config) == JpegRoute::FastPath, "jpeg_default_fastpath");
  }

  // Test 5: Default config + oneBit -> FastPath1Bit
  {
    TestImageConvertConfig config;
    config.oneBit = true;
    runner.expectTrue(getJpegRoute(config) == JpegRoute::FastPath1Bit, "jpeg_1bit_fastpath");
  }

  // Test 7: Non-default size -> WithSize (supports abort callback)
  {
    TestImageConvertConfig config;
    config.maxWidth = 240;
    config.maxHeight = 400;
    runner.expectTrue(getJpegRoute(config) == JpegRoute::WithSize, "jpeg_custom_size_withsize");
  }

  // Test 8: KEY CHANGE - shouldAbort set with 450x750 -> bypasses fast path, uses WithSize
  // Before this change: 450x750 always used FastPath (no abort support)
  // After this change: 450x750 + shouldAbort routes to WithSize (supports abort)
  {
    TestImageConvertConfig config;
    config.shouldAbort = []() { return false; };
    runner.expectTrue(getJpegRoute(config) == JpegRoute::WithSize,
                      "jpeg_450x750_with_abort_uses_withsize");
  }

  // Test 9: shouldAbort set with 450x750 + oneBit -> WithSize1Bit
  {
    TestImageConvertConfig config;
    config.oneBit = true;
    config.shouldAbort = []() { return false; };
    runner.expectTrue(getJpegRoute(config) == JpegRoute::WithSize1Bit,
                      "jpeg_450x750_1bit_with_abort_uses_withsize1bit");
    runner.expectTrue(jpegRouteSupportsAbort(getJpegRoute(config)), "jpeg_1bit_withsize_propagates_abort");
  }

  // Test 10: Only width differs from 450 -> WithSize
  {
    TestImageConvertConfig config;
    config.maxWidth = 200;
    // maxHeight still 750
    runner.expectTrue(getJpegRoute(config) == JpegRoute::WithSize, "jpeg_width_differs_withsize");
  }

  // Test 11: Only height differs from 750 -> WithSize
  {
    TestImageConvertConfig config;
    config.maxHeight = 400;
    // maxWidth still 450
    runner.expectTrue(getJpegRoute(config) == JpegRoute::WithSize, "jpeg_height_differs_withsize");
  }

  // ============================================
  // PNG routing logic
  // ============================================

  // Test 12: Default PNG config -> WithSize (always passes shouldAbort through)
  {
    TestImageConvertConfig config;
    runner.expectTrue(getPngRoute(config) == PngRoute::WithSize, "png_default_withsize");
  }

  // Test 14: PNG with shouldAbort still uses WithSize (which propagates it)
  {
    TestImageConvertConfig config;
    config.shouldAbort = []() { return false; };
    runner.expectTrue(getPngRoute(config) == PngRoute::WithSize, "png_with_abort_withsize");
  }

  // ============================================
  // Abort callback semantics
  // ============================================

  // Test 15: Abort callback called multiple times returns consistent results
  {
    int callCount = 0;
    int abortAfter = 3;
    std::function<bool()> shouldAbort = [&]() {
      callCount++;
      return callCount > abortAfter;
    };

    runner.expectFalse(shouldAbort(), "abort_call_1_false");
    runner.expectFalse(shouldAbort(), "abort_call_2_false");
    runner.expectFalse(shouldAbort(), "abort_call_3_false");
    runner.expectTrue(shouldAbort(), "abort_call_4_true");
    runner.expectTrue(shouldAbort(), "abort_call_5_true");
    runner.expectEq(5, callCount, "abort_called_5_times");
  }

  // Test 16: Null abort callback pattern - safe to check before calling
  {
    std::function<bool()> shouldAbort = nullptr;

    // The pattern used in JPEG/PNG converters:
    // if (shouldAbort && shouldAbort()) { abort; }
    bool aborted = false;
    if (shouldAbort && shouldAbort()) {
      aborted = true;
    }
    runner.expectFalse(aborted, "null_abort_check_pattern_safe");
  }

  // Test 17: Non-null abort callback returning false - conversion continues
  {
    std::function<bool()> shouldAbort = []() { return false; };

    bool aborted = false;
    if (shouldAbort && shouldAbort()) {
      aborted = true;
    }
    runner.expectFalse(aborted, "false_abort_continues");
  }

  // Test 18: Non-null abort callback returning true - conversion stops
  {
    std::function<bool()> shouldAbort = []() { return true; };

    bool aborted = false;
    if (shouldAbort && shouldAbort()) {
      aborted = true;
    }
    runner.expectTrue(aborted, "true_abort_stops");
  }

  // ============================================
  // Simulated conversion loop with abort
  // ============================================

  // Test 19: Simulated MCU row loop (JPEG pattern) - abort mid-conversion
  {
    int totalRows = 100;
    int rowsProcessed = 0;
    int abortAtRow = 30;

    std::function<bool()> shouldAbort = [&]() { return rowsProcessed >= abortAtRow; };

    bool success = true;
    for (int mcuY = 0; mcuY < totalRows; mcuY++) {
      if (shouldAbort && shouldAbort()) {
        success = false;
        break;
      }
      rowsProcessed++;
    }

    runner.expectFalse(success, "mcu_loop_abort_returns_false");
    runner.expectEq(abortAtRow, rowsProcessed, "mcu_loop_abort_at_correct_row");
  }

  // Test 20: Simulated MCU row loop without abort - completes normally
  {
    int totalRows = 100;
    int rowsProcessed = 0;

    std::function<bool()> shouldAbort = nullptr;

    bool success = true;
    for (int mcuY = 0; mcuY < totalRows; mcuY++) {
      if (shouldAbort && shouldAbort()) {
        success = false;
        break;
      }
      rowsProcessed++;
    }

    runner.expectTrue(success, "mcu_loop_no_abort_completes");
    runner.expectEq(totalRows, rowsProcessed, "mcu_loop_no_abort_all_rows");
  }

  // Test 21: Simulated PNG pixel callback pattern - abort at row start
  {
    int totalRows = 50;
    int rowWidth = 100;
    int rowsProcessed = 0;
    int abortAtRow = 15;
    bool aborted = false;

    std::function<bool()> shouldAbort = [&]() { return rowsProcessed >= abortAtRow; };

    for (int y = 0; y < totalRows && !aborted; y++) {
      for (int x = 0; x < rowWidth; x++) {
        // Check abort at start of each row (x == 0), matching PNG pattern
        if (x == 0 && shouldAbort && shouldAbort()) {
          aborted = true;
          break;
        }
        // Simulate pixel processing
      }
      if (!aborted) rowsProcessed++;
    }

    runner.expectTrue(aborted, "png_pixel_abort_detected");
    runner.expectEq(abortAtRow, rowsProcessed, "png_pixel_abort_at_correct_row");
  }

  // ============================================
  // cacheImage abort pattern
  // ============================================

  // Test 22: cacheImage pattern - abort before image processing
  {
    bool abortRequested = true;
    std::function<bool()> externalAbortCallback = [&]() { return abortRequested; };

    // Mirrors ChapterHtmlSlimParser::cacheImage() abort check
    std::string result;
    if (externalAbortCallback && externalAbortCallback()) {
      result = "";  // Skip image
    } else {
      result = "/path/to/cached.bmp";
    }

    runner.expectTrue(result.empty(), "cacheImage_abort_returns_empty");
  }

  // Test 23: cacheImage pattern - no abort proceeds normally
  {
    std::function<bool()> externalAbortCallback = nullptr;

    std::string result;
    if (externalAbortCallback && externalAbortCallback()) {
      result = "";
    } else {
      result = "/path/to/cached.bmp";
    }

    runner.expectFalse(result.empty(), "cacheImage_no_abort_returns_path");
  }

  // Test 24: startElement pattern - abort before cacheImage
  {
    int cacheImageCalls = 0;

    std::function<bool()> abortAlways = []() { return true; };
    std::function<bool()> abortNever = []() { return false; };

    // Simulates the startElement flow for <img> tags
    auto processImage = [&](const std::function<bool()>& shouldAbort) -> bool {
      // Check abort before cacheImage
      if (shouldAbort && shouldAbort()) {
        return false;
      }

      // Simulate cacheImage call
      cacheImageCalls++;

      // Check abort after cacheImage
      if (shouldAbort && shouldAbort()) {
        return false;
      }

      return true;
    };

    // Normal case: no abort
    runner.expectTrue(processImage(abortNever), "startElement_no_abort_succeeds");
    runner.expectEq(1, cacheImageCalls, "startElement_no_abort_calls_cache");

    // Abort before cache
    cacheImageCalls = 0;
    runner.expectFalse(processImage(abortAlways), "startElement_abort_before_cache");
    runner.expectEq(0, cacheImageCalls, "startElement_abort_before_skips_cache");

    // Null callback: no abort
    cacheImageCalls = 0;
    runner.expectTrue(processImage(nullptr), "startElement_null_callback_succeeds");
    runner.expectEq(1, cacheImageCalls, "startElement_null_callback_calls_cache");
  }

  // Test 25: startElement pattern - abort triggered during slow cacheImage
  {
    int cacheImageCalls = 0;
    bool cacheImageDone = false;

    std::function<bool()> abortAfterCache = [&]() { return cacheImageDone; };

    // Check abort before cacheImage
    cacheImageDone = false;
    bool abortedBefore = abortAfterCache && abortAfterCache();
    runner.expectFalse(abortedBefore, "startElement_not_aborted_before_cache");

    // Simulate cacheImage (slow conversion triggers abort externally)
    cacheImageCalls++;
    cacheImageDone = true;

    // Check abort after cacheImage
    bool abortedAfter = abortAfterCache && abortAfterCache();
    runner.expectTrue(abortedAfter, "startElement_aborted_after_cache");
    runner.expectEq(1, cacheImageCalls, "startElement_cache_was_called");
  }

  return runner.allPassed() ? 0 : 1;
}
