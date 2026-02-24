#include "EpubChapterParser.h"

#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <Html5Normalizer.h>
#include <Hyphenation.h>
#include <Logging.h>
#include <Page.h>
#include <SDCardManager.h>

#define TAG "EPUB_CHAP"

#include <utility>

EpubChapterParser::EpubChapterParser(std::shared_ptr<Epub> epub, int spineIndex, GfxRenderer& renderer,
                                     const RenderConfig& config, const std::string& imageCachePath)
    : epub_(std::move(epub)),
      spineIndex_(spineIndex),
      renderer_(renderer),
      config_(config),
      imageCachePath_(imageCachePath) {}

EpubChapterParser::~EpubChapterParser() {
  liveParser_.reset();
  cleanupTempFiles();
}

void EpubChapterParser::cleanupTempFiles() {
  if (!tmpHtmlPath_.empty()) {
    SdMan.remove(tmpHtmlPath_.c_str());
    tmpHtmlPath_.clear();
  }
  if (!normalizedPath_.empty()) {
    SdMan.remove(normalizedPath_.c_str());
    normalizedPath_.clear();
  }
}

void EpubChapterParser::reset() {
  liveParser_.reset();
  cleanupTempFiles();
  initialized_ = false;
  hasMore_ = true;
  parseHtmlPath_.clear();
  chapterBasePath_.clear();
  anchorMap_.clear();
}

const std::vector<std::pair<std::string, uint16_t>>& EpubChapterParser::getAnchorMap() const {
  if (liveParser_) {
    return liveParser_->getAnchorMap();
  }
  return anchorMap_;
}

bool EpubChapterParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                                   const AbortCallback& shouldAbort) {
  // RESUME PATH: parser is alive from a previous call, just resume.
  // The liveParser_'s completePageFn captures `this` and delegates to member state
  // (onPageComplete_, maxPages_, etc.), so we just update those for the new batch.
  if (initialized_ && liveParser_ && liveParser_->isSuspended()) {
    Hyphenation::setLanguage(epub_->getLanguage());
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

  // INIT PATH: first call — extract HTML, normalize, create parser
  // Set up hyphenation language from EPUB metadata
  Hyphenation::setLanguage(epub_->getLanguage());

  const auto localPath = epub_->getSpineItem(spineIndex_).href;
  tmpHtmlPath_ = epub_->getCachePath() + "/.tmp_" + std::to_string(spineIndex_) + ".html";

  // Derive chapter base path for resolving relative image paths
  {
    size_t lastSlash = localPath.rfind('/');
    if (lastSlash != std::string::npos) {
      chapterBasePath_ = localPath.substr(0, lastSlash + 1);
    } else {
      chapterBasePath_.clear();
    }
  }

  // Stream HTML to temp file
  bool success = false;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      LOG_ERR(TAG, "Retrying stream (attempt %d)...", attempt + 1);
      delay(50);
    }

    if (SdMan.exists(tmpHtmlPath_.c_str())) {
      SdMan.remove(tmpHtmlPath_.c_str());
    }

    FsFile tmpHtml;
    if (!SdMan.openFileForWrite("EPUB", tmpHtmlPath_, tmpHtml)) {
      continue;
    }
    // Reuse frame buffer (48KB) as ZIP decompression dict (32KB) — safe because
    // the background task owns the renderer and display isn't active during parsing
    success = epub_->readItemContentsToStream(localPath, tmpHtml, 1024, renderer_.getFrameBuffer());
    tmpHtml.close();

    if (!success && SdMan.exists(tmpHtmlPath_.c_str())) {
      SdMan.remove(tmpHtmlPath_.c_str());
    }
  }

  if (!success) {
    LOG_ERR(TAG, "Failed to stream HTML to temp file");
    return false;
  }

  // Normalize HTML5 void elements for Expat parser
  normalizedPath_ = epub_->getCachePath() + "/.norm_" + std::to_string(spineIndex_) + ".html";
  parseHtmlPath_ = tmpHtmlPath_;
  if (html5::normalizeVoidElements(tmpHtmlPath_, normalizedPath_)) {
    parseHtmlPath_ = normalizedPath_;
  }

  // Create read callback for extracting images from EPUB
  auto readItemFn = [this](const std::string& href, Print& out, size_t chunkSize) -> bool {
    return epub_->readItemContentsToStream(href, out, chunkSize);
  };

  // Set up callback state for this batch
  onPageComplete_ = onPageComplete;
  maxPages_ = maxPages;
  pagesCreated_ = 0;
  hitMaxPages_ = false;

  // Create the parser with a callback that references our member state
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

  liveParser_.reset(new ChapterHtmlSlimParser(parseHtmlPath_, renderer_, config_, wrappedCallback, nullptr,
                                              chapterBasePath_, imageCachePath_, readItemFn, epub_->getCssParser(),
                                              shouldAbort));

  success = liveParser_->parseAndBuildPages();
  initialized_ = true;

  hasMore_ = liveParser_->isSuspended() || liveParser_->wasAborted() || (!success && pagesCreated_ > 0);

  // If parser finished (not suspended), clean up
  if (!liveParser_->isSuspended()) {
    anchorMap_ = liveParser_->getAnchorMap();
    liveParser_.reset();
    cleanupTempFiles();
    initialized_ = false;
    renderer_.clearWidthCache();
  }

  return success || pagesCreated_ > 0;
}
