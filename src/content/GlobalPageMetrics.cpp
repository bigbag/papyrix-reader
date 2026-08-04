#include "GlobalPageMetrics.h"

#include <algorithm>
#include <climits>
#include <numeric>

namespace papyrix::page_metrics {

uint16_t estimatePagesForBytes(size_t bytes, size_t bytesPerPage) {
  const size_t safeBytesPerPage = std::max<size_t>(1, bytesPerPage);
  const size_t quotient = bytes / safeBytesPerPage;
  const size_t pageCount = std::max<size_t>(1, quotient + (bytes % safeBytesPerPage != 0));
  return static_cast<uint16_t>(std::min<size_t>(pageCount, UINT16_MAX));
}

Update applyCache(Section& metric, uint16_t pageCount, bool partial) {
  Update result;
  if (!partial) {
    if (!metric.exact || metric.pages != pageCount) {
      result.changed = true;
      result.becameExact = !metric.exact;
      metric.pages = pageCount;
      metric.exact = true;
    }
  } else if (!metric.exact && pageCount > metric.pages) {
    metric.pages = pageCount;
    result.changed = true;
  }
  return result;
}

void fillEstimates(std::vector<Section>& metrics, size_t bytesPerPage) {
  for (auto& metric : metrics) {
    if (metric.exact) continue;
    const uint16_t estimated = estimatePagesForBytes(metric.byteSize, bytesPerPage);
    if (metric.pages == 0 || estimated > metric.pages) metric.pages = estimated;
  }
}

void recalibrate(std::vector<Section>& metrics) {
  uint64_t calibrationBytes = 0;
  uint32_t calibrationPages = 0;
  for (const auto& metric : metrics) {
    if (metric.exact && metric.byteSize > 0 && metric.pages > 0) {
      calibrationBytes += metric.byteSize;
      calibrationPages += metric.pages;
    }
  }
  if (calibrationPages == 0) return;

  const size_t bytesPerPage = static_cast<size_t>(std::max<uint64_t>(256, calibrationBytes / calibrationPages));
  for (auto& metric : metrics) {
    if (metric.exact || metric.byteSize == 0) continue;
    metric.pages = estimatePagesForBytes(metric.byteSize, bytesPerPage);
  }
}

uint32_t total(const std::vector<Section>& metrics) {
  return std::accumulate(metrics.begin(), metrics.end(), uint32_t{0},
                         [](uint32_t sum, const Section& metric) { return sum + metric.pages; });
}

Display resolve(const std::vector<Section>& metrics, int currentSpineIndex, int currentSectionPage,
                int textStartIndex) {
  Display result;
  if (metrics.empty()) return result;

  const int clampedSpine = std::clamp(currentSpineIndex, 0, static_cast<int>(metrics.size()) - 1);
  uint32_t pagesBefore = 0;
  uint32_t frontMatterPages = 0;
  bool totalIsExact = true;
  for (int i = 0; i < clampedSpine; ++i) {
    pagesBefore += metrics[static_cast<size_t>(i)].pages;
    totalIsExact = totalIsExact && metrics[static_cast<size_t>(i)].exact;
  }
  for (int i = 0; i < textStartIndex && i < static_cast<int>(metrics.size()); ++i) {
    frontMatterPages += metrics[static_cast<size_t>(i)].pages;
  }
  for (int i = clampedSpine; i < static_cast<int>(metrics.size()); ++i) {
    totalIsExact = totalIsExact && metrics[static_cast<size_t>(i)].exact;
  }

  const int adjustedBefore = std::max(static_cast<int>(pagesBefore) - static_cast<int>(frontMatterPages), 0);
  const int adjustedTotal = std::max(static_cast<int>(total(metrics)) - static_cast<int>(frontMatterPages), 1);
  result.currentPage = adjustedBefore + std::max(currentSectionPage, 0) + 1;
  result.totalPages = std::max(adjustedTotal, result.currentPage);
  result.totalIsExact = totalIsExact;
  return result;
}

}  // namespace papyrix::page_metrics
