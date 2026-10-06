#pragma once

#include <cstdint>

namespace papyrix::sd {

// SD logical sector size in bytes. Fixed by the SD specification for flash
// media; avoids FatPartition::bytesPerCluster(), which reports cluster bytes
// in a uint16_t that wraps to zero at 64 KiB clusters.
inline constexpr uint32_t kBytesPerSector = 512;

// Production free-space computation shared with SDCardManager::freeBytes().
// The volume reports -1 clusters on error; SdFat's uint32_t wrapper turns
// that into UINT32_MAX. No card holds 2^31 clusters.
inline uint64_t freeBytesFromClusters(uint64_t freeClusters, uint32_t sectorsPerCluster) {
  if (freeClusters == 0 || freeClusters >= static_cast<uint64_t>(INT32_MAX)) return 0;
  return freeClusters * sectorsPerCluster * kBytesPerSector;
}

}  // namespace papyrix::sd
