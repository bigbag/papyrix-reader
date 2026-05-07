// Tests for the global page metrics logic extracted from ReaderState.
// These functions compute whole-book page numbers for EPUB/FB2 by combining
// exact counts (from cached sections) with byte-size estimates (for uncached).

#include "test_utils.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <vector>

namespace {

constexpr size_t kEstimatedBytesPerPage = 2048;

uint16_t estimatePagesForBytes(const size_t bytes, const size_t bytesPerPage = kEstimatedBytesPerPage) {
  const size_t safeBytesPerPage = std::max<size_t>(1, bytesPerPage);
  const size_t pageCount = std::max<size_t>(1, (bytes + safeBytesPerPage - 1) / safeBytesPerPage);
  return static_cast<uint16_t>(std::min<size_t>(pageCount, UINT16_MAX));
}

struct SectionPageMetric {
  uint16_t pages = 0;
  bool exact = false;
  uint32_t byteSize = 0;
};

uint32_t recomputeTotal(const std::vector<SectionPageMetric>& metrics) {
  uint32_t total = 0;
  for (const auto& m : metrics) {
    total += m.pages;
  }
  return total;
}

void recalibrate(std::vector<SectionPageMetric>& metrics) {
  size_t calibBytes = 0;
  uint32_t calibPages = 0;
  for (const auto& m : metrics) {
    if (m.exact && m.byteSize > 0 && m.pages > 0) {
      calibBytes += m.byteSize;
      calibPages += m.pages;
    }
  }
  if (calibPages == 0) return;

  const size_t bytesPerPage = std::max<size_t>(256, calibBytes / calibPages);

  for (auto& m : metrics) {
    if (m.exact || m.byteSize == 0) continue;
    const uint16_t newEstimate = estimatePagesForBytes(m.byteSize, bytesPerPage);
    if (newEstimate != m.pages && newEstimate > 0) {
      m.pages = newEstimate;
    }
  }
}

struct GlobalPageMetrics {
  int currentPage = 1;
  int totalPages = 0;
  bool totalIsExact = true;
};

GlobalPageMetrics resolveMetrics(const std::vector<SectionPageMetric>& metrics, int currentSpineIndex,
                                 int currentSectionPage) {
  GlobalPageMetrics result;
  if (metrics.empty()) return result;

  const int clampedSpine = std::clamp(currentSpineIndex, 0, static_cast<int>(metrics.size()) - 1);
  uint32_t pagesBefore = 0;
  bool totalIsExact = true;
  for (int i = 0; i < clampedSpine; ++i) {
    pagesBefore += metrics[static_cast<size_t>(i)].pages;
    totalIsExact = totalIsExact && metrics[static_cast<size_t>(i)].exact;
  }
  for (int i = clampedSpine; i < static_cast<int>(metrics.size()); ++i) {
    totalIsExact = totalIsExact && metrics[static_cast<size_t>(i)].exact;
  }

  uint32_t total = recomputeTotal(metrics);
  result.currentPage = static_cast<int>(pagesBefore) + std::max(currentSectionPage, 0) + 1;
  result.totalPages = static_cast<int>(std::max<uint32_t>(total, result.currentPage));
  result.totalIsExact = totalIsExact;
  return result;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("GlobalPageMetrics");

  // ============================================
  // estimatePagesForBytes
  // ============================================

  runner.expectEq<uint16_t>(1, estimatePagesForBytes(0), "zero_bytes_yields_one_page");
  runner.expectEq<uint16_t>(1, estimatePagesForBytes(1), "one_byte_yields_one_page");
  runner.expectEq<uint16_t>(1, estimatePagesForBytes(2048), "exact_one_page");
  runner.expectEq<uint16_t>(2, estimatePagesForBytes(2049), "one_byte_over_yields_two");
  runner.expectEq<uint16_t>(5, estimatePagesForBytes(10240), "exact_five_pages");
  runner.expectEq<uint16_t>(5, estimatePagesForBytes(10000), "rounded_up_to_five");

  // Custom bytesPerPage
  runner.expectEq<uint16_t>(10, estimatePagesForBytes(10000, 1000), "custom_bpp_1000");
  runner.expectEq<uint16_t>(1, estimatePagesForBytes(500, 1000), "custom_bpp_under_one_page");
  runner.expectEq<uint16_t>(2, estimatePagesForBytes(1001, 1000), "custom_bpp_just_over_one");

  // bytesPerPage=0 is guarded (treated as 1)
  runner.expectEq<uint16_t>(100, estimatePagesForBytes(100, 0), "bpp_zero_guarded");

  // Large values don't overflow uint16_t
  runner.expectEq<uint16_t>(65535, estimatePagesForBytes(200000000, 1), "large_bytes_clamped_to_uint16_max");

  // ============================================
  // recomputeTotal
  // ============================================

  {
    std::vector<SectionPageMetric> metrics = {{10, true, 5000}, {20, true, 8000}, {30, false, 12000}};
    runner.expectEq<uint32_t>(60, recomputeTotal(metrics), "total_three_sections");
  }
  {
    std::vector<SectionPageMetric> metrics;
    runner.expectEq<uint32_t>(0, recomputeTotal(metrics), "total_empty");
  }
  {
    std::vector<SectionPageMetric> metrics = {{5, true, 1000}};
    runner.expectEq<uint32_t>(5, recomputeTotal(metrics), "total_single_section");
  }

  // ============================================
  // recalibrate
  // ============================================

  // Two exact sections calibrate the estimated third
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 10000},  // 1000 bytes/page
        {20, true, 20000},  // 1000 bytes/page
        {0, false, 5000},   // Should become 5 pages at 1000 bpp
    };
    recalibrate(metrics);
    runner.expectEq<uint16_t>(5, metrics[2].pages, "recalibrate_estimated_section");
    runner.expectEq<uint16_t>(10, metrics[0].pages, "recalibrate_exact_unchanged_0");
    runner.expectEq<uint16_t>(20, metrics[1].pages, "recalibrate_exact_unchanged_1");
  }

  // No exact sections: no recalibration
  {
    std::vector<SectionPageMetric> metrics = {
        {3, false, 5000},
        {4, false, 8000},
    };
    recalibrate(metrics);
    runner.expectEq<uint16_t>(3, metrics[0].pages, "no_exact_no_change_0");
    runner.expectEq<uint16_t>(4, metrics[1].pages, "no_exact_no_change_1");
  }

  // Exact section with byteSize=0 doesn't contribute to calibration
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 0},      // No byte info
        {0, false, 10000},  // Should stay unchanged (no calibration data)
    };
    recalibrate(metrics);
    runner.expectEq<uint16_t>(0, metrics[1].pages, "exact_no_bytes_no_calibration");
  }

  // Mixed: one exact section calibrates multiple estimated
  {
    std::vector<SectionPageMetric> metrics = {
        {20, true, 40000},  // 2000 bytes/page
        {0, false, 10000},  // Should become 5 pages
        {0, false, 20000},  // Should become 10 pages
        {0, false, 4000},   // Should become 2 pages
    };
    recalibrate(metrics);
    runner.expectEq<uint16_t>(5, metrics[1].pages, "mixed_calibrate_section_1");
    runner.expectEq<uint16_t>(10, metrics[2].pages, "mixed_calibrate_section_2");
    runner.expectEq<uint16_t>(2, metrics[3].pages, "mixed_calibrate_section_3");
  }

  // Calibration floor: bytesPerPage is at least 256
  {
    std::vector<SectionPageMetric> metrics = {
        {1000, true, 100},  // 0.1 bytes/page → clamped to 256 bpp
        {0, false, 512},    // At 256 bpp → 2 pages
    };
    recalibrate(metrics);
    runner.expectEq<uint16_t>(2, metrics[1].pages, "bpp_floor_256");
  }

  // ============================================
  // resolveMetrics
  // ============================================

  // Three exact sections, on page 5 of section 1 (0-indexed)
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, true, 8000},
        {15, true, 6000},
    };
    auto gm = resolveMetrics(metrics, 1, 5);
    runner.expectEq(16, gm.currentPage, "resolve_current_page");  // 10 + 5 + 1
    runner.expectEq(45, gm.totalPages, "resolve_total_pages");     // 10 + 20 + 15
    runner.expectTrue(gm.totalIsExact, "resolve_all_exact");
  }

  // First section, first page
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, true, 8000},
    };
    auto gm = resolveMetrics(metrics, 0, 0);
    runner.expectEq(1, gm.currentPage, "resolve_first_page");
    runner.expectEq(30, gm.totalPages, "resolve_total_first");
    runner.expectTrue(gm.totalIsExact, "resolve_first_exact");
  }

  // Last section, last page
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, true, 8000},
        {5, true, 2000},
    };
    auto gm = resolveMetrics(metrics, 2, 4);
    runner.expectEq(35, gm.currentPage, "resolve_last_page");  // 10 + 20 + 4 + 1
    runner.expectEq(35, gm.totalPages, "resolve_total_last");
  }

  // Mixed exact/estimated: totalIsExact should be false
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, false, 8000},  // estimated
        {15, true, 6000},
    };
    auto gm = resolveMetrics(metrics, 0, 0);
    runner.expectFalse(gm.totalIsExact, "resolve_mixed_not_exact");
  }

  // All estimated
  {
    std::vector<SectionPageMetric> metrics = {
        {10, false, 5000},
        {20, false, 8000},
    };
    auto gm = resolveMetrics(metrics, 0, 0);
    runner.expectFalse(gm.totalIsExact, "resolve_all_estimated_not_exact");
    runner.expectEq(30, gm.totalPages, "resolve_all_estimated_total");
  }

  // Current page exceeds computed total → total is clamped up
  {
    std::vector<SectionPageMetric> metrics = {
        {5, true, 2000},
        {3, true, 1000},
    };
    // Section 1, page 5 → currentPage = 5 + 5 + 1 = 11, but total = 8
    auto gm = resolveMetrics(metrics, 1, 5);
    runner.expectEq(11, gm.currentPage, "resolve_overflow_current");
    runner.expectEq(11, gm.totalPages, "resolve_overflow_total_clamped_up");
  }

  // Empty metrics
  {
    std::vector<SectionPageMetric> metrics;
    auto gm = resolveMetrics(metrics, 0, 0);
    runner.expectEq(1, gm.currentPage, "resolve_empty_current");
    runner.expectEq(0, gm.totalPages, "resolve_empty_total");
  }

  // Single section
  {
    std::vector<SectionPageMetric> metrics = {{50, true, 20000}};
    auto gm = resolveMetrics(metrics, 0, 25);
    runner.expectEq(26, gm.currentPage, "resolve_single_current");
    runner.expectEq(50, gm.totalPages, "resolve_single_total");
    runner.expectTrue(gm.totalIsExact, "resolve_single_exact");
  }

  // Spine index clamped when out of range
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, true, 8000},
    };
    auto gm = resolveMetrics(metrics, 5, 0);  // spine 5, but only 2 sections
    runner.expectEq(11, gm.currentPage, "resolve_clamped_spine_current");  // 10 + 0 + 1
    runner.expectEq(30, gm.totalPages, "resolve_clamped_spine_total");
  }

  // Negative currentSectionPage clamped to 0
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, true, 8000},
    };
    auto gm = resolveMetrics(metrics, 1, -3);
    runner.expectEq(11, gm.currentPage, "resolve_negative_section_page");  // 10 + max(-3,0) + 1
  }

  return runner.allPassed() ? 0 : 1;
}
