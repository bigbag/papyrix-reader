#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Page;
class GfxRenderer;
struct RenderConfig;

/**
 * Callback type for checking if operation should abort.
 * Used by background tasks to support cooperative cancellation.
 * Returns true if caller should stop work and return early.
 */
using AbortCallback = std::function<bool()>;

/**
 * Abstract interface for content parsers.
 * Implementations parse content (EPUB HTML, TXT, Markdown) into Page objects.
 */
class ContentParser {
 public:
  virtual ~ContentParser() = default;

  /**
   * Parse content and emit pages via callback.
   * @param onPageComplete Called for each completed page
   * @param maxPages Maximum pages to parse (0 = unlimited)
   * @param shouldAbort Optional callback to check for cancellation (called periodically)
   * @return true if parsing completed successfully (may be partial if maxPages hit or aborted)
   */
  virtual bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages = 0,
                          const AbortCallback& shouldAbort = nullptr) = 0;

  /**
   * Check if there's more content to parse after a partial parse.
   * @return true if more content available
   */
  virtual bool hasMoreContent() const = 0;

  /**
   * Check if this parser can resume from where it left off (hot extend).
   * Returns true when internal state allows continuing without re-parsing.
   * @return true if parsePages() will continue from last position
   */
  virtual bool canResume() const { return false; }

  /**
   * Reset parser to start from beginning.
   * Call this before re-parsing to extend cache.
   */
  virtual void reset() = 0;

  /**
   * Get anchor-to-page mapping (element id → page index).
   * Only meaningful for EPUB parsers; returns empty for other formats.
   */
  virtual const std::vector<std::pair<std::string, uint16_t>>& getAnchorMap() const {
    static const std::vector<std::pair<std::string, uint16_t>> empty;
    return empty;
  }

  /**
   * Bytes consumed from the source by the parser so far. Used to extrapolate
   * total page count while the cache is still partial. Returns 0 when not
   * tracked (e.g. EpubChapterParser, where pages are chapter-relative).
   */
  virtual uint32_t bytesConsumed() const { return 0; }

  /**
   * Total source bytes available to this parser (file size for whole-file
   * parsers, chapter size for chaptered parsers). 0 if unknown.
   */
  virtual uint32_t totalBytes() const { return 0; }
};
