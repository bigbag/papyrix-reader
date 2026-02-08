#include "EpubChapterParser.h"

#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <Html5Normalizer.h>
#include <SDCardManager.h>

#include <utility>

EpubChapterParser::EpubChapterParser(std::shared_ptr<Epub> epub, int spineIndex, GfxRenderer& renderer,
                                     const RenderConfig& config, const std::string& imageCachePath)
    : epub_(std::move(epub)),
      spineIndex_(spineIndex),
      renderer_(renderer),
      config_(config),
      imageCachePath_(imageCachePath) {}

void EpubChapterParser::reset() { hasMore_ = true; }

bool EpubChapterParser::parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint16_t maxPages,
                                   const AbortCallback& shouldAbort) {
  const auto localPath = epub_->getSpineItem(spineIndex_).href;
  const auto tmpHtmlPath = epub_->getCachePath() + "/.tmp_" + std::to_string(spineIndex_) + ".html";

  // Derive chapter base path for resolving relative image paths
  std::string chapterBasePath;
  {
    size_t lastSlash = localPath.rfind('/');
    if (lastSlash != std::string::npos) {
      chapterBasePath = localPath.substr(0, lastSlash + 1);
    }
  }

  // Stream HTML to temp file
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      Serial.printf("[EPUB] Retrying stream (attempt %d)...\n", attempt + 1);
      delay(50);
    }

    if (SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
    }

    FsFile tmpHtml;
    if (!SdMan.openFileForWrite("EPUB", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    success = epub_->readItemContentsToStream(localPath, tmpHtml, 4096);
    fileSize = tmpHtml.size();
    tmpHtml.close();

    if (!success && SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
    }
  }

  if (!success) {
    Serial.printf("[EPUB] Failed to stream HTML to temp file\n");
    return false;
  }

  // Normalize HTML5 void elements for Expat parser
  const auto normalizedPath = epub_->getCachePath() + "/.norm_" + std::to_string(spineIndex_) + ".html";
  std::string parseHtmlPath = tmpHtmlPath;
  if (html5::normalizeVoidElements(tmpHtmlPath, normalizedPath)) {
    parseHtmlPath = normalizedPath;
  }

  // Create read callback for extracting images from EPUB
  auto readItemFn = [this](const std::string& href, Print& out, size_t chunkSize) -> bool {
    return epub_->readItemContentsToStream(href, out, chunkSize);
  };

  // Track pages for early termination
  uint16_t pagesCreated = 0;
  bool hitMaxPages = false;

  auto wrappedCallback = [&](std::unique_ptr<Page> page) -> bool {
    if (hitMaxPages) return false;  // Signal parser to stop

    onPageComplete(std::move(page));
    pagesCreated++;

    if (maxPages > 0 && pagesCreated >= maxPages) {
      hitMaxPages = true;
      return false;  // Signal parser to stop
    }
    return true;  // Continue parsing
  };

  ChapterHtmlSlimParser parser(parseHtmlPath, renderer_, config_, wrappedCallback, nullptr, chapterBasePath,
                               imageCachePath_, readItemFn, epub_->getCssParser(), shouldAbort);

  success = parser.parseAndBuildPages();

  // Clean up temp files
  SdMan.remove(tmpHtmlPath.c_str());
  SdMan.remove(normalizedPath.c_str());

  // Clear word width cache
  renderer_.clearWidthCache();

  // More content remains if we hit the page limit OR the parser was internally aborted
  // (timeout/memory). Without this, an aborted parse marks the chapter as complete.
  hasMore_ = hitMaxPages || parser.wasAborted() || (!success && pagesCreated > 0);
  return success || pagesCreated > 0;
}
