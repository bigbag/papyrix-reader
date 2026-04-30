#include "HtmlParser.h"

#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <Html5Normalizer.h>
#include <Logging.h>
#include <Page.h>
#include <SDCardManager.h>

#define TAG "HTML_PARSE"

#include <utility>

HtmlParser::HtmlParser(std::string filepath, std::string cacheDir, GfxRenderer& renderer, const RenderConfig& config)
    : filepath_(std::move(filepath)), cacheDir_(std::move(cacheDir)), renderer_(renderer), config_(config) {}

HtmlParser::~HtmlParser() {
  liveParser_.reset();
  cleanupTempFiles();
}

void HtmlParser::cleanupTempFiles() {
  if (!normalizedPath_.empty()) {
    SdMan.remove(normalizedPath_.c_str());
    normalizedPath_.clear();
  }
}

void HtmlParser::reset() {
  liveParser_.reset();
  cleanupTempFiles();
  initialized_ = false;
  hasMore_ = true;
  anchorMap_.clear();
}

const std::vector<std::pair<std::string, uint16_t>>& HtmlParser::getAnchorMap() const {
  if (liveParser_) {
    return liveParser_->getAnchorMap();
  }
  return anchorMap_;
}

uint32_t HtmlParser::bytesConsumed() const { return liveParser_ ? liveParser_->bytesRead() : 0; }

uint32_t HtmlParser::totalBytes() const { return liveParser_ ? liveParser_->totalSize() : 0; }

bool HtmlParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                            const AbortCallback& shouldAbort) {
  // RESUME PATH
  if (initialized_ && liveParser_ && liveParser_->isSuspended()) {
    onPageComplete_ = onPageComplete;
    maxPages_ = maxPages;
    pagesCreated_ = 0;
    hitMaxPages_ = false;

    bool success = liveParser_->resumeParsing();

    hasMore_ = liveParser_->isSuspended() || liveParser_->wasAborted() || (!success && pagesCreated_ > 0);

    if (!liveParser_->isSuspended()) {
      anchorMap_ = liveParser_->getAnchorMap();
      liveParser_.reset();
      cleanupTempFiles();
      initialized_ = false;
      renderer_.clearWidthCache();
    }

    return success || pagesCreated_ > 0;
  }

  // INIT PATH: normalize HTML, create parser
  // Derive base path for resolving relative resources
  std::string chapterBasePath;
  {
    size_t lastSlash = filepath_.rfind('/');
    if (lastSlash != std::string::npos) {
      chapterBasePath = filepath_.substr(0, lastSlash + 1);
    }
  }

  // Normalize HTML5 void elements for Expat parser
  std::string parseHtmlPath = filepath_;
  normalizedPath_ = cacheDir_ + "/.norm_tmp.html";

  if (html5::normalizeVoidElements(filepath_, normalizedPath_)) {
    parseHtmlPath = normalizedPath_;
  } else {
    normalizedPath_.clear();
  }

  // Set up callback state
  onPageComplete_ = onPageComplete;
  maxPages_ = maxPages;
  pagesCreated_ = 0;
  hitMaxPages_ = false;

  auto wrappedCallback = [this](std::unique_ptr<Page> page) -> bool {
    if (hitMaxPages_) return false;

    onPageComplete_(std::move(page));
    pagesCreated_++;

    if (maxPages_ > 0 && pagesCreated_ >= maxPages_) {
      hitMaxPages_ = true;
      return false;
    }
    return true;
  };

  // No readItemFn (standalone HTML — images not extracted from ZIP)
  // No cssParser (standalone HTML — no external CSS)
  liveParser_.reset(new ChapterHtmlSlimParser(parseHtmlPath, renderer_, config_, wrappedCallback, nullptr,
                                              chapterBasePath, "", nullptr, nullptr, shouldAbort));

  bool success = liveParser_->parseAndBuildPages();
  initialized_ = true;

  hasMore_ = liveParser_->isSuspended() || liveParser_->wasAborted() || (!success && pagesCreated_ > 0);

  if (!liveParser_->isSuspended()) {
    anchorMap_ = liveParser_->getAnchorMap();
    liveParser_.reset();
    cleanupTempFiles();
    initialized_ = false;
    renderer_.clearWidthCache();
  }

  return success || pagesCreated_ > 0;
}
