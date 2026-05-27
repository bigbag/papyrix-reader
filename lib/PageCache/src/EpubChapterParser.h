#pragma once

#include <Epub.h>
#include <RenderConfig.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ContentParser.h"

class ChapterHtmlSlimParser;
class GfxRenderer;

/**
 * Content parser for EPUB chapters.
 * Wraps ChapterHtmlSlimParser to implement ContentParser interface.
 * Supports incremental parsing: keeps parser alive between parsePages() calls
 * so subsequent extends don't re-parse from the beginning.
 */
class EpubChapterParser : public ContentParser {
  std::shared_ptr<Epub> epub_;
  int spineIndex_;
  GfxRenderer& renderer_;
  RenderConfig config_;
  std::string imageCachePath_;
  bool hasMore_ = true;

  // Persistent parser state for incremental parsing (hot extend)
  std::unique_ptr<ChapterHtmlSlimParser> liveParser_;
  std::string tmpHtmlPath_;
  std::string normalizedPath_;
  std::string parseHtmlPath_;
  std::string chapterBasePath_;
  bool initialized_ = false;

  // Callback state shared between init and resume paths.
  // The liveParser_'s completePageFn captures `this` and delegates to these members,
  // so the callback can be rewired between parsePages() calls without recreating the parser.
  std::function<void(std::unique_ptr<Page>)> onPageComplete_;
  uint16_t maxPages_ = 0;
  uint16_t pagesCreated_ = 0;
  bool hitMaxPages_ = false;

  // Captured anchor map from parser (persisted after liveParser_ is destroyed)
  std::vector<std::pair<std::string, uint16_t>> anchorMap_;

  // Sub-section chaining for on-demand spine splitting
  int currentSubSection_ = 0;
  int totalSubSections_ = 0;
  int subSectionPageOffset_ = 0;
  uint16_t currentSubSectionPages_ = 0;

  void cleanupTempFiles();

 public:
  EpubChapterParser(std::shared_ptr<Epub> epub, int spineIndex, GfxRenderer& renderer, const RenderConfig& config,
                    const std::string& imageCachePath = "");
  ~EpubChapterParser() override;

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages = 0,
                  const AbortCallback& shouldAbort = nullptr) override;
  bool hasMoreContent() const override { return hasMore_; }
  bool canResume() const override { return initialized_ && liveParser_ != nullptr; }
  void reset() override;
  const std::vector<std::pair<std::string, uint16_t>>& getAnchorMap() const override;
};
