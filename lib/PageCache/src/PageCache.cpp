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
constexpr uint8_t CACHE_FILE_VERSION = 20;  // v20: CSS block-level margins/padding change page layout

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
constexpr uint32_t kHeaderSize = 33;

struct CacheHeader {
  uint8_t version = 0;
  RenderConfig config{};
  uint16_t pageCount = 0;
  uint8_t partial = 0;
  uint32_t lutOffset = 0;
  uint32_t bytesConsumed = 0;
  uint32_t totalBytes = 0;
};

bool readCacheHeader(FsFile& file, CacheHeader& header) {
  return file.seek(0) && serialization::readPodChecked(file, header.version) &&
         serialization::readPodChecked(file, header.config.fontId) &&
         serialization::readPodChecked(file, header.config.lineCompression) &&
         serialization::readPodChecked(file, header.config.indentLevel) &&
         serialization::readPodChecked(file, header.config.spacingLevel) &&
         serialization::readPodChecked(file, header.config.paragraphAlignment) &&
         serialization::readPodChecked(file, header.config.hyphenation) &&
         serialization::readPodChecked(file, header.config.showImages) &&
         serialization::readPodChecked(file, header.config.viewportWidth) &&
         serialization::readPodChecked(file, header.config.viewportHeight) &&
         serialization::readPodChecked(file, header.pageCount) && serialization::readPodChecked(file, header.partial) &&
         serialization::readPodChecked(file, header.lutOffset) &&
         serialization::readPodChecked(file, header.bytesConsumed) &&
         serialization::readPodChecked(file, header.totalBytes);
}

bool hasValidLutSpan(const CacheHeader& header, size_t fileSize) {
  if (header.partial > 1 || header.lutOffset < kHeaderSize || header.lutOffset > fileSize) return false;
  const size_t lutSize = static_cast<size_t>(header.pageCount) * sizeof(uint32_t);
  return lutSize <= fileSize - header.lutOffset;
}
}  // namespace

PageCache::PageCache(std::string cachePath) : cachePath_(std::move(cachePath)) {}

bool PageCache::writeHeader(bool isPartial) {
  const uint8_t partial = isPartial ? 1 : 0;
  const uint32_t lutOffset = 0;
  return file_.seek(0) && serialization::writePodChecked(file_, CACHE_FILE_VERSION) &&
         serialization::writePodChecked(file_, config_.fontId) &&
         serialization::writePodChecked(file_, config_.lineCompression) &&
         serialization::writePodChecked(file_, config_.indentLevel) &&
         serialization::writePodChecked(file_, config_.spacingLevel) &&
         serialization::writePodChecked(file_, config_.paragraphAlignment) &&
         serialization::writePodChecked(file_, config_.hyphenation) &&
         serialization::writePodChecked(file_, config_.showImages) &&
         serialization::writePodChecked(file_, config_.viewportWidth) &&
         serialization::writePodChecked(file_, config_.viewportHeight) &&
         serialization::writePodChecked(file_, pageCount_) && serialization::writePodChecked(file_, partial) &&
         serialization::writePodChecked(file_, lutOffset) && serialization::writePodChecked(file_, bytesConsumed_) &&
         serialization::writePodChecked(file_, totalBytes_);
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
    if (!serialization::writePodChecked(file_, pos)) return false;
  }

  // Update header with final values
  const uint8_t partial = isPartial_ ? 1 : 0;
  return file_.seek(kPageCountOffset) && serialization::writePodChecked(file_, pageCount_) &&
         serialization::writePodChecked(file_, partial) && serialization::writePodChecked(file_, lutOffset) &&
         serialization::writePodChecked(file_, bytesConsumed_) && serialization::writePodChecked(file_, totalBytes_);
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

  CacheHeader header;
  if (!readCacheHeader(file_, header) || header.version != CACHE_FILE_VERSION || !hasValidLutSpan(header, fileSize)) {
    LOG_ERR(TAG, "Invalid cache header");
    file_.close();
    return false;
  }
  lutOffset_ = header.lutOffset;
  pageCount_ = header.pageCount;

  // Read existing LUT entries
  if (!file_.seek(lutOffset_)) {
    file_.close();
    return false;
  }
  lut.reserve(pageCount_);
  for (uint16_t i = 0; i < pageCount_; i++) {
    uint32_t pos;
    if (!serialization::readPodChecked(file_, pos)) {
      file_.close();
      lut.clear();
      return false;
    }
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

  const size_t fileSize = file_.size();
  CacheHeader header;
  if (!readCacheHeader(file_, header) || header.version != CACHE_FILE_VERSION || !hasValidLutSpan(header, fileSize)) {
    file_.close();
    LOG_ERR(TAG, "Invalid cache header");
    return false;
  }

  pageCount_ = header.pageCount;
  isPartial_ = header.partial != 0;
  lutOffset_ = header.lutOffset;
  bytesConsumed_ = header.bytesConsumed;
  totalBytes_ = header.totalBytes;

  file_.close();
  return true;
}

