// Regression tests for the 64 KiB-cluster defect (issue #171 follow-up):
// FatPartition::bytesPerCluster() is uint16_t and wraps to zero at 128
// sectors per cluster, which reported 0 free bytes on cards with gigabytes
// free. SDCardManager::freeBytes() must use this shared sector-based
// computation instead of the truncating accessor.

#include "test_utils.h"

#include <SdFreeSpace.h>

#include <cstdint>

int main() {
  using papyrix::sd::freeBytesFromClusters;
  TestUtils::TestRunner runner("SdFreeSpaceTest");

  // 128 sectors per cluster (64 KiB clusters): 16384 clusters hold 1 GiB.
  // The old accessor returned 0 bytes here.
  {
    runner.expectEq<uint64_t>(1073741824ull, freeBytesFromClusters(16384, 128), "64kib-clusters-1gib");
  }

  // Results above 4 GiB need 64-bit arithmetic end to end.
  {
    runner.expectEq<uint64_t>(8589934592ull, freeBytesFromClusters(131072, 128), "64kib-clusters-8gib");
  }

  // Ordinary 32 KiB clusters keep working.
  {
    runner.expectEq<uint64_t>(32768000ull, freeBytesFromClusters(1000, 64), "32kib-clusters");
  }

  // Error sentinels map to zero: none, and the volume -1 wrapped to UINT32_MAX.
  {
    runner.expectEq<uint64_t>(0ull, freeBytesFromClusters(0, 128), "zero-clusters");
    runner.expectEq<uint64_t>(0ull, freeBytesFromClusters(UINT32_MAX, 128), "wrapped-error-sentinel");
    runner.expectEq<uint64_t>(0ull, freeBytesFromClusters(INT32_MAX, 128), "int32-sentinel");
  }

  return runner.allPassed() ? 0 : 1;
}
