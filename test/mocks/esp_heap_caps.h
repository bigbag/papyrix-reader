// Mock esp_heap_caps.h for desktop testing
// Delegates to platform_stubs.h to avoid redefinition
#pragma once

#include "platform_stubs.h"

#include <cstdlib>

inline void* heap_caps_malloc(size_t size, uint32_t caps) {
  if ((caps & MALLOC_CAP_SPIRAM) && testPsramAllocationFailure()) return nullptr;
  if (size > heap_caps_get_free_size(caps) || size > heap_caps_get_largest_free_block(caps)) return nullptr;
  return std::malloc(size);
}
inline size_t& testHeapCapsFreeCount() {
  static size_t count = 0;
  return count;
}
inline void heap_caps_free(void* ptr) {
  if (ptr) ++testHeapCapsFreeCount();
  std::free(ptr);
}
