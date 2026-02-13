#pragma once

#include <Epub/RenderConfig.h>
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
};
