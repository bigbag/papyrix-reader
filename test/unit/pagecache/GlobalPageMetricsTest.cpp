#include "test_utils.h"

#include <GlobalPageMetrics.h>

#include <cstdint>
#include <vector>

using papyrix::page_metrics::Display;
using papyrix::page_metrics::Section;
using papyrix::page_metrics::applyCache;
using papyrix::page_metrics::estimatePagesForBytes;
using papyrix::page_metrics::fillEstimates;
using papyrix::page_metrics::recalibrate;
using papyrix::page_metrics::resolve;
using papyrix::page_metrics::total;

using GlobalPageMetrics = Display;
using SectionPageMetric = Section;

uint32_t recomputeTotal(const std::vector<SectionPageMetric>& metrics) { return total(metrics); }

void applyProbe(SectionPageMetric& metric, uint32_t pageCount, bool partial) {
  applyCache(metric, pageCount, partial);
}

GlobalPageMetrics resolveMetrics(const std::vector<SectionPageMetric>& metrics, int currentSpineIndex,
                                 int currentSectionPage, int textStartIndex = 0) {
  return resolve(metrics, currentSpineIndex, currentSectionPage, textStartIndex);
}

int main() {
  TestUtils::TestRunner runner("GlobalPageMetrics");

  // ============================================
  // estimatePagesForBytes
  // ============================================

  runner.expectEq<uint32_t>(1, estimatePagesForBytes(0), "zero_bytes_yields_one_page");
  runner.expectEq<uint32_t>(1, estimatePagesForBytes(1), "one_byte_yields_one_page");
  runner.expectEq<uint32_t>(1, estimatePagesForBytes(2048), "exact_one_page");
  runner.expectEq<uint32_t>(2, estimatePagesForBytes(2049), "one_byte_over_yields_two");
  runner.expectEq<uint32_t>(5, estimatePagesForBytes(10240), "exact_five_pages");
  runner.expectEq<uint32_t>(5, estimatePagesForBytes(10000), "rounded_up_to_five");

  // Custom bytesPerPage
  runner.expectEq<uint32_t>(10, estimatePagesForBytes(10000, 1000), "custom_bpp_1000");
  runner.expectEq<uint32_t>(1, estimatePagesForBytes(500, 1000), "custom_bpp_under_one_page");
  runner.expectEq<uint32_t>(2, estimatePagesForBytes(1001, 1000), "custom_bpp_just_over_one");

  // bytesPerPage=0 is guarded (treated as 1)
  runner.expectEq<uint32_t>(100, estimatePagesForBytes(100, 0), "bpp_zero_guarded");

  // Large values remain exact beyond the legacy uint16_t ceiling
  runner.expectEq<uint32_t>(200000000, estimatePagesForBytes(200000000, 1),
                            "large_bytes_preserve_uint32_page_count");

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
    runner.expectEq<uint32_t>(5, metrics[2].pages, "recalibrate_estimated_section");
    runner.expectEq<uint32_t>(10, metrics[0].pages, "recalibrate_exact_unchanged_0");
    runner.expectEq<uint32_t>(20, metrics[1].pages, "recalibrate_exact_unchanged_1");
  }

  // No exact sections: no recalibration
  {
    std::vector<SectionPageMetric> metrics = {
        {3, false, 5000},
        {4, false, 8000},
    };
    recalibrate(metrics);
    runner.expectEq<uint32_t>(3, metrics[0].pages, "no_exact_no_change_0");
    runner.expectEq<uint32_t>(4, metrics[1].pages, "no_exact_no_change_1");
  }

  // Exact section with byteSize=0 doesn't contribute to calibration
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 0},      // No byte info
        {0, false, 10000},  // Should stay unchanged (no calibration data)
    };
    recalibrate(metrics);
    runner.expectEq<uint32_t>(0, metrics[1].pages, "exact_no_bytes_no_calibration");
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
    runner.expectEq<uint32_t>(5, metrics[1].pages, "mixed_calibrate_section_1");
    runner.expectEq<uint32_t>(10, metrics[2].pages, "mixed_calibrate_section_2");
    runner.expectEq<uint32_t>(2, metrics[3].pages, "mixed_calibrate_section_3");
  }

  // Calibration floor: bytesPerPage is at least 256
  {
    std::vector<SectionPageMetric> metrics = {
        {1000, true, 100},  // 0.1 bytes/page → clamped to 256 bpp
        {0, false, 512},    // At 256 bpp → 2 pages
    };
    recalibrate(metrics);
    runner.expectEq<uint32_t>(2, metrics[1].pages, "bpp_floor_256");
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

  // ============================================
  // Front-matter subtraction (textStartIndex)
  // ============================================

  // Basic front-matter: 2 front-matter sections, reading at section 2
  {
    std::vector<SectionPageMetric> metrics = {
        {3, true, 1000},   // cover
        {5, true, 2000},   // dedication
        {20, true, 8000},  // chapter 1
        {15, true, 6000},  // chapter 2
    };
    auto gm = resolveMetrics(metrics, 2, 0, 2);
    // pagesBefore=3+5=8, frontMatter=3+5=8, adjusted=0, current=0+0+1=1
    runner.expectEq(1, gm.currentPage, "frontmatter_at_text_start_current");
    // total=43, adjustedTotal=43-8=35
    runner.expectEq(35, gm.totalPages, "frontmatter_at_text_start_total");
  }

  // Reading in the middle of text with front-matter offset
  {
    std::vector<SectionPageMetric> metrics = {
        {3, true, 1000},   // cover
        {5, true, 2000},   // dedication
        {20, true, 8000},  // chapter 1
        {15, true, 6000},  // chapter 2
    };
    auto gm = resolveMetrics(metrics, 3, 10, 2);
    // pagesBefore=3+5+20=28, frontMatter=3+5=8, adjusted=20, current=20+10+1=31
    runner.expectEq(31, gm.currentPage, "frontmatter_mid_text_current");
    // total=43, adjustedTotal=43-8=35
    runner.expectEq(35, gm.totalPages, "frontmatter_mid_text_total");
  }

  // Reading in front-matter (spine < textStart): no underflow
  {
    std::vector<SectionPageMetric> metrics = {
        {3, true, 1000},   // cover
        {5, true, 2000},   // dedication
        {20, true, 8000},  // chapter 1
        {15, true, 6000},  // chapter 2
    };
    auto gm = resolveMetrics(metrics, 0, 1, 2);
    // pagesBefore=0, frontMatter=3+5=8, adjusted=max(0-8,0)=0, current=0+1+1=2
    runner.expectEq(2, gm.currentPage, "frontmatter_in_frontmatter_current");
    // total=43, adjustedTotal=43-8=35, max(35,2)=35
    runner.expectEq(35, gm.totalPages, "frontmatter_in_frontmatter_total");
  }

  // Front-matter at spine 1 of 2 (clampedSpine=1 < textStart=2, but only 2 sections)
  {
    std::vector<SectionPageMetric> metrics = {
        {3, true, 1000},
        {5, true, 2000},
    };
    auto gm = resolveMetrics(metrics, 1, 2, 2);
    // pagesBefore=3, frontMatter=3+5=8, adjusted=max(3-8,0)=0, current=0+2+1=3
    runner.expectEq(3, gm.currentPage, "frontmatter_all_front_current");
    // total=8, adjustedTotal=max(8-8,1)=1, max(1,3)=3
    runner.expectEq(3, gm.totalPages, "frontmatter_all_front_total");
  }

  // textStartIndex=0 means no front-matter (default)
  {
    std::vector<SectionPageMetric> metrics = {
        {10, true, 5000},
        {20, true, 8000},
    };
    auto gm = resolveMetrics(metrics, 1, 5, 0);
    runner.expectEq(16, gm.currentPage, "no_frontmatter_current");  // 10+5+1
    runner.expectEq(30, gm.totalPages, "no_frontmatter_total");
  }

  // ============================================
  // Exact 0-page sections (Issue #136 residual)
  // Image-only FB2 sections produce complete caches with pageCount==0.
  // They must count as exact so Full Book Process clears the status-bar "~".
  // ============================================

  // Probe of a complete empty section → exact with pages=0
  {
    SectionPageMetric m{0, false, 280};
    const auto update = applyCache(m, 0, false);
    runner.expectTrue(update.changed, "zero_page_probe_changed");
    runner.expectTrue(update.becameExact, "zero_page_probe_became_exact");
    runner.expectTrue(m.exact, "zero_page_probe_exact");
    runner.expectEq<uint32_t>(0, m.pages, "zero_page_probe_pages");
  }

  // A complete cache replaces a prior overestimate with its lower exact count.
  {
    SectionPageMetric m{7, false, 10000};
    applyProbe(m, 5, false);
    runner.expectTrue(m.exact, "complete_probe_replaces_estimate_exact");
    runner.expectEq<uint32_t>(5, m.pages, "complete_probe_lowers_estimate");
  }

  // Estimate fill must NOT overwrite exact 0 with a byte-size estimate of 1
  {
    std::vector<SectionPageMetric> metrics = {
        {0, true, 280},     // exact empty (image-only)
        {0, false, 10000},  // uncached → estimate
        {20, true, 40000},
    };
    fillEstimates(metrics, 2000);
    runner.expectEq<uint32_t>(0, metrics[0].pages, "zero_page_exact_not_overwritten");
    runner.expectTrue(metrics[0].exact, "zero_page_exact_flag_kept");
    runner.expectEq<uint32_t>(5, metrics[1].pages, "uncached_still_estimated");
    runner.expectFalse(metrics[1].exact, "uncached_not_exact");
  }

  // Old bug: treating pages==0 as "missing" left exact=false + estimate=1 → permanent "~"
  // even when every real section was exact and the displayed total looked right.
  {
    std::vector<SectionPageMetric> buggy = {
        {1, false, 280},  // phantom estimate for empty section
        {10, true, 5000},
        {20, true, 8000},
    };
    auto gmBuggy = resolveMetrics(buggy, 2, 19);
    runner.expectFalse(gmBuggy.totalIsExact, "phantom_empty_keeps_tilde");
    runner.expectEq(31, gmBuggy.totalPages, "phantom_empty_inflates_total");  // 1+10+20

    std::vector<SectionPageMetric> fixed = {
        {0, true, 280},  // exact empty
        {10, true, 5000},
        {20, true, 8000},
    };
    auto gmFixed = resolveMetrics(fixed, 2, 19);
    runner.expectTrue(gmFixed.totalIsExact, "exact_empty_clears_tilde");
    runner.expectEq(30, gmFixed.totalPages, "exact_empty_no_phantom_page");  // 0+10+20
    runner.expectEq(30, gmFixed.currentPage, "exact_empty_last_page");      // 0+10+19+1
  }

  return runner.allPassed() ? 0 : 1;
}
