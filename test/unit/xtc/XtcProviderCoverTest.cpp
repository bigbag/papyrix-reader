#include "test_utils.h"

#include <HardwareSerial.h>
#include <HomeThumbnail.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <XtcProvider.h>
#include <Xtc/XtcTypes.h>
#include <platform_stubs.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

// Mirrors the builders in XtcCoverHelperTest.cpp (minimal XTC/XTCH files).
static std::string buildXtcFile1Bit(uint16_t width, uint16_t height, const std::vector<uint8_t>& pixelData) {
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t pageTableOffset = headerSize + 128 + 64;
  const size_t pageDataOffset = pageTableOffset + sizeof(xtc::PageTableEntry);

  const size_t bitmapSize = ((width + 7) / 8) * static_cast<size_t>(height);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;

  std::string buf(pageDataOffset + pageDataSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

  auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
  hdr->magic = xtc::XTC_MAGIC;
  hdr->versionMajor = 1;
  hdr->versionMinor = 0;
  hdr->pageCount = 1;
  hdr->hasMetadata = 1;
  hdr->pageTableOffset = pageTableOffset;
  hdr->dataOffset = pageDataOffset;

  memcpy(data + headerSize, "Test Book", strlen("Test Book"));

  auto* pte = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  pte->dataOffset = pageDataOffset;
  pte->dataSize = static_cast<uint32_t>(pageDataSize);
  pte->width = width;
  pte->height = height;

  auto* pageHdr = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHdr->magic = xtc::XTG_MAGIC;
  pageHdr->width = width;
  pageHdr->height = height;
  pageHdr->dataSize = static_cast<uint32_t>(bitmapSize);

  const size_t toCopy = std::min(pixelData.size(), bitmapSize);
  if (toCopy > 0) {
    memcpy(data + pageDataOffset + sizeof(xtc::XtgPageHeader), pixelData.data(), toCopy);
  }
  return buf;
}

static std::string buildXtcFile2Bit(uint16_t width, uint16_t height, const std::vector<uint8_t>& pixelData) {
  constexpr size_t headerSize = sizeof(xtc::XtcHeader);
  constexpr size_t pageTableOffset = headerSize + 128 + 64;
  const size_t pageDataOffset = pageTableOffset + sizeof(xtc::PageTableEntry);

  const size_t bitmapSize = xtc::xthBitmapSize(width, height);
  const size_t pageDataSize = sizeof(xtc::XtgPageHeader) + bitmapSize;

  std::string buf(pageDataOffset + pageDataSize, '\0');
  auto* data = reinterpret_cast<uint8_t*>(&buf[0]);

  auto* hdr = reinterpret_cast<xtc::XtcHeader*>(data);
  hdr->magic = xtc::XTCH_MAGIC;
  hdr->versionMajor = 1;
  hdr->versionMinor = 0;
  hdr->pageCount = 1;
  hdr->hasMetadata = 1;
  hdr->pageTableOffset = pageTableOffset;
  hdr->dataOffset = pageDataOffset;

  memcpy(data + headerSize, "Test Book 2bit", strlen("Test Book 2bit"));

  auto* pte = reinterpret_cast<xtc::PageTableEntry*>(data + pageTableOffset);
  pte->dataOffset = pageDataOffset;
  pte->dataSize = static_cast<uint32_t>(pageDataSize);
  pte->width = width;
  pte->height = height;

  auto* pageHdr = reinterpret_cast<xtc::XtgPageHeader*>(data + pageDataOffset);
  pageHdr->magic = xtc::XTH_MAGIC;
  pageHdr->width = width;
  pageHdr->height = height;
  pageHdr->dataSize = static_cast<uint32_t>(bitmapSize);

  const size_t toCopy = std::min(pixelData.size(), bitmapSize);
  if (toCopy > 0) {
    memcpy(data + pageDataOffset + sizeof(xtc::XtgPageHeader), pixelData.data(), toCopy);
  }
  return buf;
}

int main() {
  TestUtils::TestRunner runner("XtcProviderCover Tests");
  testResetLargestFreeBlock();

  // ---- Test: cover generates from page 0 ----
  std::string cachePath;
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/book.xtc", buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF)));

    papyrix::XtcProvider provider;
    runner.expectTrue(provider.open("/book.xtc", "/cache").ok(), "open: succeeds");
    cachePath = provider.meta.cachePath;
    const std::string coverPath = provider.getCoverBmpPath();
    const std::string failedMarker = cachePath + "/.cover.failed";
    runner.expectTrue(coverPath == cachePath + "/cover.bmp", "open: cover path is <cache>/cover.bmp");

    runner.expectTrue(provider.generateCoverBmp(), "generate: succeeds");
    runner.expectTrue(home_thumbnail::validateCover(coverPath), "generate: published cover is valid");
    runner.expectFalse(SdMan.exists(failedMarker.c_str()), "generate: no failure marker");
    provider.close();
  }

  // ---- Test: stale markers invalidated exactly once ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/book.xtc", buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF)));
    // Pre-existing cache poisoned by the old page-buffer generator
    SdMan.mkdir(cachePath.c_str());
    SdMan.registerFile(cachePath + "/.cover.failed", "");
    SdMan.registerFile(cachePath + "/.thumb.failed", "");

    papyrix::XtcProvider provider;
    provider.open("/book.xtc", "/cache");
    runner.expectFalse(SdMan.exists((cachePath + "/.cover.failed").c_str()), "migrate: cover marker removed");
    runner.expectFalse(SdMan.exists((cachePath + "/.thumb.failed").c_str()), "migrate: thumb marker removed");
    runner.expectTrue(SdMan.exists((cachePath + "/.cover.v2").c_str()), "migrate: sentinel created");
    provider.close();

    // Markers written after migration must persist across re-open
    SdMan.registerFile(cachePath + "/.cover.failed", "");
    provider.open("/book.xtc", "/cache");
    runner.expectTrue(SdMan.exists((cachePath + "/.cover.failed").c_str()),
                      "migrate: post-migration marker persists");
    provider.close();
  }

  // ---- Test: valid cover overrides an existing marker ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/book.xtc", buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF)));

    papyrix::XtcProvider provider;
    provider.open("/book.xtc", "/cache");
    runner.expectTrue(provider.generateCoverBmp(), "override: cover generated first");
    SdMan.registerFile(cachePath + "/.cover.failed", "");
    runner.expectTrue(provider.generateCoverBmp(), "override: valid cover wins");
    runner.expectFalse(SdMan.exists((cachePath + "/.cover.failed").c_str()), "override: marker removed");
    provider.close();
  }

  // ---- Test: deterministic failure persists a marker and suppresses retry ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    std::string corrupt = buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF));
    const size_t pageDataOffset = 56 + 128 + 64 + 16;
    corrupt[pageDataOffset] = 'X';
    corrupt[pageDataOffset + 1] = 'X';
    SdMan.registerFile("/bad.xtc", corrupt);

    papyrix::XtcProvider provider;
    provider.open("/bad.xtc", "/cache");
    const std::string badCache = provider.meta.cachePath;
    runner.expectFalse(provider.generateCoverBmp(), "invalid: generation fails");
    runner.expectTrue(SdMan.exists((badCache + "/.cover.failed").c_str()), "invalid: marker persisted");
    runner.expectFalse(provider.generateCoverBmp(), "invalid: marker suppresses retry");
    provider.close();
  }

  // ---- Test: transient failure leaves no marker and retries ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    // 100x200 XTCH: band scratch 2048 bytes, blocked when 80% gate < 2048
    SdMan.registerFile("/mem.xtch", buildXtcFile2Bit(100, 200, std::vector<uint8_t>(xtc::xthBitmapSize(100, 200))));

    papyrix::XtcProvider provider;
    provider.open("/mem.xtch", "/cache");
    const std::string memCache = provider.meta.cachePath;
    testSetLargestFreeBlock(1500);
    runner.expectFalse(provider.generateCoverBmp(), "transient: generation fails");
    runner.expectFalse(SdMan.exists((memCache + "/.cover.failed").c_str()), "transient: no marker");
    runner.expectFalse(SdMan.exists((memCache + "/cover.bmp").c_str()), "transient: no cover published");
    testResetLargestFreeBlock();
    runner.expectTrue(provider.generateCoverBmp(), "transient: retry succeeds after recovery");
    provider.close();
  }

  // ---- Test: cancellation creates no marker ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/book.xtc", buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF)));

    papyrix::XtcProvider provider;
    provider.open("/book.xtc", "/cache");
    runner.expectFalse(provider.generateCoverBmp([]() { return true; }), "cancel: generation aborts");
    runner.expectFalse(SdMan.exists((cachePath + "/.cover.failed").c_str()), "cancel: no marker");
    runner.expectFalse(SdMan.exists((cachePath + "/cover.bmp").c_str()), "cancel: no cover published");
    runner.expectFalse(SdMan.exists((cachePath + "/cover.bmp.part").c_str()), "cancel: no part left");
    provider.close();
  }

  // ---- Test: stale 2-bit artifacts regenerate after threshold restore ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/book.xtch", buildXtcFile2Bit(32, 16, std::vector<uint8_t>(xtc::xthBitmapSize(32, 16))));

    papyrix::XtcProvider provider;
    provider.open("/book.xtch", "/cache");
    const std::string twoBitCache = provider.meta.cachePath;
    provider.close();

    // Simulate a device upgrading from the >=2-threshold build: stale
    // artifacts present, no .cover.v2 sentinel (only the old .cover.migrated).
    SdMan.registerFile(twoBitCache + "/cover.bmp", "stale");
    SdMan.registerFile(twoBitCache + "/thumb.bmp", "stale");
    SdMan.registerFile(twoBitCache + "/.cover.migrated", "");
    SdMan.remove((twoBitCache + "/.cover.v2").c_str());  // device never had the v2 sentinel

    provider.open("/book.xtch", "/cache");
    runner.expectFalse(SdMan.exists((twoBitCache + "/cover.bmp").c_str()),
                       "purge: stale 2-bit cover removed on first open");
    runner.expectFalse(SdMan.exists((twoBitCache + "/thumb.bmp").c_str()),
                       "purge: stale 2-bit thumb removed on first open");
    runner.expectFalse(SdMan.exists((twoBitCache + "/.cover.migrated").c_str()),
                       "purge: legacy sentinel removed");
    runner.expectTrue(SdMan.exists((twoBitCache + "/.cover.v2").c_str()), "purge: v2 sentinel created");
    runner.expectTrue(provider.generateCoverBmp(), "purge: cover regenerates with restored threshold");
    runner.expectTrue(home_thumbnail::validateCover(provider.getCoverBmpPath()), "purge: regenerated cover valid");

    // Second open must not purge again (sentinel guards one-time migration)
    provider.close();
    provider.open("/book.xtch", "/cache");
    runner.expectTrue(SdMan.exists((twoBitCache + "/cover.bmp").c_str()),
                      "purge: regenerated cover survives re-open");
    provider.close();
  }

  // ---- Test: 1-bit artifacts are NOT purged by migration ----
  {
    SdMan.clearFiles();
    SdMan.clearWrittenFiles();
    SdMan.registerFile("/book.xtc", buildXtcFile1Bit(16, 8, std::vector<uint8_t>(16, 0xFF)));

    papyrix::XtcProvider provider;
    runner.expectTrue(provider.open("/book.xtc", "/cache").ok(), "1bit migrate: open succeeds");
    runner.expectTrue(provider.generateCoverBmp(), "1bit migrate: cover generated");
    provider.close();

    // Reset sentinel to simulate migration pending; cover content is
    // threshold-independent for 1-bit sources and must be kept.
    SdMan.remove((cachePath + "/.cover.v2").c_str());
    SdMan.registerFile(cachePath + "/.cover.migrated", "");

    provider.open("/book.xtc", "/cache");
    runner.expectTrue(SdMan.exists((cachePath + "/cover.bmp").c_str()),
                      "1bit migrate: 1-bit cover kept");
    runner.expectTrue(SdMan.exists((cachePath + "/.cover.v2").c_str()), "1bit migrate: sentinel created");
    provider.close();
  }

  return runner.allPassed() ? 0 : 1;
}
