#pragma once

#include <cstdint>

namespace page_cache {

inline constexpr uint16_t EXTEND_THRESHOLD = 3;
inline constexpr uint32_t MAX_PROACTIVE_COLD_PAGES = 1000;

constexpr bool needsExtension(uint32_t pageCount, bool partial, uint32_t currentPage) {
  return partial && (currentPage >= pageCount || pageCount - currentPage <= EXTEND_THRESHOLD);
}

constexpr bool hotExtendShouldCommit(bool parseSucceeded, bool madeProgress) { return parseSucceeded || madeProgress; }

constexpr bool hotExtendIsPartial(bool parseSucceeded, bool parserHasMoreContent) {
  return !parseSucceeded || parserHasMoreContent;
}

enum class FullIndexCacheAction : uint8_t {
  Create,
  Extend,
  Skip,
};

constexpr FullIndexCacheAction fullIndexCacheAction(bool loaded, bool partial) {
  return loaded ? (partial ? FullIndexCacheAction::Extend : FullIndexCacheAction::Skip) : FullIndexCacheAction::Create;
}

constexpr uint16_t extensionChunk(uint32_t pageCount, uint16_t requestedPages) {
  const uint16_t desired = pageCount >= 30 ? 50 : requestedPages;
  const uint64_t remaining = static_cast<uint64_t>(UINT32_MAX) - pageCount;
  return static_cast<uint16_t>(desired < remaining ? desired : remaining);
}

constexpr bool proactiveExtensionAllowed(bool parserCanResume, uint32_t pageCount) {
  return parserCanResume || pageCount < MAX_PROACTIVE_COLD_PAGES;
}

constexpr bool backgroundShouldExtend(bool cacheLoaded, bool cachePartial, bool parserCanResume, uint32_t pageCount,
                                      uint32_t currentPage) {
  return cacheLoaded && cachePartial && (parserCanResume || needsExtension(pageCount, cachePartial, currentPage));
}

constexpr bool backgroundWorkPending(bool cacheLoaded, bool cachePartial, bool thumbnailDone, bool coverDone,
                                     bool parserCanResume, uint32_t pageCount, uint32_t currentPage,
                                     bool cacheRequired = true) {
  if (!thumbnailDone || !coverDone) return true;
  if (!cacheRequired) return false;
  return !cacheLoaded || backgroundShouldExtend(cacheLoaded, cachePartial, parserCanResume, pageCount, currentPage);
}

constexpr bool lutFitsFile(uint32_t pageCount, uint64_t availableBytes) {
  return static_cast<uint64_t>(pageCount) * sizeof(uint32_t) <= availableBytes;
}

}  // namespace page_cache
