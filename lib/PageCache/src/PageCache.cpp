#include "PageCache.h"

#include <Logging.h>

#ifdef ARDUINO
#include <esp_task_wdt.h>
#endif

#define TAG "CACHE"

#include <Page.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "ContentParser.h"

#ifndef ARDUINO
uint16_t PageCache::failSerializeInterval_ = 0;
uint16_t PageCache::failSerializeCounter_ = 0;
#endif

namespace {
constexpr uint8_t CACHE_FILE_VERSION = 19;  // v19: + bytesConsumed/totalBytes for partial-cache page-total estimate

// Header layout (offsets are absolute from start of file):
// - version (1 byte)        @ 0
// - fontId (4 bytes)        @ 1
// - lineCompression (4)     @ 5
// - indentLevel (1)         @ 9
// - spacingLevel (1)        @ 10
// - paragraphAlignment (1)  @ 11
// - hyphenation (1)         @ 12
// - showImages (1)          @ 13
// - viewportWidth (2)       @ 14
// - viewportHeight (2)      @ 16
// - pageCount (2)           @ 18
// - isPartial (1)           @ 20
// - lutOffset (4)           @ 21
// - bytesConsumed (4)       @ 25  (v19+)
// - totalBytes (4)          @ 29  (v19+)
constexpr uint32_t kPageCountOffset = 18;
constexpr uint32_t kLutOffsetOffset = 21;
constexpr uint32_t kBytesConsumedOffset = 25;
constexpr uint32_t kHeaderSize = 33;
}  // namespace

PageCache::PageCache(std::string cachePath) : cachePath_(std::move(cachePath)) {}

bool PageCache::writeHeader(bool isPartial) {
  file_.seek(0);
  serialization::writePod(file_, CACHE_FILE_VERSION);
  serialization::writePod(file_, config_.fontId);
  serialization::writePod(file_, config_.lineCompression);
  serialization::writePod(file_, config_.indentLevel);
  serialization::writePod(file_, config_.spacingLevel);
  serialization::writePod(file_, config_.paragraphAlignment);
  serialization::writePod(file_, config_.hyphenation);
  serialization::writePod(file_, config_.showImages);
  serialization::writePod(file_, config_.viewportWidth);
  serialization::writePod(file_, config_.viewportHeight);
  serialization::writePod(file_, pageCount_);
  serialization::writePod(file_, static_cast<uint8_t>(isPartial ? 1 : 0));
  serialization::writePod(file_, static_cast<uint32_t>(0));  // LUT offset placeholder
  serialization::writePod(file_, bytesConsumed_);
  serialization::writePod(file_, totalBytes_);
  return true;
}

bool PageCache::writeLut(const std::vector<uint32_t>& lut) {
  if (lut.size() != pageCount_) {
    LOG_ERR(TAG, "LUT size mismatch: %zu entries vs %u pages", lut.size(), pageCount_);
    return false;
  }

  const uint32_t lutOffset = file_.position();

  for (const uint32_t pos : lut) {
    if (pos == 0) {
      LOG_ERR(TAG, "Invalid page position in LUT");
      return false;
    }
    serialization::writePod(file_, pos);
  }

  // Update header with final values
  file_.seek(kPageCountOffset);
  serialization::writePod(file_, pageCount_);
  serialization::writePod(file_, static_cast<uint8_t>(isPartial_ ? 1 : 0));
  serialization::writePod(file_, lutOffset);
  serialization::writePod(file_, bytesConsumed_);
  serialization::writePod(file_, totalBytes_);

  return true;
}

bool PageCache::loadLut(std::vector<uint32_t>& lut) {
  if (!SdMan.openFileForRead("CACHE", cachePath_, file_)) {
    return false;
  }

  const size_t fileSize = file_.size();
  if (fileSize < kHeaderSize) {
    LOG_ERR(TAG, "File too small: %zu (need %u)", fileSize, kHeaderSize);
    file_.close();
    return false;
  }

  // Read lutOffset from header
  file_.seek(kLutOffsetOffset);
  serialization::readPod(file_, lutOffset_);

  // Validate lutOffset before seeking
  if (lutOffset_ < kHeaderSize || lutOffset_ >= fileSize) {
    LOG_ERR(TAG, "Invalid lutOffset: %u (file size: %zu)", lutOffset_, fileSize);
    file_.close();
    return false;
  }

  // Read pageCount from header
  file_.seek(kPageCountOffset);
  serialization::readPod(file_, pageCount_);

  // Validate LUT fits within file
  const size_t lutEnd = lutOffset_ + static_cast<size_t>(pageCount_) * sizeof(uint32_t);
  if (lutEnd > fileSize) {
    LOG_ERR(TAG, "LUT extends past file: %zu > %zu", lutEnd, fileSize);
    file_.close();
    return false;
  }

  // Read existing LUT entries
  file_.seek(lutOffset_);
  lut.reserve(pageCount_);
  for (uint16_t i = 0; i < pageCount_; i++) {
    uint32_t pos;
    serialization::readPod(file_, pos);
    if (pos == 0 || pos < kHeaderSize || pos >= lutOffset_) {
      LOG_ERR(TAG, "Corrupt LUT entry %d: position %u", i, pos);
      file_.close();
      lut.clear();
      return false;
    }
    lut.push_back(pos);
  }

  file_.close();
  return true;
}

