#include "PageCache.h"

#include <Logging.h>

#define TAG "CACHE"

#include <Page.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include "ContentParser.h"

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

  // Read existing LUT entries
  file_.seek(lutOffset_);
  lut.reserve(pageCount_);
  for (uint16_t i = 0; i < pageCount_; i++) {
    uint32_t pos;
    serialization::readPod(file_, pos);
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

  std::vector<uint32_t> lut;

  if (skipPages > 0) {
    // Extending: load existing LUT
    if (!loadLut(lut)) {
      LOG_ERR(TAG, "Failed to load existing LUT for extend");
      return false;
    }

    // Append new pages AFTER old LUT (crash-safe: old LUT remains valid until header update)
    if (!file_.open(cachePath_.c_str(), O_RDWR)) {
      LOG_ERR(TAG, "Failed to open cache file for append");
      return false;
    }
    file_.seekEnd();  // Append after old LUT
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
        if (!page->serialize(file_)) {
          LOG_ERR(TAG, "Failed to serialize page %d", pageCount_);
          return;
        }

        lut.push_back(position);
        pageCount_++;
        LOG_DBG(TAG, "Page %d cached", pageCount_ - 1);

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
    // Remove file to prevent corrupt/incomplete cache
    SdMan.remove(cachePath_.c_str());
    LOG_ERR(TAG, "Parsing failed or aborted with %d pages", pageCount_);
    return false;
  }

  isPartial_ = parser.hasMoreContent();
  bytesConsumed_ = parser.bytesConsumed();
  totalBytes_ = parser.totalBytes();

  if (!writeLut(lut)) {
    file_.close();
    SdMan.remove(cachePath_.c_str());
    return false;
  }

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
    LOG_INF(TAG, "Hot extend from %d pages (+%d)", currentPages, chunk);

    std::vector<uint32_t> lut;
    if (!loadLut(lut)) return false;

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
    file_.seekEnd();

    const uint16_t pagesBefore = pageCount_;
    bool parseOk = parser.parsePages(
        [this, &lut](std::unique_ptr<Page> page) {
          const uint32_t position = file_.position();
          if (!page->serialize(file_)) return;
          lut.push_back(position);
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

    if (!writeLut(lut)) {
      file_.close();
      SdMan.remove(cachePath_.c_str());
      return false;
    }

    file_.close();
    LOG_INF(TAG, "Hot extend done: %d pages, partial=%d", pageCount_, isPartial_);
    return true;
  }

  // COLD PATH: Fresh parser (after exit/reboot) — re-parse from start, skip cached pages.
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

bool PageCache::clear() const {
  if (!SdMan.exists(cachePath_.c_str())) {
    return true;
  }
  return SdMan.remove(cachePath_.c_str());
}
