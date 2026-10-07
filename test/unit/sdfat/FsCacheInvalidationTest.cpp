// Regression for the vendored SdFat cache-invalidation patch: a failed
// sector read must not leave new bytes cached under an old sector number.
// Compiles the real lib/SdFat FsCache.cpp against a fake block device whose
// failing read still fills the buffer (the SHARED_SPI CMD12 failure shape).
//
// Red on pristine SdFat 2.3.1, green with patches/0001 applied.

#include "common/FsCache.h"
#include "common/FsBlockDeviceInterface.h"

#include <cstring>
#include "test_utils.h"


namespace {

constexpr uint32_t kSectorA = 100;
constexpr uint32_t kSectorB = 200;
constexpr uint32_t kSectorC = 300;
constexpr size_t kSectorBytes = 512;

class FakeDevice : public FsBlockDeviceInterface {
 public:
  // Failure plan: the read of failSector fills dst, then returns false.
  uint32_t failSector = 0;
  int readsAfterFailure = 0;
  uint32_t lastReadSector = 0;
  uint32_t writtenSector = 0;
  uint8_t writtenByte = 0;
  bool wroteAnything = false;

  bool readSector(uint32_t sector, uint8_t* dst) override {
    lastReadSector = sector;
    // Distinct fill per sector: the byte equals the low bits of the number.
    memset(dst, static_cast<uint8_t>(sector), kSectorBytes);
    if (sector == failSector) {
      ++readsAfterFailure;
      return false;
    }
    return true;
  }
  bool readSectors(uint32_t sector, uint8_t* dst, size_t ns) override {
    for (size_t i = 0; i < ns; ++i) {
      if (!readSector(sector + i, dst + i * kSectorBytes)) return false;
    }
    return true;
  }
  bool writeSector(uint32_t sector, const uint8_t* src) override {
    wroteAnything = true;
    writtenSector = sector;
    writtenByte = src[0];
    return true;
  }
  bool writeSectors(uint32_t sector, const uint8_t* src, size_t ns) override {
    for (size_t i = 0; i < ns; ++i) {
      if (!writeSector(sector + i, src + i * kSectorBytes)) return false;
    }
    return true;
  }
  bool isBusy() override { return false; }
  bool syncDevice() override { return true; }
  uint32_t sectorCount() override { return 4096; }
};

}  // namespace

int main() {
  TestUtils::TestRunner runner("FsCacheInvalidationTest");

  // Cache sector A, then a read of B that fills the buffer and fails.
  {
    FakeDevice dev;
    FsCache cache;
    cache.init(&dev);

    uint8_t* a = cache.prepare(kSectorA, 0);
    runner.expectTrue(a != nullptr, "sector A caches");
    runner.expectEq(static_cast<int>(static_cast<uint8_t>(kSectorA)), static_cast<int>(a[0]),
                    "sector A holds its own bytes");

    dev.failSector = kSectorB;
    runner.expectTrue(cache.prepare(kSectorB, 0) == nullptr, "failing read of B returns null");

    // The bug: a prepare for A returns B's bytes without re-reading.
    const uint32_t sectorBefore = dev.lastReadSector;
    uint8_t* again = cache.prepare(kSectorA, FsCache::CACHE_FOR_WRITE);
    runner.expectTrue(again != nullptr, "sector A prepares again");
    runner.expectEq(static_cast<int>(static_cast<uint8_t>(kSectorA)), static_cast<int>(again[0]),
                    "sector A re-read, not B's leftover bytes");
    runner.expectTrue(dev.lastReadSector != sectorBefore || again[0] == static_cast<uint8_t>(kSectorA),
                      "A comes from a fresh read or valid cache");

    // No write-back of B's bytes to A.
    runner.expectTrue(cache.sync(), "cache syncs");
    runner.expectTrue(!dev.wroteAnything || dev.writtenByte == static_cast<uint8_t>(kSectorA),
                      "no foreign bytes written to A");
  }

  // A clean failing read must not poison later unrelated sectors either.
  {
    FakeDevice dev;
    FsCache cache;
    cache.init(&dev);

    dev.failSector = kSectorB;
    runner.expectTrue(cache.prepare(kSectorB, 0) == nullptr, "B fails again");
    uint8_t* c = cache.prepare(kSectorC, FsCache::CACHE_FOR_WRITE);
    runner.expectTrue(c != nullptr, "sector C prepares after the failure");
    runner.expectEq(static_cast<int>(static_cast<uint8_t>(kSectorC)), static_cast<int>(c[0]),
                    "sector C holds its own bytes");
    runner.expectTrue(cache.sync(), "sync after C");
    runner.expectTrue(!dev.wroteAnything || dev.writtenByte == static_cast<uint8_t>(kSectorC),
                      "no foreign bytes written to C");
  }

  return runner.allPassed() ? 0 : 1;
}
