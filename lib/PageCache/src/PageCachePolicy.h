#pragma once

#include <cstdint>

namespace page_cache {

inline constexpr uint16_t EXTEND_THRESHOLD = 3;

constexpr bool needsExtension(uint16_t pageCount, bool partial, uint16_t currentPage) {
  return partial && static_cast<uint32_t>(currentPage) + EXTEND_THRESHOLD >= pageCount;
}

constexpr bool hotExtendShouldCommit(bool parseSucceeded, bool madeProgress) { return parseSucceeded || madeProgress; }

constexpr bool hotExtendIsPartial(bool parseSucceeded, bool parserHasMoreContent) {
  return !parseSucceeded || parserHasMoreContent;
}

}  // namespace page_cache