bool PageCache::loadRaw() {
  if (!SdMan.openFileForRead("CACHE", cachePath_, file_)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(file_, version);
  if (version != CACHE_FILE_VERSION) {
    file_.close();
    LOG_ERR(TAG, "Version mismatch: got %u, expected %u", version, CACHE_FILE_VERSION);
    return false;
  }

  // Skip config fields, read pageCount and isPartial
  file_.seek(kPageCountOffset);
  serialization::readPod(file_, pageCount_);
  uint8_t partial;
  serialization::readPod(file_, partial);
  isPartial_ = (partial != 0);

  // Skip lutOffset, read bytesConsumed/totalBytes
  file_.seek(kBytesConsumedOffset);
  serialization::readPod(file_, bytesConsumed_);
  serialization::readPod(file_, totalBytes_);

  file_.close();
  return true;
}

bool PageCache::load(const RenderConfig& config) {
  if (!SdMan.openFileForRead("CACHE", cachePath_, file_)) {
    return false;
  }

  // Read and validate header
  uint8_t version;
  serialization::readPod(file_, version);
  if (version != CACHE_FILE_VERSION) {
    file_.close();
    LOG_ERR(TAG, "Version mismatch: got %u, expected %u", version, CACHE_FILE_VERSION);
    clear();
    return false;
  }

  RenderConfig fileConfig;
  serialization::readPod(file_, fileConfig.fontId);
  serialization::readPod(file_, fileConfig.lineCompression);
  serialization::readPod(file_, fileConfig.indentLevel);
  serialization::readPod(file_, fileConfig.spacingLevel);
  serialization::readPod(file_, fileConfig.paragraphAlignment);
  serialization::readPod(file_, fileConfig.hyphenation);
  serialization::readPod(file_, fileConfig.showImages);
  serialization::readPod(file_, fileConfig.viewportWidth);
  serialization::readPod(file_, fileConfig.viewportHeight);

  if (config != fileConfig) {
    file_.close();
    LOG_INF(TAG, "Config mismatch, invalidating cache");
    clear();
    return false;
  }

  serialization::readPod(file_, pageCount_);
  uint8_t partial;
  serialization::readPod(file_, partial);
  isPartial_ = (partial != 0);
  config_ = config;

  // Skip lutOffset, read parser-progress sample
  file_.seek(kBytesConsumedOffset);
  serialization::readPod(file_, bytesConsumed_);
  serialization::readPod(file_, totalBytes_);

  file_.close();
  LOG_INF(TAG, "Loaded: %d pages, partial=%d", pageCount_, isPartial_);
  return true;
}

bool PageCache::create(ContentParser& parser, const RenderConfig& config, uint16_t maxPages, uint16_t skipPages,
                       const AbortCallback& shouldAbort) {
  const unsigned long startMs = millis();

  // For extends with existing pages, track old LUT position for disk-to-disk copy
  uint32_t oldLutOffset = 0;
  uint16_t oldPageCount = 0;
  std::vector<uint32_t> lut;

  if (skipPages > 0) {
    // Read LUT position from header (no vector allocation)
    {
      FsFile hdr;
      if (!SdMan.openFileForRead("CACHE", cachePath_, hdr)) {
        LOG_ERR(TAG, "Failed to read header for extend");
        return false;
      }
      hdr.seek(kLutOffsetOffset);
      serialization::readPod(hdr, oldLutOffset);
      hdr.seek(kPageCountOffset);
      serialization::readPod(hdr, oldPageCount);
      hdr.close();
    }

    if (!file_.open(cachePath_.c_str(), O_RDWR)) {
      LOG_ERR(TAG, "Failed to open cache file for append");
      return false;
    }
    file_.seekEnd();
  } else {
    // Fresh create
    if (!SdMan.openFileForWrite("CACHE", cachePath_, file_)) {
      LOG_ERR(TAG, "Failed to open cache file for writing");
      return false;
    }

    config_ = config;
    pageCount_ = 0;
    isPartial_ = false;

    // Write placeholder header
    writeHeader(false);
  }

  // Check for abort before starting expensive parsing
  if (shouldAbort && shouldAbort()) {
    file_.close();
    LOG_INF(TAG, "Aborted before parsing");
    return false;
  }

  uint16_t parsedPages = 0;
  bool hitMaxPages = false;
  bool aborted = false;

  bool success = parser.parsePages(
      [this, &lut, &hitMaxPages, &parsedPages, maxPages, skipPages](std::unique_ptr<Page> page) {
        if (hitMaxPages) return;

        parsedPages++;

        // Skip pages we already have cached
        if (parsedPages <= skipPages) {
          return;
        }

        // Serialize new page
        const uint32_t position = file_.position();
#ifndef ARDUINO
        if (failSerializeInterval_ > 0 && ++failSerializeCounter_ % failSerializeInterval_ == 0) {
          LOG_ERR(TAG, "Simulated serialize failure (page %u)", pageCount_);
          hitMaxPages = true;
          return;
        }
#endif
        if (!page->serialize(file_)) {
          LOG_ERR(TAG, "Failed to serialize page %u, stopping", pageCount_);
          hitMaxPages = true;
          return;
        }

        lut.push_back(position);
        pageCount_++;
        LOG_DBG(TAG, "Page %u cached", pageCount_ - 1);

#ifdef ARDUINO
        if (pageCount_ % 10 == 0) esp_task_wdt_reset();
#endif

        if (maxPages > 0 && pageCount_ >= maxPages) {
          hitMaxPages = true;
        }
      },
      maxPages, shouldAbort);

  // Check if we were aborted
  if (shouldAbort && shouldAbort()) {
    aborted = true;
    LOG_INF(TAG, "Aborted during parsing");
  }

  if ((!success && pageCount_ == 0) || aborted) {
    file_.close();
    if (skipPages == 0) {
      SdMan.remove(cachePath_.c_str());
    }
    LOG_ERR(TAG, "Parsing failed or aborted with %d pages (extend=%d)", pageCount_, skipPages > 0);
    return false;
  }

  isPartial_ = parser.hasMoreContent();
  bytesConsumed_ = parser.bytesConsumed();
  totalBytes_ = parser.totalBytes();

  if (skipPages > 0 && oldPageCount > 0) {
    // Disk-to-disk LUT copy for cold extend: copy old entries then append new
    const uint32_t newLutOffset = file_.position();
    constexpr size_t kCopyBuf = 256;
    uint8_t copyBuf[kCopyBuf];
    uint32_t remaining = oldPageCount * static_cast<uint32_t>(sizeof(uint32_t));
    uint32_t srcPos = oldLutOffset;
    uint32_t dstPos = newLutOffset;
    bool copyOk = true;
    while (remaining > 0) {
      uint32_t toRead = remaining < kCopyBuf ? remaining : kCopyBuf;
      file_.seek(srcPos);
      if (file_.read(copyBuf, toRead) != toRead) {
        copyOk = false;
        break;
      }
      file_.seek(dstPos);
      file_.write(copyBuf, toRead);
      srcPos += toRead;
      dstPos += toRead;
      remaining -= toRead;
    }
    if (!copyOk) {
      file_.close();
      pageCount_ = skipPages;
      SdMan.remove(cachePath_.c_str());
      return false;
    }
    // Append new entries
    for (const uint32_t pos : lut) {
      serialization::writePod(file_, pos);
    }
    // Update header
    file_.seek(kPageCountOffset);
    serialization::writePod(file_, pageCount_);
    serialization::writePod(file_, static_cast<uint8_t>(isPartial_ ? 1 : 0));
    serialization::writePod(file_, newLutOffset);
    serialization::writePod(file_, bytesConsumed_);
    serialization::writePod(file_, totalBytes_);
    lutOffset_ = newLutOffset;
  } else if (!writeLut(lut)) {
    file_.close();
    if (skipPages == 0) {
      pageCount_ = 0;
      isPartial_ = false;
      bytesConsumed_ = 0;
      totalBytes_ = 0;
    } else {
      pageCount_ = skipPages;
    }
    SdMan.remove(cachePath_.c_str());
    return false;
  }

  file_.sync();
  file_.close();
  LOG_INF(TAG, "Created in %lu ms: %d pages, partial=%d", millis() - startMs, pageCount_, isPartial_);
  return true;
}

bool PageCache::extend(ContentParser& parser, uint16_t additionalPages, const AbortCallback& shouldAbort) {
  if (!isPartial_) {
    LOG_INF(TAG, "Cache is complete, nothing to extend");
    return true;
  }

  const uint16_t chunk = pageCount_ >= 30 ? 50 : additionalPages;
  const uint16_t currentPages = pageCount_;

  if (parser.canResume()) {
    // HOT PATH: Parser has live session from previous extend, just append new pages.
    // No re-parsing — O(chunk) work instead of O(totalPages).
    // Uses disk-to-disk LUT copy with small buffer to avoid large heap allocations
    // that fragment memory on spines with thousands of pages.
    LOG_INF(TAG, "Hot extend from %d pages (+%d)", currentPages, chunk);

    bool opened = false;
    for (int attempt = 0; attempt < 3; attempt++) {
      if (attempt > 0) delay(50);
      if (file_.open(cachePath_.c_str(), O_RDWR)) {
        opened = true;
        break;
      }
    }
    if (!opened) {
      LOG_ERR(TAG, "Failed to open cache file for hot extend");
      return false;
    }

    // Read current LUT position from header
    file_.seek(kLutOffsetOffset);
    uint32_t oldLutOffset = 0;
    serialization::readPod(file_, oldLutOffset);

    file_.seekEnd();

    const uint16_t pagesBefore = pageCount_;
    const uint16_t oldPageCount = pageCount_;
    uint32_t newOffsets[50];
    uint16_t newCount = 0;
    bool hitMaxPages = false;
    bool parseOk = parser.parsePages(
        [this, &newOffsets, &newCount, &hitMaxPages](std::unique_ptr<Page> page) {
          if (hitMaxPages || newCount >= 50) return;
          const uint32_t position = file_.position();
#ifndef ARDUINO
          if (failSerializeInterval_ > 0 && ++failSerializeCounter_ % failSerializeInterval_ == 0) {
            LOG_ERR(TAG, "Simulated serialize failure (page %u)", pageCount_);
            hitMaxPages = true;
            return;
          }
#endif
          if (!page->serialize(file_)) {
            LOG_ERR(TAG, "Failed to serialize page %u, stopping", pageCount_);
            hitMaxPages = true;
            return;
          }
          newOffsets[newCount++] = position;
          pageCount_++;
        },
        chunk, shouldAbort);

    isPartial_ = parser.hasMoreContent();
    bytesConsumed_ = parser.bytesConsumed();
    totalBytes_ = parser.totalBytes();

    if (!parseOk && pageCount_ == pagesBefore) {
      file_.close();
      LOG_ERR(TAG, "Hot extend failed with no new pages");
      return false;
    }

    // Copy old LUT entries to after the new pages using a small buffer (no heap alloc)
    const uint32_t newLutOffset = file_.position();
    {
      constexpr size_t kCopyBuf = 256;
      uint8_t buf[kCopyBuf];
      uint32_t remaining = oldPageCount * static_cast<uint32_t>(sizeof(uint32_t));
      uint32_t srcPos = oldLutOffset;
      uint32_t dstPos = newLutOffset;
      while (remaining > 0) {
        uint32_t toRead = remaining < kCopyBuf ? remaining : kCopyBuf;
        file_.seek(srcPos);
        size_t n = file_.read(buf, toRead);
        if (n != toRead) {
          LOG_ERR(TAG, "LUT copy read failed at %u", srcPos);
          pageCount_ = pagesBefore;
          file_.close();
          SdMan.remove(cachePath_.c_str());
          return false;
        }
        file_.seek(dstPos);
        file_.write(buf, n);
        srcPos += static_cast<uint32_t>(n);
        dstPos += static_cast<uint32_t>(n);
        remaining -= static_cast<uint32_t>(n);
      }
    }

    // Append new LUT entries
    file_.seek(newLutOffset + oldPageCount * static_cast<uint32_t>(sizeof(uint32_t)));
    for (uint16_t i = 0; i < newCount; i++) {
      serialization::writePod(file_, newOffsets[i]);
    }

    // Update header
    lutOffset_ = newLutOffset;
    file_.seek(kPageCountOffset);
    serialization::writePod(file_, pageCount_);
    serialization::writePod(file_, static_cast<uint8_t>(isPartial_ ? 1 : 0));
    serialization::writePod(file_, newLutOffset);
    serialization::writePod(file_, bytesConsumed_);
    serialization::writePod(file_, totalBytes_);

    file_.sync();
    file_.close();
    LOG_INF(TAG, "Hot extend done: %d pages, partial=%d", pageCount_, isPartial_);
    return true;
  }

  // COLD PATH: Fresh parser (after exit/reboot) — re-parse from start, skip cached pages.
  // For large caches, cold re-parsing thousands of skipped pages fragments the heap.
  // Skip the cold extend and let on-demand caching handle the rest.
  if (pageCount_ >= 1000) {
    LOG_INF(TAG, "Skipping cold extend at %d pages (too expensive), deferring to on-demand", pageCount_);
    isPartial_ = false;
    return true;
  }
  const uint16_t targetPages = pageCount_ + chunk;
  LOG_INF(TAG, "Cold extend from %d to %d pages", currentPages, targetPages);

  parser.reset();
  bool result = create(parser, config_, targetPages, currentPages, shouldAbort);

  // No forward progress AND parser has no more content → content is truly finished.
  // Without the hasMoreContent() check, an aborted extend (timeout/memory pressure)
  // would permanently mark the chapter as complete, truncating it.
  if (result && pageCount_ <= currentPages && !parser.hasMoreContent()) {
    LOG_INF(TAG, "No progress during extend (%d pages), marking complete", pageCount_);
    isPartial_ = false;
  }

  return result;
}

std::unique_ptr<Page> PageCache::loadPage(uint16_t pageNum) {
  if (pageNum >= pageCount_) {
    LOG_ERR(TAG, "Page %d out of range (max %d)", pageNum, pageCount_);
    return nullptr;
  }

  for (int attempt = 0; attempt < 3; attempt++) {
    if (attempt > 0) delay(50);

    if (!SdMan.openFileForRead("CACHE", cachePath_, file_)) {
      continue;
    }

    const size_t fileSize = file_.size();

    // Read LUT offset from header
    file_.seek(kLutOffsetOffset);
    uint32_t lutOffset;
    serialization::readPod(file_, lutOffset);

    // Validate LUT offset
    if (lutOffset < kHeaderSize || lutOffset >= fileSize) {
      LOG_ERR(TAG, "Invalid LUT offset: %u (file size: %zu)", lutOffset, fileSize);
      file_.close();
      continue;
    }

    // Read page position from LUT
    file_.seek(lutOffset + static_cast<size_t>(pageNum) * sizeof(uint32_t));
    uint32_t pagePos;
    serialization::readPod(file_, pagePos);

    // Validate page position
    if (pagePos < kHeaderSize || pagePos >= fileSize) {
      LOG_ERR(TAG, "Invalid page position: %u (file size: %zu)", pagePos, fileSize);
      file_.close();
      continue;
    }

    // Read page
    file_.seek(pagePos);
    auto page = Page::deserialize(file_);
    file_.close();

    if (page) return page;
  }

  return nullptr;
}

PageCache::ProbeResult PageCache::probe(const std::string& cachePath, const RenderConfig& config) {
  ProbeResult result;

  FsFile file;
  if (!SdMan.openFileForRead("PROBE", cachePath, file)) {
    return result;
  }

  const size_t fileSize = file.size();
  if (fileSize < kHeaderSize) {
    file.close();
    return result;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != CACHE_FILE_VERSION) {
    file.close();
    return result;
  }

  RenderConfig fileConfig;
  serialization::readPod(file, fileConfig.fontId);
  serialization::readPod(file, fileConfig.lineCompression);
  serialization::readPod(file, fileConfig.indentLevel);
  serialization::readPod(file, fileConfig.spacingLevel);
  serialization::readPod(file, fileConfig.paragraphAlignment);
  serialization::readPod(file, fileConfig.hyphenation);
  serialization::readPod(file, fileConfig.showImages);
  serialization::readPod(file, fileConfig.viewportWidth);
  serialization::readPod(file, fileConfig.viewportHeight);
  if (config != fileConfig) {
    file.close();
    return result;
  }

  serialization::readPod(file, result.pageCount);
  uint8_t partial;
  serialization::readPod(file, partial);
  result.partial = (partial != 0);

  file.close();
  result.valid = true;
  return result;
}

bool PageCache::clear() const {
  if (!SdMan.exists(cachePath_.c_str())) {
    return true;
  }
  return SdMan.remove(cachePath_.c_str());
}
