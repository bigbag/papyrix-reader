#pragma once

#include <cstdint>

namespace papyrix {

// Minimum SD free space to open a book. Below this, the reader refuses the
// book and shows a warning instead of stalling on cache writes.
inline constexpr uint64_t kMinOpenBookFreeBytes = 20ull * 1024 * 1024;

// Pure decision: open a book only when free space covers the page cache.
inline bool openBookAllowed(uint64_t freeBytes) { return freeBytes >= kMinOpenBookFreeBytes; }

}  // namespace papyrix
