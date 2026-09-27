#pragma once

#include <esp_heap_caps.h>
#if PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC
#include <TargetConfig.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>

class ParserScratch {
 public:
  ParserScratch(uint8_t* borrowed, size_t capacity) : borrowed_(borrowed), data_(borrowed), capacity_(capacity) {
#if PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC
    const size_t requested = std::max(capacity, size_t{papyrix::board::kTargetParserScratchBytes});
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    if (requested <= heap_caps_get_largest_free_block(caps) * 80 / 100) {
      if (auto* owned = static_cast<uint8_t*>(heap_caps_malloc(requested, caps))) {
        data_ = owned;
        capacity_ = requested;
      }
    }
#endif
  }
  ~ParserScratch() {
#if PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC
    if (data_ != borrowed_) heap_caps_free(data_);
#endif
  }
  ParserScratch(const ParserScratch&) = delete;
  ParserScratch& operator=(const ParserScratch&) = delete;

  uint8_t* data() const { return data_; }
  size_t capacity() const { return capacity_; }

 private:
  uint8_t* borrowed_;
  uint8_t* data_;
  size_t capacity_;
};
