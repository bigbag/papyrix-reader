#pragma once

#include <cstddef>
#include <cstdint>

inline constexpr uintptr_t mockTaskStackBase = 0x10000;
inline size_t mockTaskFreeStackBytes = 4096;

inline void* esp_cpu_get_sp() {
  return reinterpret_cast<void*>(mockTaskStackBase + mockTaskFreeStackBytes);
}
