#pragma once

#include <cstdint>

class PageCache {
 public:
  PageCache(uint16_t pageCount, bool partial) : pageCount_(pageCount), partial_(partial) {}

  uint16_t pageCount() const { return pageCount_; }
  bool isPartial() const { return partial_; }
  bool needsExtension(uint16_t) const { return partial_; }

  void setPageCount(uint16_t count) { pageCount_ = count; }
  void setPartial(bool partial) { partial_ = partial; }

 private:
  uint16_t pageCount_;
  bool partial_;
};
