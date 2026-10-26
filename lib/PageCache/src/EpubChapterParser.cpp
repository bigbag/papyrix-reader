#include "EpubChapterParser.h"

#include <BuildArena.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <Html5Normalizer.h>
#include <Hyphenation.h>
#include <Logging.h>
#include <Page.h>
#include <ParserScratch.h>
#include <SDCardManager.h>
#include <core/PerfLog.h>
#include <esp_heap_caps.h>

#define TAG "EPUB_CHAP"

#include <utility>

namespace {
class ScratchReporter {
 public:
  ScratchReporter(const char* tag, BuildArena& arena, uint32_t started)
      : tag_(tag),
        arena_(arena),
        started_(started),
        freeBefore_(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
        largestBefore_(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)) {}

  ~ScratchReporter() {
    LOG_DBG(tag_,
            "Scratch high=%zu/%zu failed=%zu fallbacks=%u scope_failures=%u "
            "heap=%zu->%zu largest=%zu->%zu elapsed=%lu ms",
            arena_.highWater(), arena_.capacity(), arena_.failedAllocSize(), arena_.fallbackCount(),
            arena_.scopeFailureCount(), freeBefore_, heap_caps_get_free_size(MALLOC_CAP_8BIT), largestBefore_,
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), static_cast<unsigned long>(millis() - started_));
  }

 private:
  const char* tag_;
  BuildArena& arena_;
  uint32_t started_;
  size_t freeBefore_;
  size_t largestBefore_;
};
}  // namespace

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

const std::vector<std::pair<std::string, uint32_t>>& EpubChapterParser::getAnchorMap() const {
  return liveParser_ ? liveParser_->getAnchorMap() : anchorMap_;
}

bool EpubChapterParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint32_t maxPages,
                                   const AbortCallback& shouldAbort) {
  const uint32_t scratchStarted = millis();
  ParserScratch scratchBuffer(renderer_.getFrameBuffer(), renderer_.getBufferSize());
  BuildArena scratch(scratchBuffer.data(), scratchBuffer.capacity());
  ScratchReporter scratchReporter(TAG, scratch, scratchStarted);
  activeDict_ = scratchBuffer.data();
  struct ClearDict {
    uint8_t*& slot;
    ~ClearDict() { slot = nullptr; }
  } clearDict{activeDict_};

  onPageComplete_ = onPageComplete;
  maxPages_ = maxPages;
  pagesCreated_ = 0;
  hitMaxPages_ = false;
  if (shouldAbort && shouldAbort()) return false;
  Hyphenation::setLanguage(epub_->getLanguage());

  const bool resume = initialized_ && liveParser_ && liveParser_->isSuspended();
  if (!resume) {
    if (!epub_->prepareCssStyles(config_.sourceFingerprint, activeDict_, &scratch, shouldAbort)) return false;
    const auto localPath = epub_->getSpineItem(spineIndex_).href;
    if (localPath.empty() || localPath.front() == '/') return false;
    const size_t lastSlash = localPath.rfind('/');
    chapterBasePath_ = lastSlash == std::string::npos ? "" : localPath.substr(0, lastSlash + 1);
    parseHtmlPath_.clear();
    size_t itemSize = 0;
    if (epub_->getItemSize(localPath, &itemSize) && itemSize > Epub::MIN_CACHED_CHAPTER_SIZE) {
      epub_->prepareChapterHtml(spineIndex_, config_.sourceFingerprint, parseHtmlPath_, activeDict_, &scratch,
                                shouldAbort);
    }
    if (shouldAbort && shouldAbort()) return false;

    if (parseHtmlPath_.empty()) {
      tmpHtmlPath_ = epub_->getCachePath() + "/.tmp_" + std::to_string(spineIndex_) + ".html";
      bool extracted = false;
      for (int attempt = 0; attempt < 3 && !extracted; ++attempt) {
        if (shouldAbort && shouldAbort()) return false;
        if (attempt > 0) {
          LOG_ERR(TAG, "Retrying stream (attempt %d)...", attempt + 1);
          delay(50);
        }
        SdMan.remove(tmpHtmlPath_.c_str());
        FsFile tmpHtml;
        if (!SdMan.openFileForWrite("EPUB", tmpHtmlPath_, tmpHtml)) continue;
        const uint32_t extractionStarted = perfMsNow();
        extracted = epub_->readItemContentsToStream(localPath, tmpHtml, 1024, activeDict_, &scratch, shouldAbort) &&
                    tmpHtml.sync();
        readerPerfLog("epub-extract", extractionStarted);
        tmpHtml.close();
        if (!extracted) SdMan.remove(tmpHtmlPath_.c_str());
      }
      if (!extracted || (shouldAbort && shouldAbort())) return false;

      normalizedPath_ = epub_->getCachePath() + "/.norm_" + std::to_string(spineIndex_) + ".html";
      parseHtmlPath_ = tmpHtmlPath_;
      const uint32_t normalizationStarted = perfMsNow();
      if (html5::normalizeHtmlForXml(tmpHtmlPath_, normalizedPath_, &scratch, shouldAbort)) {
        parseHtmlPath_ = normalizedPath_;
      }
      readerPerfLog("epub-normalize", normalizationStarted);
      if (shouldAbort && shouldAbort()) return false;
    }

    auto readItemFn = [this](const std::string& href, Print& out, size_t chunkSize, BuildArena* arena) -> bool {
      return epub_->readItemContentsToStream(href, out, chunkSize, activeDict_, arena);
    };
    auto wrappedCallback = [this](std::unique_ptr<Page> page) -> bool {
      if (hitMaxPages_) return false;
      onPageComplete_(std::move(page));
      ++pagesCreated_;
      hitMaxPages_ = maxPages_ > 0 && pagesCreated_ >= maxPages_;
      return !hitMaxPages_;
    };
    liveParser_.reset(new ChapterHtmlSlimParser(parseHtmlPath_, renderer_, config_, wrappedCallback, nullptr,
                                                chapterBasePath_, imageCachePath_, readItemFn, epub_->getCssParser(),
                                                shouldAbort));
    initialized_ = true;
  }

  liveParser_->setExternalAbortCallback(shouldAbort);
  liveParser_->setBuildScratch(&scratch);
  const bool success = resume ? liveParser_->resumeParsing() : liveParser_->parseAndBuildPages();
  liveParser_->setBuildScratch(nullptr);
  hasMore_ = liveParser_->isSuspended() || liveParser_->wasAborted() || !success;
  if (!liveParser_->isSuspended()) {
    anchorMap_ = liveParser_->getAnchorMap();
    liveParser_.reset();
    cleanupTempFiles();
    initialized_ = false;
    renderer_.clearWidthCache();
  }
  return success || pagesCreated_ > 0;
}