bool PageCache::load(const RenderConfig& config) {
  if (!SdMan.openFileForRead("CACHE", cachePath_, file_)) {
    return false;
  }

  // Read and validate header
  const size_t fileSize = file_.size();
  CacheHeader header;
  if (!readCacheHeader(file_, header) || header.version != CACHE_FILE_VERSION || !hasValidLutSpan(header, fileSize)) {
    file_.close();
    LOG_ERR(TAG, "Invalid cache header");
    clear();
    return false;
  }

  if (config != header.config) {
    file_.close();
    LOG_INF(TAG, "Config mismatch, invalidating cache");
    clear();
    return false;
  }

  pageCount_ = header.pageCount;
  isPartial_ = header.partial != 0;
  lutOffset_ = header.lutOffset;
  bytesConsumed_ = header.bytesConsumed;
  totalBytes_ = header.totalBytes;
  config_ = config;

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
      CacheHeader header;
      const bool validHeader = readCacheHeader(hdr, header) && header.version == CACHE_FILE_VERSION &&
                               hasValidLutSpan(header, hdr.size()) && header.pageCount == skipPages;
      hdr.close();
      if (!validHeader) {
        LOG_ERR(TAG, "Invalid header for extend");
        return false;
      }
      oldLutOffset = header.lutOffset;
      oldPageCount = header.pageCount;
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
    if (!writeHeader(false)) {
      file_.close();
      SdMan.remove(cachePath_.c_str());
      return false;
    }
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
      if (!file_.seek(srcPos) || file_.read(copyBuf, toRead) != toRead || !file_.seek(dstPos) ||
          file_.write(copyBuf, toRead) != toRead) {
        copyOk = false;
        break;
      }
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
    // Append new entries and update header
    bool writeOk = true;
    for (const uint32_t pos : lut) {
      writeOk = writeOk && serialization::writePodChecked(file_, pos);
    }
    const uint8_t partial = isPartial_ ? 1 : 0;
    writeOk = writeOk && file_.seek(kPageCountOffset) && serialization::writePodChecked(file_, pageCount_) &&
              serialization::writePodChecked(file_, partial) && serialization::writePodChecked(file_, newLutOffset) &&
              serialization::writePodChecked(file_, bytesConsumed_) &&
              serialization::writePodChecked(file_, totalBytes_);
    if (!writeOk) {
      file_.close();
      pageCount_ = skipPages;
      SdMan.remove(cachePath_.c_str());
      return false;
    }
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

  const bool synced = file_.sync();
  file_.close();
  if (!synced) {
    SdMan.remove(cachePath_.c_str());
    return false;
  }
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
    CacheHeader header;
    if (!readCacheHeader(file_, header) || header.version != CACHE_FILE_VERSION || header.pageCount != pageCount_ ||
        !hasValidLutSpan(header, file_.size())) {
      LOG_ERR(TAG, "Invalid header for hot extend");
      file_.close();
      return false;
    }
    const uint32_t oldLutOffset = header.lutOffset;

    if (!file_.seekEnd()) {
      file_.close();
      return false;
    }

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
        if (!file_.seek(srcPos)) {
          LOG_ERR(TAG, "LUT copy seek failed at %u", srcPos);
          pageCount_ = pagesBefore;
          file_.close();
          SdMan.remove(cachePath_.c_str());
          return false;
        }
        const size_t n = file_.read(buf, toRead);
        if (n != toRead || !file_.seek(dstPos) || file_.write(buf, n) != n) {
          LOG_ERR(TAG, "LUT copy failed at %u", srcPos);
          pageCount_ = pagesBefore;
          file_.close();
          SdMan.remove(cachePath_.c_str());
          return false;
        }
        srcPos += static_cast<uint32_t>(n);
        dstPos += static_cast<uint32_t>(n);
        remaining -= static_cast<uint32_t>(n);
      }
    }

    // Append new LUT entries and update header
    bool writeOk = file_.seek(newLutOffset + oldPageCount * static_cast<uint32_t>(sizeof(uint32_t)));
    for (uint16_t i = 0; i < newCount; i++) {
      writeOk = writeOk && serialization::writePodChecked(file_, newOffsets[i]);
    }

    lutOffset_ = newLutOffset;
    const uint8_t partial = isPartial_ ? 1 : 0;
    writeOk = writeOk && file_.seek(kPageCountOffset) && serialization::writePodChecked(file_, pageCount_) &&
              serialization::writePodChecked(file_, partial) && serialization::writePodChecked(file_, newLutOffset) &&
              serialization::writePodChecked(file_, bytesConsumed_) &&
              serialization::writePodChecked(file_, totalBytes_) && file_.sync();
    file_.close();
    if (!writeOk) {
      pageCount_ = pagesBefore;
      SdMan.remove(cachePath_.c_str());
      return false;
    }
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

    CacheHeader header;
    if (!readCacheHeader(file_, header) || header.version != CACHE_FILE_VERSION || pageNum >= header.pageCount ||
        !hasValidLutSpan(header, fileSize)) {
      LOG_ERR(TAG, "Invalid cache header while loading page");
      file_.close();
      continue;
    }
    const uint32_t lutOffset = header.lutOffset;

    // Validate LUT offset and requested LUT entry
    const size_t lutEntryEnd = static_cast<size_t>(lutOffset) + (static_cast<size_t>(pageNum) + 1) * sizeof(uint32_t);
    if (lutEntryEnd > fileSize) {
      LOG_ERR(TAG, "Invalid LUT offset: %u (file size: %zu)", lutOffset, fileSize);
      file_.close();
      continue;
    }

    // Read page position from LUT
    if (!file_.seek(lutOffset + static_cast<size_t>(pageNum) * sizeof(uint32_t))) {
      file_.close();
      continue;
    }
    uint32_t pagePos;
    if (!serialization::readPodChecked(file_, pagePos)) {
      file_.close();
      continue;
    }

    // Validate page position
    if (pagePos < kHeaderSize || pagePos >= lutOffset) {
      LOG_ERR(TAG, "Invalid page position: %u (LUT offset: %u)", pagePos, lutOffset);
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

  CacheHeader header;
  if (!readCacheHeader(file, header) || header.version != CACHE_FILE_VERSION || config != header.config ||
      !hasValidLutSpan(header, fileSize)) {
    file.close();
    return result;
  }

  result.pageCount = header.pageCount;
  result.partial = header.partial != 0;

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
