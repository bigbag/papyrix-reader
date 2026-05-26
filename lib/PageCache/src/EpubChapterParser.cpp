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
  currentSubSection_ = 0;
  totalSubSections_ = 0;
  subSectionPageOffset_ = 0;
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
      if (totalSubSections_ > 0 && currentSubSection_ < totalSubSections_ - 1) {
        // Merge anchor map from completed sub-section with page offset
        const auto& subAnchors = liveParser_->getAnchorMap();
        for (const auto& anchor : subAnchors) {
          anchorMap_.emplace_back(anchor.first, anchor.second + static_cast<uint16_t>(subSectionPageOffset_));
        }
        subSectionPageOffset_ += pagesCreated_;
        currentSubSection_++;
        liveParser_.reset();
        cleanupTempFiles();
        initialized_ = false;
        renderer_.clearWidthCache();
        hasMore_ = true;
        return success || pagesCreated_ > 0;
      }

      if (totalSubSections_ > 0) {
        // Final sub-section — merge anchors with offset
        const auto& subAnchors = liveParser_->getAnchorMap();
        for (const auto& anchor : subAnchors) {
          anchorMap_.emplace_back(anchor.first, anchor.second + static_cast<uint16_t>(subSectionPageOffset_));
        }
      } else {
        anchorMap_ = liveParser_->getAnchorMap();
      }
      liveParser_.reset();
      cleanupTempFiles();
      initialized_ = false;
      renderer_.clearWidthCache();
    }

    return success || pagesCreated_ > 0;
  }

  // INIT PATH: first call — extract HTML, normalize, create parser
  Hyphenation::setLanguage(epub_->getLanguage());

  auto localPath = epub_->getSpineItem(spineIndex_).href;
  bool isVirtualSection = localPath.find("/sections/") != std::string::npos;

  // On-demand spine splitting: detect or trigger splitting for oversized items
  if (!isVirtualSection && totalSubSections_ == 0) {
    int vsCount = epub_->getVirtualSectionCount(spineIndex_);
    if (vsCount > 0) {
      totalSubSections_ = vsCount;
      currentSubSection_ = 0;
    } else {
      size_t itemSize = 0;
      if (epub_->getItemSize(localPath, &itemSize) && itemSize > Epub::MAX_SECTION_SIZE) {
        if (epub_->splitSingleSpineItem(spineIndex_, renderer_.getFrameBuffer())) {
          vsCount = epub_->getVirtualSectionCount(spineIndex_);
          if (vsCount > 0) {
            totalSubSections_ = vsCount;
            currentSubSection_ = 0;
          }
        }
      }
    }
  }
  if (totalSubSections_ > 0) {
    localPath = epub_->getVirtualSectionPath(spineIndex_, currentSubSection_);
    isVirtualSection = true;
  }

  if (isVirtualSection) {
    // Virtual section: pre-extracted file on SD card (from spine splitting).
    // No ZIP decompression or normalization needed — already done during extraction.
    parseHtmlPath_ = localPath;
    tmpHtmlPath_.clear();
    normalizedPath_.clear();

    // Derive chapter base path from original EPUB content path for image resolution.
    // Prefer the .base sidecar file (exact match for this split group), fall back to
    // walking the spine backwards for backward compatibility with older caches.
    chapterBasePath_.clear();
    {
      size_t sectionsPos = localPath.rfind("/sections/");
      if (sectionsPos != std::string::npos) {
        size_t nameStart = sectionsPos + 10;  // past "/sections/"
        size_t underscore = localPath.find('_', nameStart);
        if (underscore != std::string::npos) {
          std::string origIdx = localPath.substr(nameStart, underscore - nameStart);
          const std::string bpFile = epub_->getCachePath() + "/sections/" + origIdx + ".base";
          FsFile bp;
          if (SdMan.openFileForRead("ECP", bpFile, bp)) {
            char buf[256];
            const size_t n = bp.read(reinterpret_cast<uint8_t*>(buf), sizeof(buf) - 1);
            if (n > 0) {
              buf[n] = '\0';
              chapterBasePath_ = buf;
            }
            bp.close();
          }
        }
      }
    }

    if (chapterBasePath_.empty()) {
      for (int si = spineIndex_; si >= 0; si--) {
        const auto entry = epub_->getSpineItem(si);
        if (entry.href.find("/sections/") == std::string::npos) {
          size_t lastSlash = entry.href.rfind('/');
          if (lastSlash != std::string::npos) {
            chapterBasePath_ = entry.href.substr(0, lastSlash + 1);
          }
          break;
        }
      }
    }
  } else {
    // Normal spine item: decompress from EPUB ZIP
    tmpHtmlPath_ = epub_->getCachePath() + "/.tmp_" + std::to_string(spineIndex_) + ".html";

    {
      size_t lastSlash = localPath.rfind('/');
      if (lastSlash != std::string::npos) {
        chapterBasePath_ = localPath.substr(0, lastSlash + 1);
      } else {
        chapterBasePath_.clear();
      }
    }

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

    normalizedPath_ = epub_->getCachePath() + "/.norm_" + std::to_string(spineIndex_) + ".html";
    parseHtmlPath_ = tmpHtmlPath_;
    if (html5::normalizeVoidElements(tmpHtmlPath_, normalizedPath_)) {
      parseHtmlPath_ = normalizedPath_;
    }
  }

  // Create read callback for extracting images from EPUB
  auto readItemFn = [this](const std::string& href, Print& out, size_t chunkSize) -> bool {
    return epub_->readItemContentsToStream(href, out, chunkSize, renderer_.getFrameBuffer());
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

  bool success = liveParser_->parseAndBuildPages();
  initialized_ = true;

  hasMore_ = liveParser_->isSuspended() || liveParser_->wasAborted() || (!success && pagesCreated_ > 0);

  // If parser finished (not suspended), handle sub-section chaining or clean up
  if (!liveParser_->isSuspended()) {
    if (totalSubSections_ > 0 && currentSubSection_ < totalSubSections_ - 1) {
      const auto& subAnchors = liveParser_->getAnchorMap();
      for (const auto& anchor : subAnchors) {
        anchorMap_.emplace_back(anchor.first, anchor.second + static_cast<uint16_t>(subSectionPageOffset_));
      }
      subSectionPageOffset_ += pagesCreated_;
      currentSubSection_++;
      liveParser_.reset();
      cleanupTempFiles();
      initialized_ = false;
      renderer_.clearWidthCache();
      hasMore_ = true;
      return success || pagesCreated_ > 0;
    }

    if (totalSubSections_ > 0) {
      const auto& subAnchors = liveParser_->getAnchorMap();
      for (const auto& anchor : subAnchors) {
        anchorMap_.emplace_back(anchor.first, anchor.second + static_cast<uint16_t>(subSectionPageOffset_));
      }
    } else {
      anchorMap_ = liveParser_->getAnchorMap();
    }
    liveParser_.reset();
    cleanupTempFiles();
    initialized_ = false;
    renderer_.clearWidthCache();
  }

  return success || pagesCreated_ > 0;
}
