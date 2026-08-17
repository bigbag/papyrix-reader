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
bool PageCache::failHeaderCommitOnce_ = false;
#endif

namespace {
constexpr uint8_t CACHE_FILE_VERSION = 22;  // v22: source/font fingerprints; 32-bit indexes

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
// - pageCount (4)           @ 18
// - isPartial (1)           @ 22
// - lutOffset (4)           @ 23
// - bytesConsumed (4)       @ 27
// - totalBytes (4)          @ 31
// - sourceFingerprint (4)   @ 35
// - fontFingerprint (4)     @ 39
constexpr uint32_t kPageCountOffset = 18;
constexpr uint32_t kHeaderSize = 43;

struct CacheHeader {
  uint8_t version = 0;
  RenderConfig config{};
  uint32_t pageCount = 0;
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
         serialization::readPodChecked(file, header.totalBytes) &&
         serialization::readPodChecked(file, header.config.sourceFingerprint) &&
         serialization::readPodChecked(file, header.config.fontFingerprint);
}

bool hasValidLutSpan(const CacheHeader& header, size_t fileSize) {
  if (header.partial > 1 || header.lutOffset < kHeaderSize || header.lutOffset > fileSize) return false;
  return page_cache::lutFitsFile(header.pageCount, fileSize - header.lutOffset);
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
         serialization::writePodChecked(file_, totalBytes_) &&
         serialization::writePodChecked(file_, config_.sourceFingerprint) &&
         serialization::writePodChecked(file_, config_.fontFingerprint);
}

bool PageCache::writeMutableHeader(uint32_t pageCount, bool isPartial, uint32_t lutOffset, uint32_t bytesConsumed,
                                   uint32_t totalBytes, bool allowInjectedFailure) {
  const uint8_t partial = isPartial ? 1 : 0;
  if (!file_.seek(kPageCountOffset) || !serialization::writePodChecked(file_, pageCount)) return false;
#ifndef ARDUINO
  if (allowInjectedFailure && failHeaderCommitOnce_) {
    failHeaderCommitOnce_ = false;
    return false;
  }
#endif
  return serialization::writePodChecked(file_, partial) && serialization::writePodChecked(file_, lutOffset) &&
         serialization::writePodChecked(file_, bytesConsumed) && serialization::writePodChecked(file_, totalBytes);
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

  return writeMutableHeader(pageCount_, isPartial_, lutOffset, bytesConsumed_, totalBytes_);
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
  LOG_INF(TAG, "Loaded: %u pages, partial=%d", pageCount_, isPartial_);
  return true;
}

bool PageCache::create(ContentParser& parser, const RenderConfig& config, uint32_t maxPages, uint32_t skipPages,
                       const AbortCallback& shouldAbort) {
  const unsigned long startMs = millis();

  // For extends with existing pages, track the committed header so any failed
  // append can leave the previous cache readable.
  CacheHeader oldHeader;
  uint32_t oldLutOffset = 0;
  uint32_t oldPageCount = 0;
  std::vector<uint32_t> lut;

  if (skipPages > 0) {
    // Read LUT position from header (no vector allocation)
    {
      FsFile hdr;
      if (!SdMan.openFileForRead("CACHE", cachePath_, hdr)) {
        LOG_ERR(TAG, "Failed to read header for extend");
        return false;
      }
      const bool validHeader = readCacheHeader(hdr, oldHeader) && oldHeader.version == CACHE_FILE_VERSION &&
                               hasValidLutSpan(oldHeader, hdr.size()) && oldHeader.pageCount == skipPages;
      hdr.close();
      if (!validHeader) {
        LOG_ERR(TAG, "Invalid header for extend");
        return false;
      }
      oldLutOffset = oldHeader.lutOffset;
      oldPageCount = oldHeader.pageCount;
    }

    file_ = SdMan.open(cachePath_.c_str(), O_RDWR);
    if (!file_) {
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

  uint32_t parsedPages = 0;
  bool hitMaxPages = false;
  bool aborted = false;
  bool serializeFailed = false;

  bool success = parser.parsePages(
      [this, &lut, &hitMaxPages, &serializeFailed, &parsedPages, maxPages, skipPages](std::unique_ptr<Page> page) {
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
          serializeFailed = true;
          hitMaxPages = true;
          return;
        }
#endif
        if (!page->serialize(file_)) {
          LOG_ERR(TAG, "Failed to serialize page %u, stopping", pageCount_);
          serializeFailed = true;
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

  if (serializeFailed) {
    parser.reset();
    file_.close();
    if (skipPages == 0) {
      pageCount_ = 0;
      isPartial_ = false;
      bytesConsumed_ = 0;
      totalBytes_ = 0;
      SdMan.remove(cachePath_.c_str());
    } else {
      pageCount_ = oldHeader.pageCount;
      isPartial_ = oldHeader.partial != 0;
      lutOffset_ = oldHeader.lutOffset;
      bytesConsumed_ = oldHeader.bytesConsumed;
      totalBytes_ = oldHeader.totalBytes;
    }
    LOG_ERR(TAG, "Page serialization failed; previous cache state preserved");
    return false;
  }

  if ((!success && pageCount_ == 0) || aborted) {
    file_.close();
    parser.reset();
    if (skipPages == 0) {
      pageCount_ = 0;
      isPartial_ = false;
      bytesConsumed_ = 0;
      totalBytes_ = 0;
      SdMan.remove(cachePath_.c_str());
    } else {
      pageCount_ = oldHeader.pageCount;
      isPartial_ = oldHeader.partial != 0;
      lutOffset_ = oldHeader.lutOffset;
      bytesConsumed_ = oldHeader.bytesConsumed;
      totalBytes_ = oldHeader.totalBytes;
    }
    LOG_ERR(TAG, "Parsing failed or aborted with %u pages (extend=%d)", pageCount_, skipPages > 0);
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
      parser.reset();
      pageCount_ = oldHeader.pageCount;
      isPartial_ = oldHeader.partial != 0;
      lutOffset_ = oldHeader.lutOffset;
      bytesConsumed_ = oldHeader.bytesConsumed;
      totalBytes_ = oldHeader.totalBytes;
      return false;
    }
    // Append new entries and update header
    bool writeOk = true;
    for (const uint32_t pos : lut) {
      writeOk = writeOk && serialization::writePodChecked(file_, pos);
    }
    writeOk = writeOk && writeMutableHeader(pageCount_, isPartial_, newLutOffset, bytesConsumed_, totalBytes_) &&
              file_.sync();
    if (!writeOk) {
      const bool rollbackOk = writeMutableHeader(oldHeader.pageCount, oldHeader.partial != 0, oldHeader.lutOffset,
                                                 oldHeader.bytesConsumed, oldHeader.totalBytes, false) &&
                              file_.sync();
      file_.close();
      parser.reset();
      pageCount_ = oldHeader.pageCount;
      isPartial_ = oldHeader.partial != 0;
      lutOffset_ = oldHeader.lutOffset;
      bytesConsumed_ = oldHeader.bytesConsumed;
      totalBytes_ = oldHeader.totalBytes;
      if (!rollbackOk) SdMan.remove(cachePath_.c_str());
      return false;
    }
    lutOffset_ = newLutOffset;
    file_.close();
    LOG_INF(TAG, "Created in %lu ms: %u pages, partial=%d", millis() - startMs, pageCount_, isPartial_);
    return true;
  } else if (!writeLut(lut)) {
    file_.close();
    parser.reset();
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
    parser.reset();
    SdMan.remove(cachePath_.c_str());
    return false;
  }
  LOG_INF(TAG, "Created in %lu ms: %u pages, partial=%d", millis() - startMs, pageCount_, isPartial_);
  return true;
}

bool PageCache::extend(ContentParser& parser, uint16_t additionalPages, const AbortCallback& shouldAbort) {
  if (!isPartial_) {
    LOG_INF(TAG, "Cache is complete, nothing to extend");
    return true;
  }

  const uint16_t chunk = page_cache::extensionChunk(pageCount_, additionalPages);
  const uint32_t currentPages = pageCount_;
  if (chunk == 0) {
    LOG_ERR(TAG, "Cache reached the page-count limit (%u)", pageCount_);
    return false;
  }

  if (parser.canResume()) {
    // HOT PATH: Parser has live session from previous extend, just append new pages.
    // No re-parsing — O(chunk) work instead of O(totalPages).
    // Uses disk-to-disk LUT copy with small buffer to avoid large heap allocations
    // that fragment memory on spines with thousands of pages.
    LOG_INF(TAG, "Hot extend from %u pages (+%u)", currentPages, static_cast<unsigned>(chunk));

    bool opened = false;
    for (int attempt = 0; attempt < 3; attempt++) {
      if (attempt > 0) delay(50);
      file_ = SdMan.open(cachePath_.c_str(), O_RDWR);
      if (file_) {
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
    const auto restoreCommittedState = [this, &header]() {
      pageCount_ = header.pageCount;
      isPartial_ = header.partial != 0;
      lutOffset_ = header.lutOffset;
      bytesConsumed_ = header.bytesConsumed;
      totalBytes_ = header.totalBytes;
    };

    if (!file_.seekEnd()) {
      file_.close();
      return false;
    }

    const uint32_t pagesBefore = pageCount_;
    const uint32_t oldPageCount = pageCount_;
    uint32_t newOffsets[50];
    uint16_t newCount = 0;
    bool hitMaxPages = false;
    bool serializeFailed = false;
    bool parseOk = parser.parsePages(
        [this, &newOffsets, &newCount, &hitMaxPages, &serializeFailed](std::unique_ptr<Page> page) {
          if (hitMaxPages || newCount >= 50) return;
          const uint32_t position = file_.position();
#ifndef ARDUINO
          if (failSerializeInterval_ > 0 && ++failSerializeCounter_ % failSerializeInterval_ == 0) {
            LOG_ERR(TAG, "Simulated serialize failure (page %u)", pageCount_);
            serializeFailed = true;
            hitMaxPages = true;
            return;
          }
#endif
          if (!page->serialize(file_)) {
            LOG_ERR(TAG, "Failed to serialize page %u, stopping", pageCount_);
            serializeFailed = true;
            hitMaxPages = true;
            return;
          }
          newOffsets[newCount++] = position;
          pageCount_++;
        },
        chunk, shouldAbort);

    if (serializeFailed) {
      parser.reset();
      file_.close();
      restoreCommittedState();
      LOG_ERR(TAG, "Page serialization failed; previous cache state preserved");
      return false;
    }

    if (!page_cache::hotExtendShouldCommit(parseOk, pageCount_ != pagesBefore)) {
      parser.reset();
      file_.close();
      LOG_ERR(TAG, "Hot extend failed with no new pages");
      return false;
    }

    isPartial_ = page_cache::hotExtendIsPartial(parseOk, parser.hasMoreContent());
    bytesConsumed_ = parser.bytesConsumed();
    totalBytes_ = parser.totalBytes();

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
          parser.reset();
          LOG_ERR(TAG, "LUT copy seek failed at %u", srcPos);
          file_.close();
          restoreCommittedState();
          return false;
        }
        const size_t n = file_.read(buf, toRead);
        if (n != toRead || !file_.seek(dstPos) || file_.write(buf, n) != n) {
          parser.reset();
          LOG_ERR(TAG, "LUT copy failed at %u", srcPos);
          file_.close();
          restoreCommittedState();
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

    writeOk = writeOk && writeMutableHeader(pageCount_, isPartial_, newLutOffset, bytesConsumed_, totalBytes_) &&
              file_.sync();
    if (!writeOk) {
      const bool rollbackOk = writeMutableHeader(header.pageCount, header.partial != 0, header.lutOffset,
                                                 header.bytesConsumed, header.totalBytes, false) &&
                              file_.sync();
      file_.close();
      parser.reset();
      restoreCommittedState();
      if (!rollbackOk) SdMan.remove(cachePath_.c_str());
      return false;
    }
    lutOffset_ = newLutOffset;
    file_.close();
    LOG_INF(TAG, "Hot extend done: %u pages, partial=%d", pageCount_, isPartial_);
    return true;
  }

  // COLD PATH: Fresh parser (after exit/reboot) — re-parse from start, skip
  // cached pages, then append the next chunk. This is slower than hot resume,
  // but it must remain correct for interrupted large books.
  const uint32_t targetPages = pageCount_ + chunk;
  LOG_INF(TAG, "Cold extend from %u to %u pages", currentPages, targetPages);

  parser.reset();
  bool result = create(parser, config_, targetPages, currentPages, shouldAbort);

  // No forward progress AND parser has no more content → content is truly finished.
  // Without the hasMoreContent() check, an aborted extend (timeout/memory pressure)
  // would permanently mark the chapter as complete, truncating it.
  if (result && pageCount_ <= currentPages && !parser.hasMoreContent()) {
    LOG_INF(TAG, "No progress during extend (%u pages), marking complete", pageCount_);
    isPartial_ = false;
  }

  return result;
}

std::unique_ptr<Page> PageCache::loadPage(uint32_t pageNum) {
  if (pageNum >= pageCount_) {
    LOG_ERR(TAG, "Page %u out of range (max %u)", pageNum, pageCount_);
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

    // Read this page's start and the following page's start. The next LUT
    // entry (or LUT start for the final page) is the exact record boundary.
    if (!file_.seek(lutOffset + static_cast<size_t>(pageNum) * sizeof(uint32_t))) {
      file_.close();
      continue;
    }
    uint32_t pagePos = 0;
    if (!serialization::readPodChecked(file_, pagePos)) {
      file_.close();
      continue;
    }
    uint32_t pageEnd = lutOffset;
    if (pageNum + 1 < header.pageCount && !serialization::readPodChecked(file_, pageEnd)) {
      file_.close();
      continue;
    }

    if (pagePos < kHeaderSize || pagePos >= pageEnd || pageEnd > lutOffset) {
      LOG_ERR(TAG, "Invalid page span: %u..%u (LUT offset: %u)", pagePos, pageEnd, lutOffset);
      file_.close();
      continue;
    }

    if (!file_.seek(pagePos)) {
      file_.close();
      continue;
    }
    auto page = Page::deserialize(file_);
    const uint32_t recordEnd = file_.position();
    file_.close();

    if (page && recordEnd <= pageEnd) return page;
    if (page) {
      LOG_ERR(TAG, "Page %u record overruns boundary: ended at %u, boundary %u", pageNum, recordEnd, pageEnd);
    }
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
