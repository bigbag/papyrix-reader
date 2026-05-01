#include "ReaderNavigation.h"

#include <PageCache.h>

#include <cstdint>

namespace papyrix {

ReaderNavigation::NavResult ReaderNavigation::next(ContentType type, const Position& current, const PageCache* cache,
                                                   uint32_t totalPages) {
  NavResult result;
  result.position = current;
  result.needsRender = false;
  result.needsCacheReset = false;

  const int pageCount = cache ? cache->pageCount() : 0;

  if (type == ContentType::Xtc) {
    if (current.flatPage + 1 < totalPages) {
      result.position.flatPage = current.flatPage + 1;
      result.needsRender = true;
    }
  } else if (type == ContentType::Epub || type == ContentType::Fb2) {
    if (pageCount > 0 && current.sectionPage < pageCount - 1) {
      result.position.sectionPage = current.sectionPage + 1;
      result.needsRender = true;
    } else if (cache && cache->isPartial()) {
      // Cache is partial - increment page to trigger cache extension
      result.position.sectionPage = current.sectionPage + 1;
      result.needsRender = true;
    } else if (pageCount > 0 && current.sectionPage >= pageCount - 1) {
      // Cache is complete - move to next chapter/section
      result.position.spineIndex = current.spineIndex + 1;
      result.position.sectionPage = 0;
      result.needsCacheReset = true;
      result.needsRender = true;
    }
  } else {
    if (pageCount > 0 && current.sectionPage < pageCount - 1) {
      result.position.sectionPage = current.sectionPage + 1;
      result.needsRender = true;
    } else if (cache && cache->isPartial()) {
      result.position.sectionPage = current.sectionPage + 1;
      result.needsRender = true;
    }
  }

  return result;
}

ReaderNavigation::NavResult ReaderNavigation::prev(ContentType type, const Position& current, const PageCache* cache) {
  NavResult result;
  result.position = current;
  result.needsRender = false;
  result.needsCacheReset = false;

  if (type == ContentType::Xtc) {
    if (current.flatPage > 0) {
      result.position.flatPage = current.flatPage - 1;
      result.needsRender = true;
    }
  } else if (type == ContentType::Epub || type == ContentType::Fb2) {
    if (current.sectionPage > 0) {
      result.position.sectionPage = current.sectionPage - 1;
      result.needsRender = true;
    } else if (current.spineIndex > 0) {
      result.position.spineIndex = current.spineIndex - 1;
      result.position.sectionPage = INT16_MAX;  // Will be clamped to last page
      result.needsCacheReset = true;
      result.needsRender = true;
    }
  } else {
    if (current.sectionPage > 0) {
      result.position.sectionPage = current.sectionPage - 1;
      result.needsRender = true;
    }
  }

  return result;
}

bool ReaderNavigation::needsCacheExtension(const PageCache* cache, int sectionPage) {
  if (!cache) return false;
  return cache->needsExtension(sectionPage);
}

}  // namespace papyrix
