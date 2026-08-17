#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace papyrix::page_metrics {

constexpr size_t kEstimatedBytesPerPage = 2048;

struct Section {
  uint32_t pages = 0;
  bool exact = false;
  uint32_t byteSize = 0;
};

struct Display {
  int currentPage = 1;
  int totalPages = 0;
  bool totalIsExact = true;
};

struct Update {
  bool changed = false;
  bool becameExact = false;
};

uint32_t estimatePagesForBytes(size_t bytes, size_t bytesPerPage = kEstimatedBytesPerPage);
Update applyCache(Section& metric, uint32_t pageCount, bool partial);
void fillEstimates(std::vector<Section>& metrics, size_t bytesPerPage = kEstimatedBytesPerPage);
void recalibrate(std::vector<Section>& metrics);
uint32_t total(const std::vector<Section>& metrics);
Display resolve(const std::vector<Section>& metrics, int currentSpineIndex, int currentSectionPage,
                int textStartIndex = 0);

}  // namespace papyrix::page_metrics
