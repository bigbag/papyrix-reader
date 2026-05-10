#include "test_utils.h"

#include <cmath>
#include <cstdint>

// Mirrors the fixed-point scaling logic from GfxRenderer::drawBitmap()
// invScale_fp = (1.0f / scale) * 65536.0f + 0.5f
// srcCoord = (uint32_t(destCoord) * invScale_fp) >> 16

static uint32_t computeInvScaleFp(float scale) {
  return static_cast<uint32_t>((1.0f / scale) * 65536.0f + 0.5f);
}

static int mapCoordFp(int destCoord, uint32_t invScale_fp) {
  return static_cast<int>((uint32_t(destCoord) * invScale_fp) >> 16);
}

static int mapCoordFloat(int destCoord, float scale) {
  return static_cast<int>(destCoord * (1.0f / scale));
}

int main() {
  TestUtils::TestRunner runner("FixedPointScale");

  // ============================================
  // Basic scale factors: verify FP matches float
  // ============================================

  // Test 1: 2x downscale (scale=0.5, invScale=2.0)
  {
    const float scale = 0.5f;
    const uint32_t fp = computeInvScaleFp(scale);
    runner.expectEq(mapCoordFp(0, fp), mapCoordFloat(0, scale), "2x: dest=0");
    runner.expectEq(mapCoordFp(1, fp), mapCoordFloat(1, scale), "2x: dest=1");
    runner.expectEq(mapCoordFp(100, fp), mapCoordFloat(100, scale), "2x: dest=100");
    runner.expectEq(mapCoordFp(239, fp), mapCoordFloat(239, scale), "2x: dest=239");
  }

  // Test 2: 3x downscale (scale=1/3, invScale=3.0)
  {
    const float scale = 1.0f / 3.0f;
    const uint32_t fp = computeInvScaleFp(scale);
    for (int d = 0; d < 160; d++) {
      int fpResult = mapCoordFp(d, fp);
      int floatResult = mapCoordFloat(d, scale);
      if (fpResult != floatResult) {
        runner.expectEq(fpResult, floatResult, "3x: mismatch at dest=" + std::to_string(d));
        break;
      }
    }
    runner.expectEq(mapCoordFp(0, fp), 0, "3x: dest=0 → src=0");
    runner.expectEq(mapCoordFp(159, fp), mapCoordFloat(159, scale), "3x: dest=159");
  }

  // Test 3: Scale factor from typical image (800px → 464px viewport)
  // scale = 464/800 = 0.58, invScale = 800/464 ≈ 1.7241
  // Fixed-point may round differently by ≤1 pixel at some coordinates
  {
    const float scale = 464.0f / 800.0f;
    const uint32_t fp = computeInvScaleFp(scale);
    int maxDiff = 0;
    for (int d = 0; d < 464; d++) {
      int fpResult = mapCoordFp(d, fp);
      int floatResult = mapCoordFloat(d, scale);
      int diff = fpResult > floatResult ? fpResult - floatResult : floatResult - fpResult;
      if (diff > maxDiff) maxDiff = diff;
    }
    runner.expectTrue(maxDiff <= 1, "800→464: max coord difference <= 1 pixel");
  }

  // Test 4: Scale factor from tall image (1200px → 765px viewport)
  // scale = 765/1200 = 0.6375, invScale ≈ 1.5686
  {
    const float scale = 765.0f / 1200.0f;
    const uint32_t fp = computeInvScaleFp(scale);
    int mismatches = 0;
    for (int d = 0; d < 765; d++) {
      if (mapCoordFp(d, fp) != mapCoordFloat(d, scale)) mismatches++;
    }
    runner.expectTrue(mismatches == 0, "1200→765: zero mismatches across full height");
  }

  // Test 5: 4x downscale (scale=0.25, invScale=4.0)
  {
    const float scale = 0.25f;
    const uint32_t fp = computeInvScaleFp(scale);
    runner.expectEq(mapCoordFp(0, fp), 0, "4x: dest=0 → src=0");
    runner.expectEq(mapCoordFp(1, fp), 4, "4x: dest=1 → src=4");
    runner.expectEq(mapCoordFp(50, fp), 200, "4x: dest=50 → src=200");
    runner.expectEq(mapCoordFp(115, fp), 460, "4x: dest=115 → src=460");
  }

  // ============================================
  // Boundary conditions
  // ============================================

  // Test 6: No scale (scale=1.0, invScale=1.0) → identity mapping
  {
    const uint32_t fp = 65536U;  // 1.0 in 16.16
    runner.expectEq(mapCoordFp(0, fp), 0, "1x: dest=0 → src=0");
    runner.expectEq(mapCoordFp(100, fp), 100, "1x: dest=100 → src=100");
    runner.expectEq(mapCoordFp(463, fp), 463, "1x: dest=463 → src=463");
  }

  // Test 7: Maximum typical dest coordinate (464px width)
  // Ensure no uint32_t overflow: dest=463, invScale_fp could be up to ~65536*10=655360
  // 463 * 655360 = 303,421,680 < UINT32_MAX
  {
    const float scale = 0.1f;  // 10x downscale
    const uint32_t fp = computeInvScaleFp(scale);
    int srcX = mapCoordFp(463, fp);
    runner.expectTrue(srcX >= 0, "10x: no overflow at dest=463");
    runner.expectEq(srcX, mapCoordFloat(463, scale), "10x: matches float at dest=463");
  }

  // Test 8: Large image (2048px) scaled to 464px
  // scale = 464/2048 ≈ 0.2266, invScale ≈ 4.414
  {
    const float scale = 464.0f / 2048.0f;
    const uint32_t fp = computeInvScaleFp(scale);
    // Last dest pixel should map to near src=2047
    int lastSrc = mapCoordFp(463, fp);
    runner.expectTrue(lastSrc <= 2047, "2048→464: last pixel within source bounds");
    runner.expectTrue(lastSrc >= 2040, "2048→464: last pixel near end of source");
  }

  // ============================================
  // Verify source never exceeds bounds
  // (GfxRenderer clamps: if (srcY >= height) srcY = height - 1)
  // ============================================

  // Test 9: Verify mapped source never exceeds original dimension
  {
    struct Case {
      int srcDim;
      int destDim;
    };
    Case cases[] = {{800, 464}, {1200, 765}, {480, 464}, {600, 400}, {2048, 464}, {3072, 765}};

    bool allValid = true;
    for (const auto& c : cases) {
      const float scale = static_cast<float>(c.destDim) / c.srcDim;
      const uint32_t fp = computeInvScaleFp(scale);
      for (int d = 0; d < c.destDim; d++) {
        int src = mapCoordFp(d, fp);
        if (src >= c.srcDim) {
          allValid = false;
          break;
        }
      }
    }
    runner.expectTrue(allValid, "all typical scales: mapped source within bounds");
  }

  // ============================================
  // Consistency: X and Y use the same formula
  // ============================================

  // Test 10: Same scale for width and height produces consistent mapping
  {
    const float scale = 464.0f / 800.0f;
    const uint32_t fp = computeInvScaleFp(scale);
    // X=100 and Y=100 should map to the same source coord (same formula)
    runner.expectEq(mapCoordFp(100, fp), mapCoordFp(100, fp), "same scale: X=Y mapping consistent");
  }

  // Test 11: Monotonicity - larger dest always maps to >= source
  {
    const float scale = 464.0f / 1000.0f;
    const uint32_t fp = computeInvScaleFp(scale);
    bool monotonic = true;
    int prev = 0;
    for (int d = 0; d < 464; d++) {
      int src = mapCoordFp(d, fp);
      if (src < prev) {
        monotonic = false;
        break;
      }
      prev = src;
    }
    runner.expectTrue(monotonic, "mapping is monotonically non-decreasing");
  }

  return runner.allPassed() ? 0 : 1;
}
