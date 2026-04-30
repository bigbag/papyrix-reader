#pragma once

#include <RenderConfig.h>
#include <SdFat.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ContentParser.h"  // For AbortCallback

class ContentParser;
class GfxRenderer;
class Page;

/**
 * Unified page cache for all content types (EPUB, TXT, Markdown).
 * Supports partial caching - only caches N pages at a time.
 */
class PageCache {
 public:
  // Default number of pages to cache initially
  static constexpr uint16_t DEFAULT_CACHE_CHUNK = 5;
  // Extend cache when within this many pages of the end
  static constexpr uint16_t EXTEND_THRESHOLD = 3;

 private:
  std::string cachePath_;
  FsFile file_;
  uint16_t pageCount_ = 0;
  bool isPartial_ = false;
  RenderConfig config_;
  uint32_t lutOffset_ = 0;  // Cached LUT offset for extend operations
  // Last sample of parser progress, used to extrapolate the total page count
  // while the cache is partial (see estimatedTotalPages()). Persisted in the
  // cache header so cold-loaded partial caches can show an estimate without
  // waiting for the next extend.
  uint32_t bytesConsumed_ = 0;
  uint32_t totalBytes_ = 0;

  bool writeHeader(bool isPartial);
  bool writeLut(const std::vector<uint32_t>& lut);
  bool loadLut(std::vector<uint32_t>& lut);  // Load existing LUT for extend

 public:
  explicit PageCache(std::string cachePath);
  ~PageCache() = default;

  /**
   * Try to load existing cache from disk.
   * @param config Render config to validate against
   * @return true if valid cache exists and was loaded
   */
  bool load(const RenderConfig& config);

  /**
   * Load cache header without config validation (for dump/debug tools).
   * @return true if valid cache exists and header was read
   */
  bool loadRaw();

  /**
   * Create cache by parsing content.
   * @param parser Content parser to use
   * @param config Render config
   * @param maxPages Maximum pages to cache (0 = unlimited)
   * @param skipPages Skip serializing first N pages (for extend)
   * @param shouldAbort Optional callback to check for cancellation
   * @return true on success
   */
  bool create(ContentParser& parser, const RenderConfig& config, uint16_t maxPages = DEFAULT_CACHE_CHUNK,
              uint16_t skipPages = 0, const AbortCallback& shouldAbort = nullptr);

  /**
   * Extend cache with more pages.
   * Re-parses content but skips already-cached pages, then appends new pages.
   * @param parser Content parser (will be reset)
   * @param additionalPages Number of additional pages to cache
   * @param shouldAbort Optional callback to check for cancellation
   * @return true on success
   */
  bool extend(ContentParser& parser, uint16_t additionalPages = DEFAULT_CACHE_CHUNK,
              const AbortCallback& shouldAbort = nullptr);

  /**
   * Load a specific page from cache.
   * @param pageNum Page number (0-indexed)
   * @return Page object or nullptr on error
   */
  std::unique_ptr<Page> loadPage(uint16_t pageNum);

  /**
   * Clear cache from disk.
   * @return true on success
   */
  bool clear() const;

  // Accessors
  uint16_t pageCount() const { return pageCount_; }
  bool isPartial() const { return isPartial_; }
  bool needsExtension(uint16_t currentPage) const { return isPartial_ && currentPage >= pageCount_ - EXTEND_THRESHOLD; }
  const std::string& path() const { return cachePath_; }

  // Suppress the worst early-skew sample (description page + first chunk).
  // 3 cached pages is enough that the body/metadata ratio has stabilized.
  static constexpr uint16_t kMinPagesForEstimate = 3;

  /**
   * Extrapolate the total page count from the parser's source-byte progress
   * captured during the last create()/extend(). Returns 0 when no sample is
   * available yet (e.g. EpubChapterParser, or fewer than kMinPagesForEstimate
   * cached pages). Caller falls back to pageCount() in that case.
   * Thread-safety: shares the same access pattern as pageCount_/isPartial_;
   * ReaderState's ownership model (stopBackgroundCaching before render)
   * already serializes background-task writes against main-thread reads.
   */
  uint32_t estimatedTotalPages() const {
    if (pageCount_ < kMinPagesForEstimate) return 0;
    if (bytesConsumed_ == 0 || totalBytes_ == 0) return 0;
    if (bytesConsumed_ >= totalBytes_) return pageCount_;
    return static_cast<uint32_t>(static_cast<uint64_t>(totalBytes_) * pageCount_ / bytesConsumed_);
  }
};
