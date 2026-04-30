#pragma once

#include <RenderConfig.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ContentParser.h"

class ChapterHtmlSlimParser;
class GfxRenderer;

class HtmlParser : public ContentParser {
  std::string filepath_;
  std::string cacheDir_;
  GfxRenderer& renderer_;
  RenderConfig config_;
  bool hasMore_ = true;

  std::unique_ptr<ChapterHtmlSlimParser> liveParser_;
  std::string normalizedPath_;
  bool initialized_ = false;

  std::function<void(std::unique_ptr<Page>)> onPageComplete_;
  uint16_t maxPages_ = 0;
  uint16_t pagesCreated_ = 0;
  bool hitMaxPages_ = false;

  std::vector<std::pair<std::string, uint16_t>> anchorMap_;

  void cleanupTempFiles();

 public:
  HtmlParser(std::string filepath, std::string cacheDir, GfxRenderer& renderer, const RenderConfig& config);
  ~HtmlParser() override;

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages = 0,
                  const AbortCallback& shouldAbort = nullptr) override;
  bool hasMoreContent() const override { return hasMore_; }
  bool canResume() const override { return initialized_ && liveParser_ != nullptr; }
  void reset() override;
  const std::vector<std::pair<std::string, uint16_t>>& getAnchorMap() const override;
  uint32_t bytesConsumed() const override;
  uint32_t totalBytes() const override;
};
