#include "ReaderState.h"

#include <Arduino.h>
#include <ContentParser.h>
#include <CoverHelpers.h>
#include <Epub/Page.h>
#include <EpubChapterParser.h>
#include <Fb2.h>
#include <Fb2Parser.h>
#include <GfxRenderer.h>
#include <MarkdownParser.h>
#include <PageCache.h>
#include <PlainTextParser.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <esp_system.h>

#include <cstring>

#include "../Battery.h"
#include "../FontManager.h"
#include "../config.h"
#include "../content/ProgressManager.h"
#include "../content/ReaderNavigation.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

namespace papyrix {

static constexpr int kCacheTaskStackSize = 12288;
static constexpr int kCacheTaskStopTimeoutMs = 10000;  // 10s - generous for slow SD operations

namespace {
constexpr int horizontalPadding = 5;
constexpr int statusBarMargin = 23;

// Cache path helpers
inline std::string epubSectionCachePath(const std::string& epubCachePath, int spineIndex) {
  return epubCachePath + "/sections/" + std::to_string(spineIndex) + ".bin";
}

inline std::string contentCachePath(const char* cacheDir, int fontId) {
  return std::string(cacheDir) + "/pages_" + std::to_string(fontId) + ".bin";
}
}  // namespace

void ReaderState::saveAnchorMap(const ContentParser& parser, const std::string& cachePath) {
  const auto& anchors = parser.getAnchorMap();

  std::string anchorPath = cachePath + ".anchors";
  FsFile file;
  if (!SdMan.openFileForWrite("RDR", anchorPath, file)) return;

  if (anchors.size() > UINT16_MAX) {
    uint16_t zero = 0;
    serialization::writePod(file, zero);
    file.close();
    return;
  }
  uint16_t count = static_cast<uint16_t>(anchors.size());
  serialization::writePod(file, count);
  for (const auto& entry : anchors) {
    serialization::writeString(file, entry.first);
    serialization::writePod(file, entry.second);
  }
  file.close();
}

int ReaderState::loadAnchorPage(const std::string& cachePath, const std::string& anchor) {
  std::string anchorPath = cachePath + ".anchors";
  FsFile file;
  if (!SdMan.openFileForRead("RDR", anchorPath, file)) return -1;

  uint16_t count;
  if (!serialization::readPodChecked(file, count)) {
    file.close();
    return -1;
  }

  for (uint16_t i = 0; i < count; i++) {
    std::string anchorId;
    uint16_t page;
    if (!serialization::readString(file, anchorId) || !serialization::readPodChecked(file, page)) {
      file.close();
      return -1;
    }
    if (anchorId == anchor) {
      file.close();
      return page;
    }
  }

  file.close();
  return -1;
}

std::vector<std::pair<std::string, uint16_t>> ReaderState::loadAnchorMap(const std::string& cachePath) {
  std::vector<std::pair<std::string, uint16_t>> anchors;
  std::string anchorPath = cachePath + ".anchors";
  FsFile file;
  if (!SdMan.openFileForRead("RDR", anchorPath, file)) return anchors;

  uint16_t count;
  if (serialization::readPodChecked(file, count)) {
    for (uint16_t i = 0; i < count; i++) {
      std::string anchorId;
      uint16_t page;
      if (!serialization::readString(file, anchorId) || !serialization::readPodChecked(file, page)) break;
      anchors.emplace_back(std::move(anchorId), page);
    }
  }
  file.close();
  return anchors;
}

int ReaderState::calcFirstContentSpine(bool hasCover, int textStartIndex, size_t spineCount) {
  if (hasCover && textStartIndex == 0 && spineCount > 1) {
    return 1;
  }
  return textStartIndex;
}

// Cache creation/extension implementation
// Called from main thread when background task is NOT running (ownership model)
// No mutex needed - main thread owns pageCache_/parser_ when task is stopped
void ReaderState::createOrExtendCacheImpl(ContentParser& parser, const std::string& cachePath,
                                          const RenderConfig& config) {
  bool needsCreate = false;
  bool needsExtend = false;

  if (!pageCache_) {
    pageCache_.reset(new PageCache(cachePath));
    if (pageCache_->load(config)) {
      if (!SdMan.exists((cachePath + ".anchors").c_str())) {
        needsCreate = true;  // Migration: rebuild cache to generate anchor map
      } else {
        needsExtend = pageCache_->isPartial();
      }
    } else {
      needsCreate = true;
    }
  } else {
    if (!SdMan.exists((cachePath + ".anchors").c_str())) {
      needsCreate = true;  // Migration: rebuild cache to generate anchor map
    } else {
      needsExtend = pageCache_->isPartial();
    }
  }

  if (pageCache_) {
    if (needsExtend) {
      pageCache_->extend(parser, PageCache::DEFAULT_CACHE_CHUNK);
      saveAnchorMap(parser, cachePath);
    } else if (needsCreate) {
      parser.reset();  // Ensure clean state for fresh cache creation
      pageCache_->create(parser, config, PageCache::DEFAULT_CACHE_CHUNK);
      saveAnchorMap(parser, cachePath);
    }
  }
}

// Background caching implementation (handles stop request checks)
// Called from background task - uses BackgroundTask's shouldStop() and getAbortCallback()
// Ownership: background task owns pageCache_/parser_ while running
void ReaderState::backgroundCacheImpl(ContentParser& parser, const std::string& cachePath, const RenderConfig& config) {
  auto shouldAbort = cacheTask_.getAbortCallback();

  // Check for early abort before doing anything
  if (cacheTask_.shouldStop()) {
    Serial.println("[READER] Background cache aborted before start");
    return;
  }

  // Create/load cache (we own pageCache_ while task is running)
  pageCache_.reset(new PageCache(cachePath));
  bool loaded = pageCache_->load(config);
  // Migration: rebuild cache to generate anchor map if missing
  if (loaded && !SdMan.exists((cachePath + ".anchors").c_str())) {
    loaded = false;
  }
  bool needsExtend = loaded && pageCache_->needsExtension(currentSectionPage_);

  // Check for abort after setup
  if (cacheTask_.shouldStop()) {
    pageCache_.reset();
    Serial.println("[READER] Background cache aborted after setup");
    return;
  }

  if (!loaded || needsExtend) {
    bool success;
    if (needsExtend) {
      success = pageCache_->extend(parser, PageCache::DEFAULT_CACHE_CHUNK, shouldAbort);
    } else {
      parser.reset();  // Ensure clean state for fresh cache creation
      success = pageCache_->create(parser, config, PageCache::DEFAULT_CACHE_CHUNK, 0, shouldAbort);
    }

    if (success && !cacheTask_.shouldStop()) {
      saveAnchorMap(parser, cachePath);
    }

    if (!success || cacheTask_.shouldStop()) {
      Serial.println("[READER] Cache creation failed or aborted, clearing pageCache");
      pageCache_.reset();
    }
  }
}

ReaderState::ReaderState(GfxRenderer& renderer)
    : renderer_(renderer),
      xtcRenderer_(renderer),
      currentPage_(0),
      needsRender_(true),
      contentLoaded_(false),
      currentSpineIndex_(0),
      currentSectionPage_(0),
      pagesUntilFullRefresh_(1),
      tocView_{} {
  contentPath_[0] = '\0';
}

ReaderState::~ReaderState() { stopBackgroundCaching(); }

void ReaderState::setContentPath(const char* path) {
  if (path) {
    strncpy(contentPath_, path, sizeof(contentPath_) - 1);
    contentPath_[sizeof(contentPath_) - 1] = '\0';
  } else {
    contentPath_[0] = '\0';
  }
}

void ReaderState::enter(Core& core) {
  // Free memory from other states before loading book
  THEME_MANAGER.clearCache();
  renderer_.clearWidthCache();

  contentLoaded_ = false;
  loadFailed_ = false;
  needsRender_ = true;
  stopBackgroundCaching();  // Ensure any previous task is stopped
  parser_.reset();          // Safe - task is stopped
  parserSpineIndex_ = -1;
  pageCache_.reset();
  currentSpineIndex_ = 0;
  currentSectionPage_ = 0;  // Will be set to -1 after progress load if at start

  // Read path from shared buffer if not already set
  if (contentPath_[0] == '\0' && core.buf.path[0] != '\0') {
    strncpy(contentPath_, core.buf.path, sizeof(contentPath_) - 1);
    contentPath_[sizeof(contentPath_) - 1] = '\0';
    core.buf.path[0] = '\0';
  }

  // Determine source state from boot transition
  const auto& transition = getTransition();
  sourceState_ =
      (transition.isValid() && transition.returnTo == ReturnTo::FILE_MANAGER) ? StateId::FileList : StateId::Home;

  Serial.printf("[READER] Entering with path: %s\n", contentPath_);

  if (contentPath_[0] == '\0') {
    Serial.println("[READER] No content path set");
    return;
  }

  // Apply orientation setting to renderer
  switch (core.settings.orientation) {
    case Settings::Portrait:
      renderer_.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case Settings::LandscapeCW:
      renderer_.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case Settings::Inverted:
      renderer_.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case Settings::LandscapeCCW:
      renderer_.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      renderer_.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
  }

  // Open content using ContentHandle
  auto result = core.content.open(contentPath_, PAPYRIX_CACHE_DIR);
  if (!result.ok()) {
    Serial.printf("[READER] Failed to open content: %s\n", errorToString(result.err));
    // Store error message for ErrorState to display
    snprintf(core.buf.text, sizeof(core.buf.text), "Cannot open file:\n%s", errorToString(result.err));
    loadFailed_ = true;  // Mark as failed for update() to transition to error state
    return;
  }

  contentLoaded_ = true;

  // Save last book path to settings
  strncpy(core.settings.lastBookPath, contentPath_, sizeof(core.settings.lastBookPath) - 1);
  core.settings.lastBookPath[sizeof(core.settings.lastBookPath) - 1] = '\0';
  core.settings.save(core.storage);

  // Setup cache directories for all content types
  // Reset state for new book
  textStartIndex_ = 0;
  hasCover_ = false;
  thumbnailDone_ = false;
  switch (core.content.metadata().type) {
    case ContentType::Epub: {
      auto* provider = core.content.asEpub();
      if (provider && provider->getEpub()) {
        const auto* epub = provider->getEpub();
        epub->setupCacheDir();
        // Get the spine index for the first text content (from <guide> element)
        textStartIndex_ = epub->getSpineIndexForTextReference();
        Serial.printf("[READER] Text starts at spine index %d\n", textStartIndex_);
      }
      break;
    }
    case ContentType::Txt: {
      auto* provider = core.content.asTxt();
      if (provider && provider->getTxt()) {
        provider->getTxt()->setupCacheDir();
      }
      break;
    }
    case ContentType::Markdown: {
      auto* provider = core.content.asMarkdown();
      if (provider && provider->getMarkdown()) {
        provider->getMarkdown()->setupCacheDir();
      }
      break;
    }
    case ContentType::Fb2: {
      auto* provider = core.content.asFb2();
      if (provider && provider->getFb2()) {
        provider->getFb2()->setupCacheDir();
      }
      break;
    }
    default:
      break;
  }

  // Load saved progress
  ContentType type = core.content.metadata().type;
  auto progress = ProgressManager::load(core, core.content.cacheDir(), type);
  progress = ProgressManager::validate(core, type, progress);
  currentSpineIndex_ = progress.spineIndex;
  currentSectionPage_ = progress.sectionPage;
  currentPage_ = progress.flatPage;

  // If at start of book and showImages enabled, begin at cover
  if (currentSpineIndex_ == 0 && currentSectionPage_ == 0 && core.settings.showImages) {
    currentSectionPage_ = -1;  // Cover page
  }

  // Initialize last rendered to loaded position (until first render)
  lastRenderedSpineIndex_ = currentSpineIndex_;
  lastRenderedSectionPage_ = currentSectionPage_;

  Serial.printf("[READER] Loaded: %s\n", core.content.metadata().title);

  // Start background caching (includes thumbnail generation)
  // This runs once per book open regardless of starting position
  startBackgroundCaching(core);
}

void ReaderState::exit(Core& core) {
  Serial.println("[READER] Exiting");

  // Stop background caching task first - BackgroundTask::stop() waits properly
  stopBackgroundCaching();

  if (contentLoaded_) {
    // Save progress at last rendered position (not current requested position)
    ProgressManager::Progress progress;
    // If on cover, save as (0, 0) - cover is implicit start
    progress.spineIndex = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSpineIndex_;
    progress.sectionPage = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSectionPage_;
    progress.flatPage = currentPage_;
    ProgressManager::save(core, core.content.cacheDir(), core.content.metadata().type, progress);

    // Safe to reset - task is stopped, we own pageCache_/parser_
    parser_.reset();
    parserSpineIndex_ = -1;
    pageCache_.reset();
    core.content.close();
  }

  // Unload custom reader fonts to free memory
  // Note: device may restart after this (dual-boot system), but explicit cleanup
  // ensures predictable memory behavior and better logging
  FONT_MANAGER.unloadReaderFonts();

  contentLoaded_ = false;
  contentPath_[0] = '\0';

  // Reset orientation to Portrait for UI
  renderer_.setOrientation(GfxRenderer::Orientation::Portrait);
}

StateTransition ReaderState::update(Core& core) {
  // Handle load failure - transition to error state or back to file list
  if (loadFailed_ || !contentLoaded_) {
    // If error message was set, show ErrorState; otherwise just go back to FileList
    if (core.buf.text[0] != '\0') {
      return StateTransition::to(StateId::Error);
    }
    return StateTransition::to(StateId::FileList);
  }

  Event e;
  while (core.events.pop(e)) {
    // Route input to TOC handler when in TOC mode
    if (tocMode_) {
      handleTocInput(core, e);
      continue;
    }

    switch (e.type) {
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Right:
          case Button::Down:
            navigateNext(core);
            break;

          case Button::Left:
          case Button::Up:
            navigatePrev(core);
            break;

          case Button::Center:
            if (core.content.tocCount() > 0) {
              enterTocMode(core);
            }
            break;
          case Button::Back:
            exitToUI(core);
            // Won't reach here after restart
            return StateTransition::stay(StateId::Reader);
          case Button::Power:
            if (core.settings.shortPwrBtn == Settings::PowerPageTurn) {
              navigateNext(core);
            }
            break;
        }
        break;

      default:
        break;
    }
  }

  return StateTransition::stay(StateId::Reader);
}

void ReaderState::render(Core& core) {
  if (!needsRender_ || !contentLoaded_) {
    return;
  }

  if (tocMode_) {
    renderTocOverlay(core);
  } else {
    renderCurrentPage(core);
    // Track last successfully rendered position for progress saving
    lastRenderedSpineIndex_ = currentSpineIndex_;
    lastRenderedSectionPage_ = currentSectionPage_;
  }

  needsRender_ = false;
}

void ReaderState::navigateNext(Core& core) {
  // Stop background task before accessing pageCache_ (ownership model)
  stopBackgroundCaching();

  ContentType type = core.content.metadata().type;

  // XTC uses flatPage navigation, not spine/section - skip to navigation logic
  if (type == ContentType::Xtc) {
    ReaderNavigation::Position pos;
    pos.flatPage = currentPage_;
    auto result = ReaderNavigation::next(type, pos, nullptr, core.content.pageCount());
    applyNavResult(result, core);
    return;
  }

  // Spine/section logic for EPUB, TXT, Markdown
  // From cover (-1) -> first text content page
  if (currentSpineIndex_ == 0 && currentSectionPage_ == -1) {
    auto* provider = core.content.asEpub();
    size_t spineCount = 1;
    if (provider && provider->getEpub()) {
      spineCount = provider->getEpub()->getSpineItemsCount();
    }
    int firstContentSpine = calcFirstContentSpine(hasCover_, textStartIndex_, spineCount);

    if (firstContentSpine != currentSpineIndex_) {
      currentSpineIndex_ = firstContentSpine;
      parser_.reset();
      parserSpineIndex_ = -1;
      pageCache_.reset();
    }
    currentSectionPage_ = 0;
    needsRender_ = true;
    startBackgroundCaching(core);
    return;
  }

  ReaderNavigation::Position pos;
  pos.spineIndex = currentSpineIndex_;
  pos.sectionPage = currentSectionPage_;
  pos.flatPage = currentPage_;
  auto result = ReaderNavigation::next(type, pos, pageCache_.get(), core.content.pageCount());
  applyNavResult(result, core);
}

void ReaderState::navigatePrev(Core& core) {
  // Stop background task before accessing pageCache_ (ownership model)
  stopBackgroundCaching();

  ContentType type = core.content.metadata().type;

  // XTC uses flatPage navigation, not spine/section - skip to navigation logic
  if (type == ContentType::Xtc) {
    ReaderNavigation::Position pos;
    pos.flatPage = currentPage_;
    auto result = ReaderNavigation::prev(type, pos, nullptr);
    applyNavResult(result, core);
    return;
  }

  // Spine/section logic for EPUB, TXT, Markdown
  auto* provider = core.content.asEpub();
  size_t spineCount = 1;
  if (provider && provider->getEpub()) {
    spineCount = provider->getEpub()->getSpineItemsCount();
  }
  int firstContentSpine = calcFirstContentSpine(hasCover_, textStartIndex_, spineCount);

  // At first page of text content
  if (currentSpineIndex_ == firstContentSpine && currentSectionPage_ == 0) {
    // Only go to cover if it exists and images enabled
    if (hasCover_ && core.settings.showImages) {
      currentSpineIndex_ = 0;
      currentSectionPage_ = -1;
      parser_.reset();
      parserSpineIndex_ = -1;
      pageCache_.reset();  // Don't need cache for cover
      needsRender_ = true;
    }
    return;  // At start of book either way
  }

  // Prevent going back from cover
  if (currentSpineIndex_ == 0 && currentSectionPage_ == -1) {
    startBackgroundCaching(core);  // Resume task before returning
    return;                        // Already at cover
  }

  ReaderNavigation::Position pos;
  pos.spineIndex = currentSpineIndex_;
  pos.sectionPage = currentSectionPage_;
  pos.flatPage = currentPage_;
  auto result = ReaderNavigation::prev(type, pos, pageCache_.get());
  applyNavResult(result, core);
}

void ReaderState::applyNavResult(const ReaderNavigation::NavResult& result, Core& core) {
  currentSpineIndex_ = result.position.spineIndex;
  currentSectionPage_ = result.position.sectionPage;
  currentPage_ = result.position.flatPage;
  needsRender_ = result.needsRender;
  if (result.needsCacheReset) {
    parser_.reset();  // Safe - task already stopped by caller
    parserSpineIndex_ = -1;
    pageCache_.reset();
  }
  startBackgroundCaching(core);  // Resume caching
}

void ReaderState::renderCurrentPage(Core& core) {
  ContentType type = core.content.metadata().type;
  const Theme& theme = THEME_MANAGER.current();

  // Always clear screen first (prevents previous content from showing through)
  renderer_.clearScreen(theme.backgroundColor);

  // Cover page: spineIndex=0, sectionPage=-1 (only when showImages enabled)
  if (currentSpineIndex_ == 0 && currentSectionPage_ == -1) {
    if (core.settings.showImages) {
      if (renderCoverPage(core)) {
        hasCover_ = true;
        core.display.markDirty();
        return;
      }
      // No cover - skip spine 0 if textStartIndex is 0 (likely empty cover document)
      hasCover_ = false;
      currentSectionPage_ = 0;
      if (textStartIndex_ == 0) {
        // Only skip to spine 1 if it exists
        auto* provider = core.content.asEpub();
        if (provider && provider->getEpub()) {
          const auto* epub = provider->getEpub();
          if (epub->getSpineItemsCount() > 1) {
            currentSpineIndex_ = 1;
          }
        }
      }
      // Fall through to render content
    } else {
      currentSectionPage_ = 0;
    }
  }

  switch (type) {
    case ContentType::Epub:
    case ContentType::Txt:
    case ContentType::Markdown:
    case ContentType::Fb2:
      renderCachedPage(core);
      break;
    case ContentType::Xtc:
      renderXtcPage(core);
      break;
    default:
      break;
  }

  if (!cacheTask_.isRunning() && (!pageCache_ || !thumbnailDone_)) {
    startBackgroundCaching(core);
  }

  core.display.markDirty();
}

void ReaderState::renderCachedPage(Core& core) {
  Theme& theme = THEME_MANAGER.mutableCurrent();
  ContentType type = core.content.metadata().type;
  const auto vp = getReaderViewport(core.settings.statusBar != 0);

  // Handle EPUB bounds
  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) return;

    auto epub = provider->getEpubShared();
    if (currentSpineIndex_ < 0) currentSpineIndex_ = 0;
    if (currentSpineIndex_ >= static_cast<int>(epub->getSpineItemsCount())) {
      renderer_.drawCenteredText(core.settings.getReaderFontId(theme), 300, "End of book", theme.primaryTextBlack,
                                 BOLD);
      renderer_.displayBuffer();
      return;
    }
  }

  // Stop background task to ensure we own pageCache_ (ownership model)
  stopBackgroundCaching();

  // Background task may have left parser in inconsistent state
  if (!pageCache_ && parser_ && parserSpineIndex_ == currentSpineIndex_) {
    parser_.reset();
    parserSpineIndex_ = -1;
  }

  // Create or load cache if needed
  if (!pageCache_) {
    // Try to load existing cache silently first
    loadCacheFromDisk(core);

    bool pageIsCached =
        pageCache_ && currentSectionPage_ >= 0 && currentSectionPage_ < static_cast<int>(pageCache_->pageCount());

    if (!pageIsCached) {
      // Current page not cached - show "Indexing..." and create/extend
      renderer_.clearScreen(theme.backgroundColor);
      ui::centeredMessage(renderer_, theme, core.settings.getReaderFontId(theme), "Indexing...");
      renderer_.displayBuffer();

      createOrExtendCache(core);

      // Backward navigation: cache entire chapter to find actual last page
      if (currentSectionPage_ == INT16_MAX) {
        while (pageCache_ && pageCache_->isPartial()) {
          const size_t pagesBefore = pageCache_->pageCount();
          createOrExtendCache(core);
          if (pageCache_ && pageCache_->pageCount() <= pagesBefore) {
            break;  // No progress - avoid infinite loop
          }
        }
      }

      // Clear overlay
      renderer_.clearScreen(theme.backgroundColor);
    }

    // Clamp page number (handle negative values and out-of-bounds)
    if (pageCache_) {
      const int cachedPages = static_cast<int>(pageCache_->pageCount());
      if (currentSectionPage_ < 0) {
        currentSectionPage_ = 0;
      } else if (currentSectionPage_ >= cachedPages) {
        currentSectionPage_ = cachedPages > 0 ? cachedPages - 1 : 0;
      }
    }
  }

  // Check if we need to extend cache
  if (!ensurePageCached(core, currentSectionPage_)) {
    renderer_.drawCenteredText(core.settings.getReaderFontId(theme), 300, "Failed to load page", theme.primaryTextBlack,
                               BOLD);
    renderer_.displayBuffer();
    needsRender_ = false;  // Prevent infinite render loop on cache failure
    return;
  }

  // ensurePageCached may have used the frame buffer as ZIP decompression dictionary
  renderer_.clearScreen(theme.backgroundColor);

  // Load and render page (cache is now guaranteed to exist, we own it)
  size_t pageCount = pageCache_ ? pageCache_->pageCount() : 0;
  auto page = pageCache_ ? pageCache_->loadPage(currentSectionPage_) : nullptr;

  if (!page) {
    Serial.println("[READER] Failed to load page, clearing cache");
    if (pageCache_) {
      pageCache_->clear();
      pageCache_.reset();
    }
    needsRender_ = true;
    return;
  }

  const int fontId = core.settings.getReaderFontId(theme);

  renderPageContents(core, *page, vp.marginTop, vp.marginRight, vp.marginBottom, vp.marginLeft);
  renderStatusBar(core, vp.marginRight, vp.marginBottom, vp.marginLeft);

  const bool aaEnabled = core.settings.textAntiAliasing && !FONT_MANAGER.isUsingCustomReaderFont() &&
                         renderer_.fontSupportsGrayscale(fontId);
  const bool imagePageWithAA = aaEnabled && page->hasImages();

  if (imagePageWithAA) {
    // Double FAST_REFRESH with selective image blanking:
    // HALF_REFRESH sets e-ink particles too firmly for the grayscale LUT to adjust.
    // Instead, blank only the image area and do two fast refreshes (~1200ms total
    // vs ~1720ms for HALF_REFRESH) with better visual quality.
    const bool turnOffScreen = core.settings.sunlightFadingFix != 0;
    int16_t imgX, imgY, imgW, imgH;
    if (page->getImageBoundingBox(imgX, imgY, imgW, imgH)) {
      // Step 1: Display page with image area blanked (text appears, image area white)
      renderer_.fillRect(imgX + vp.marginLeft, imgY + vp.marginTop, imgW, imgH, !theme.primaryTextBlack);
      renderer_.displayBuffer(EInkDisplay::FAST_REFRESH, turnOffScreen);

      // Step 2: Re-render with images and display again (images appear clean)
      renderPageContents(core, *page, vp.marginTop, vp.marginRight, vp.marginBottom, vp.marginLeft);
      renderStatusBar(core, vp.marginRight, vp.marginBottom, vp.marginLeft);
      renderer_.displayBuffer(EInkDisplay::FAST_REFRESH, turnOffScreen);
    } else {
      renderer_.displayBuffer(EInkDisplay::HALF_REFRESH, turnOffScreen);
    }
    // Double FAST_REFRESH handles ghosting; don't count toward full refresh cadence
  } else {
    displayWithRefresh(core);
  }

  // Grayscale text rendering (anti-aliasing) - skip for custom fonts (saves ~48KB)
  if (aaEnabled && renderer_.storeBwBuffer()) {
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer_, fontId, vp.marginLeft, vp.marginTop, theme.primaryTextBlack);
    renderer_.copyGrayscaleLsbBuffers();

    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer_, fontId, vp.marginLeft, vp.marginTop, theme.primaryTextBlack);
    renderer_.copyGrayscaleMsbBuffers();

    const bool turnOffScreen = core.settings.sunlightFadingFix != 0;
    renderer_.displayGrayBuffer(turnOffScreen);
    renderer_.setRenderMode(GfxRenderer::BW);
    renderer_.restoreBwBuffer();
  }

  Serial.printf("[READER] Rendered page %d/%d\n", currentSectionPage_ + 1, pageCount);
}

bool ReaderState::ensurePageCached(Core& core, uint16_t pageNum) {
  // Caller must have stopped background task (we own pageCache_)
  if (!pageCache_) {
    return false;
  }

  // If page is already cached, we're good
  size_t pageCount = pageCache_->pageCount();
  bool needsExtension = pageCache_->needsExtension(pageNum);
  bool isPartial = pageCache_->isPartial();

  if (pageNum < pageCount) {
    // Check if we should pre-extend (approaching end of partial cache)
    if (needsExtension) {
      Serial.printf("[READER] Pre-extending cache at page %d\n", pageNum);
      createOrExtendCache(core);
    }
    return true;
  }

  // Page not cached yet - need to extend
  if (!isPartial) {
    Serial.printf("[READER] Page %d not available (cache complete at %d pages)\n", pageNum, pageCount);
    return false;
  }

  Serial.printf("[READER] Extending cache for page %d\n", pageNum);

  const Theme& theme = THEME_MANAGER.current();
  ui::centeredMessage(renderer_, theme, core.settings.getReaderFontId(theme), "Loading...");

  createOrExtendCache(core);

  pageCount = pageCache_ ? pageCache_->pageCount() : 0;
  return pageNum < pageCount;
}

void ReaderState::loadCacheFromDisk(Core& core) {
  const Theme& theme = THEME_MANAGER.current();
  ContentType type = core.content.metadata().type;

  const auto vp = getReaderViewport(core.settings.statusBar != 0);
  const auto config = core.settings.getRenderConfig(theme, vp.width, vp.height);

  std::string cachePath;
  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) {
      Serial.println("[READER] loadCacheFromDisk: no epub provider");
      return;
    }
    cachePath = epubSectionCachePath(provider->getEpub()->getCachePath(), currentSpineIndex_);
  } else if (type == ContentType::Markdown || type == ContentType::Txt || type == ContentType::Fb2) {
    cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
  } else {
    Serial.printf("[READER] loadCacheFromDisk: unsupported content type %d\n", static_cast<int>(type));
    return;
  }

  // Caller must have stopped background task (we own pageCache_)
  if (!pageCache_) {
    pageCache_.reset(new PageCache(cachePath));
    if (!pageCache_->load(config)) {
      pageCache_.reset();
    }
  }
}

void ReaderState::createOrExtendCache(Core& core) {
  const Theme& theme = THEME_MANAGER.current();
  ContentType type = core.content.metadata().type;

  const auto vp = getReaderViewport(core.settings.statusBar != 0);
  const auto config = core.settings.getRenderConfig(theme, vp.width, vp.height);

  std::string cachePath;
  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) return;
    auto epub = provider->getEpubShared();
    cachePath = epubSectionCachePath(epub->getCachePath(), currentSpineIndex_);

    // Create parser if we don't have one (or if spine changed)
    if (!parser_ || parserSpineIndex_ != currentSpineIndex_) {
      std::string imageCachePath = core.settings.showImages ? (epub->getCachePath() + "/images") : "";
      parser_.reset(new EpubChapterParser(epub, currentSpineIndex_, renderer_, config, imageCachePath));
      parserSpineIndex_ = currentSpineIndex_;
    }
  } else if (type == ContentType::Markdown) {
    cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
    if (!parser_) {
      parser_.reset(new MarkdownParser(contentPath_, renderer_, config));
      parserSpineIndex_ = 0;
    }
  } else if (type == ContentType::Fb2) {
    cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
    if (!parser_) {
      parser_.reset(new Fb2Parser(contentPath_, renderer_, config));
      parserSpineIndex_ = 0;
    }
  } else {
    cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
    if (!parser_) {
      parser_.reset(new PlainTextParser(contentPath_, renderer_, config));
      parserSpineIndex_ = 0;
    }
  }

  createOrExtendCacheImpl(*parser_, cachePath, config);
}

void ReaderState::renderPageContents(Core& core, Page& page, int marginTop, int marginRight, int marginBottom,
                                     int marginLeft) {
  (void)marginRight;
  (void)marginBottom;

  const Theme& theme = THEME_MANAGER.current();
  const int fontId = core.settings.getReaderFontId(theme);
  page.render(renderer_, fontId, marginLeft, marginTop, theme.primaryTextBlack);
}

void ReaderState::renderStatusBar(Core& core, int marginRight, int marginBottom, int marginLeft) {
  const Theme& theme = THEME_MANAGER.current();
  ContentType type = core.content.metadata().type;

  // Build status bar data
  ui::ReaderStatusBarData data{};
  data.mode = core.settings.statusBar;
  data.title = core.content.metadata().title;

  // Battery
  const uint16_t millivolts = batteryMonitor.readMillivolts();
  data.batteryPercent = (millivolts < 100) ? -1 : BatteryMonitor::percentageFromMillivolts(millivolts);

  // Page info
  // Note: renderCachedPage() already stopped the task, so we own pageCache_
  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (provider && provider->getEpub()) {
      data.currentPage = currentSectionPage_ + 1;
      if (pageCache_) {
        data.totalPages = pageCache_->pageCount();
        data.isPartial = pageCache_->isPartial();
      } else {
        data.isPartial = true;
      }
    } else {
      return;
    }
  } else {
    data.currentPage = currentSectionPage_ + 1;
    data.totalPages = core.content.pageCount();
  }

  ui::readerStatusBar(renderer_, theme, marginLeft, marginRight, marginBottom, data);
}

void ReaderState::renderXtcPage(Core& core) {
  auto* provider = core.content.asXtc();
  if (!provider) {
    return;
  }

  const Theme& theme = THEME_MANAGER.current();

  auto result = xtcRenderer_.render(provider->getParser(), currentPage_, [this, &core]() { displayWithRefresh(core); });

  switch (result) {
    case XtcPageRenderer::RenderResult::Success:
      break;
    case XtcPageRenderer::RenderResult::EndOfBook:
      ui::centeredMessage(renderer_, theme, theme.uiFontId, "End of book");
      break;
    case XtcPageRenderer::RenderResult::InvalidDimensions:
      ui::centeredMessage(renderer_, theme, theme.uiFontId, "Invalid file");
      break;
    case XtcPageRenderer::RenderResult::AllocationFailed:
      ui::centeredMessage(renderer_, theme, theme.uiFontId, "Memory error");
      break;
    case XtcPageRenderer::RenderResult::PageLoadFailed:
      ui::centeredMessage(renderer_, theme, theme.uiFontId, "Page load error");
      break;
  }
}

void ReaderState::displayWithRefresh(Core& core) {
  const bool turnOffScreen = core.settings.sunlightFadingFix != 0;
  if (pagesUntilFullRefresh_ <= 1) {
    renderer_.displayBuffer(EInkDisplay::HALF_REFRESH, turnOffScreen);
    pagesUntilFullRefresh_ = core.settings.getPagesPerRefreshValue();
  } else {
    renderer_.displayBuffer(EInkDisplay::FAST_REFRESH, turnOffScreen);
    pagesUntilFullRefresh_--;
  }
}

ReaderState::Viewport ReaderState::getReaderViewport(bool showStatusBar) const {
  Viewport vp{};
  renderer_.getOrientedViewableTRBL(&vp.marginTop, &vp.marginRight, &vp.marginBottom, &vp.marginLeft);
  vp.marginLeft += horizontalPadding;
  vp.marginRight += horizontalPadding;
  if (showStatusBar) {
    vp.marginBottom += statusBarMargin;
  }
  vp.width = renderer_.getScreenWidth() - vp.marginLeft - vp.marginRight;
  vp.height = renderer_.getScreenHeight() - vp.marginTop - vp.marginBottom;
  return vp;
}

bool ReaderState::renderCoverPage(Core& core) {
  Serial.printf("[%lu] [RDR] Generating cover for reader...\n", millis());
  std::string coverPath = core.content.generateCover(true);  // Always 1-bit in reader (saves ~48KB grayscale buffer)
  if (coverPath.empty()) {
    Serial.printf("[%lu] [RDR] No cover available, skipping cover page\n", millis());
    return false;
  }

  Serial.printf("[%lu] [RDR] Rendering cover page from: %s\n", millis(), coverPath.c_str());
  const auto vp = getReaderViewport(core.settings.statusBar != 0);
  int pagesUntilRefresh = pagesUntilFullRefresh_;
  const bool turnOffScreen = core.settings.sunlightFadingFix != 0;

  bool rendered = CoverHelpers::renderCoverFromBmp(renderer_, coverPath, vp.marginTop, vp.marginRight, vp.marginBottom,
                                                   vp.marginLeft, pagesUntilRefresh,
                                                   core.settings.getPagesPerRefreshValue(), turnOffScreen);

  // Force half refresh on next page to fully clear the cover image
  pagesUntilFullRefresh_ = 1;
  return rendered;
}

void ReaderState::startBackgroundCaching(Core& core) {
  // XTC content uses pre-rendered bitmaps — no page cache needed.
  // Generate cover + thumbnail synchronously since XTC has no background task.
  if (core.content.metadata().type == ContentType::Xtc) {
    if (!thumbnailDone_) {
      core.content.generateCover(true);
      core.content.generateThumbnail();
      thumbnailDone_ = true;
    }
    return;
  }

  // BackgroundTask handles safe restart via CAS loop
  if (cacheTask_.isRunning()) {
    Serial.println("[READER] Warning: Previous cache task still running, stopping first");
    stopBackgroundCaching();
  }

  Serial.println("[READER] Starting background page cache task");
  coreForCacheTask_ = &core;

  // Snapshot state for the background task
  const int sectionPage = currentSectionPage_;
  const int spineIndex = currentSpineIndex_;
  const bool coverExists = hasCover_;
  const int textStart = textStartIndex_;

  cacheTask_.start(
      "PageCache", kCacheTaskStackSize,
      [this, sectionPage, spineIndex, coverExists, textStart]() {
        const Theme& theme = THEME_MANAGER.current();
        Serial.println("[READER] Background cache task started");

        if (cacheTask_.shouldStop()) {
          Serial.println("[READER] Background cache task aborted (stop requested)");
          return;
        }

        Core* corePtr = coreForCacheTask_;
        if (!corePtr) {
          Serial.println("[READER] Background cache task aborted (no core)");
          return;
        }

        Core& coreRef = *corePtr;
        ContentType type = coreRef.content.metadata().type;

        // Build cache if it doesn't exist
        if (!pageCache_ && !cacheTask_.shouldStop()) {
          const auto vp = getReaderViewport(coreRef.settings.statusBar != 0);
          const auto config = coreRef.settings.getRenderConfig(theme, vp.width, vp.height);
          std::string cachePath;

          if (type == ContentType::Epub) {
            auto* provider = coreRef.content.asEpub();
            if (provider && provider->getEpub() && !cacheTask_.shouldStop()) {
              const auto* epub = provider->getEpub();
              std::string imageCachePath = coreRef.settings.showImages ? (epub->getCachePath() + "/images") : "";
              // When on cover page (sectionPage=-1), cache the first content spine
              int spineToCache = spineIndex;
              if (sectionPage == -1) {
                spineToCache = calcFirstContentSpine(coverExists, textStart, epub->getSpineItemsCount());
              }
              cachePath = epubSectionCachePath(epub->getCachePath(), spineToCache);

              if (!parser_ || parserSpineIndex_ != spineToCache) {
                parser_.reset(
                    new EpubChapterParser(provider->getEpubShared(), spineToCache, renderer_, config, imageCachePath));
                parserSpineIndex_ = spineToCache;
              }
            }
          } else if (type == ContentType::Markdown && !cacheTask_.shouldStop()) {
            cachePath = contentCachePath(coreRef.content.cacheDir(), config.fontId);
            if (!parser_) {
              parser_.reset(new MarkdownParser(contentPath_, renderer_, config));
              parserSpineIndex_ = 0;
            }
          } else if (type == ContentType::Fb2 && !cacheTask_.shouldStop()) {
            cachePath = contentCachePath(coreRef.content.cacheDir(), config.fontId);
            if (!parser_) {
              parser_.reset(new Fb2Parser(contentPath_, renderer_, config));
              parserSpineIndex_ = 0;
            }
          } else if (type == ContentType::Txt && !cacheTask_.shouldStop()) {
            cachePath = contentCachePath(coreRef.content.cacheDir(), config.fontId);
            if (!parser_) {
              parser_.reset(new PlainTextParser(contentPath_, renderer_, config));
              parserSpineIndex_ = 0;
            }
          }

          if (parser_ && !cachePath.empty() && !cacheTask_.shouldStop()) {
            backgroundCacheImpl(*parser_, cachePath, config);
          }
        }

        // Generate thumbnail from cover for HomeState (lower priority than page cache)
        // Only attempt once per book open — skip if already tried (success or failure)
        if (!thumbnailDone_ && !cacheTask_.shouldStop()) {
          coreRef.content.generateThumbnail();
          thumbnailDone_ = true;
        }

        if (!cacheTask_.shouldStop()) {
          Serial.println("[READER] Background cache task completed");
        } else {
          Serial.println("[READER] Background cache task stopped");
        }
      },
      0);  // priority 0 (idle)
}

void ReaderState::stopBackgroundCaching() {
  if (!cacheTask_.isRunning()) {
    return;
  }

  // BackgroundTask::stop() uses event-based waiting (no polling)
  // and NEVER force-deletes the task
  if (!cacheTask_.stop(kCacheTaskStopTimeoutMs)) {
    Serial.println("[READER] WARNING: Cache task did not stop within timeout");
    Serial.println("[READER] Task may be blocked on SD card I/O");
  }

  // Yield to allow FreeRTOS idle task to clean up the deleted task's TCB.
  // The background task self-deletes via vTaskDelete(NULL), but the idle task
  // must run to free its resources. Without this, parser_.reset() or
  // pageCache_.reset() can trigger mutex ownership violations
  // (assert failed: xQueueGenericSend queue.c:832).
  vTaskDelay(10 / portTICK_PERIOD_MS);
}

// ============================================================================
// TOC Overlay Mode
// ============================================================================

void ReaderState::enterTocMode(Core& core) {
  if (core.content.tocCount() == 0) {
    return;
  }

  // Stop background task before TOC overlay — both SD card I/O (thumbnail)
  // and e-ink display update share the same SPI bus
  stopBackgroundCaching();

  populateTocView(core);
  int currentIdx = findCurrentTocEntry(core);
  if (currentIdx >= 0) {
    tocView_.setCurrentChapter(static_cast<uint16_t>(currentIdx));
  }

  tocView_.buttons = ui::ButtonBar("Back", "Go", "<<", ">>");
  tocMode_ = true;
  needsRender_ = true;
  Serial.println("[READER] Entered TOC mode");
}

void ReaderState::exitTocMode() {
  tocMode_ = false;
  needsRender_ = true;
  Serial.println("[READER] Exited TOC mode");
}

void ReaderState::handleTocInput(Core& core, const Event& e) {
  if (e.type != EventType::ButtonPress && e.type != EventType::ButtonRepeat) {
    return;
  }

  switch (e.button) {
    case Button::Up:
      tocView_.moveUp();
      needsRender_ = true;
      break;

    case Button::Down:
      tocView_.moveDown();
      needsRender_ = true;
      break;

    case Button::Left:
      tocView_.movePageUp(tocVisibleCount());
      needsRender_ = true;
      break;

    case Button::Right:
      tocView_.movePageDown(tocVisibleCount());
      needsRender_ = true;
      break;

    case Button::Center:
      jumpToTocEntry(core, tocView_.selected);
      exitTocMode();
      startBackgroundCaching(core);
      break;

    case Button::Back:
      exitTocMode();
      startBackgroundCaching(core);
      break;

    case Button::Power:
      if (core.settings.shortPwrBtn == Settings::PowerPageTurn) {
        tocView_.moveDown();
        needsRender_ = true;
      }
      break;
  }
}

void ReaderState::populateTocView(Core& core) {
  tocView_.clear();
  const uint16_t count = core.content.tocCount();

  for (uint16_t i = 0; i < count && i < ui::ChapterListView::MAX_CHAPTERS; i++) {
    auto result = core.content.getTocEntry(i);
    if (result.ok()) {
      const TocEntry& entry = result.value;
      tocView_.addChapter(entry.title, static_cast<uint16_t>(entry.pageIndex), entry.depth);
    }
  }
}

int ReaderState::findCurrentTocEntry(Core& core) {
  ContentType type = core.content.metadata().type;

  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) return -1;
    auto epub = provider->getEpubShared();

    // Start with spine-level match as fallback
    int bestMatch = epub->getTocIndexForSpineIndex(currentSpineIndex_);
    int bestMatchPage = -1;

    // Load anchor map once from disk (avoids reopening file per TOC entry)
    std::string cachePath = epubSectionCachePath(epub->getCachePath(), currentSpineIndex_);
    auto anchors = loadAnchorMap(cachePath);

    // Refine: find the latest TOC entry whose anchor page <= current page
    const int tocCount = epub->getTocItemsCount();

    for (int i = 0; i < tocCount; i++) {
      auto tocItem = epub->getTocItem(i);
      if (tocItem.spineIndex != currentSpineIndex_) continue;

      int entryPage = 0;  // No anchor = start of spine
      if (!tocItem.anchor.empty()) {
        int anchorPage = -1;
        for (const auto& a : anchors) {
          if (a.first == tocItem.anchor) {
            anchorPage = a.second;
            break;
          }
        }
        if (anchorPage < 0) continue;  // Anchor not resolved yet
        entryPage = anchorPage;
      }

      if (entryPage <= currentSectionPage_ && entryPage >= bestMatchPage) {
        bestMatch = i;
        bestMatchPage = entryPage;
      }
    }

    return bestMatch;
  } else if (type == ContentType::Xtc) {
    // For XTC, find chapter containing current page
    const uint16_t count = core.content.tocCount();
    int lastMatch = -1;
    for (uint16_t i = 0; i < count; i++) {
      auto result = core.content.getTocEntry(i);
      if (result.ok() && result.value.pageIndex <= static_cast<uint32_t>(currentPage_)) {
        lastMatch = i;
      }
    }
    return lastMatch;
  } else if (type == ContentType::Fb2) {
    // For FB2, TOC entries store section indices — resolve to pages via anchor map
    const Theme& theme = THEME_MANAGER.current();
    const auto vp = getReaderViewport(core.settings.statusBar != 0);
    const auto config = core.settings.getRenderConfig(theme, vp.width, vp.height);
    std::string cachePath = contentCachePath(core.content.cacheDir(), config.fontId);

    auto anchors = loadAnchorMap(cachePath);

    const uint16_t count = core.content.tocCount();
    int lastMatch = -1;
    for (uint16_t i = 0; i < count; i++) {
      auto result = core.content.getTocEntry(i);
      if (!result.ok()) continue;

      std::string anchor = "section_" + std::to_string(result.value.pageIndex);
      int entryPage = -1;
      for (const auto& a : anchors) {
        if (a.first == anchor) {
          entryPage = a.second;
          break;
        }
      }
      if (entryPage < 0) continue;
      if (entryPage <= currentSectionPage_) {
        lastMatch = i;
      }
    }
    return lastMatch;
  } else if (type == ContentType::Markdown || type == ContentType::Txt) {
    // For flat-page formats, find chapter whose pageIndex <= current section page
    const uint16_t count = core.content.tocCount();
    int lastMatch = -1;
    for (uint16_t i = 0; i < count; i++) {
      auto result = core.content.getTocEntry(i);
      if (result.ok() && result.value.pageIndex <= static_cast<uint32_t>(currentSectionPage_)) {
        lastMatch = i;
      }
    }
    return lastMatch;
  }

  return -1;
}

void ReaderState::jumpToTocEntry(Core& core, int tocIndex) {
  if (tocIndex < 0 || tocIndex >= tocView_.chapterCount) {
    return;
  }

  const auto& chapter = tocView_.chapters[tocIndex];
  ContentType type = core.content.metadata().type;

  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) return;
    auto epub = provider->getEpubShared();

    if (static_cast<int>(chapter.pageNum) != currentSpineIndex_) {
      // Different spine — full reset
      // Task already stopped by enterTocMode(); caller restarts after exitTocMode()
      currentSpineIndex_ = chapter.pageNum;
      parser_.reset();
      parserSpineIndex_ = -1;
      pageCache_.reset();
      currentSectionPage_ = 0;
    } else {
      // Same spine — navigate using anchor (default to page 0)
      currentSectionPage_ = 0;
    }

    // Try anchor-based navigation for precise positioning
    auto tocItem = epub->getTocItem(tocIndex);
    if (!tocItem.anchor.empty()) {
      std::string cachePath = epubSectionCachePath(epub->getCachePath(), chapter.pageNum);
      int page = loadAnchorPage(cachePath, tocItem.anchor);

      // Anchor not resolved — build cache until found or chapter fully parsed
      if (page < 0) {
        const Theme& theme = THEME_MANAGER.current();
        renderer_.clearScreen(theme.backgroundColor);
        ui::centeredMessage(renderer_, theme, core.settings.getReaderFontId(theme), "Indexing...");
        renderer_.displayBuffer();

        createOrExtendCache(core);
        page = loadAnchorPage(cachePath, tocItem.anchor);

        while (page < 0 && pageCache_ && pageCache_->isPartial()) {
          const size_t pagesBefore = pageCache_->pageCount();
          createOrExtendCache(core);
          if (!pageCache_ || pageCache_->pageCount() <= pagesBefore) break;
          page = loadAnchorPage(cachePath, tocItem.anchor);
        }
      }

      if (page >= 0) {
        currentSectionPage_ = page;
      }
    }
  } else if (type == ContentType::Xtc) {
    // For XTC, pageNum is page index
    currentPage_ = chapter.pageNum;
  } else if (type == ContentType::Fb2) {
    // For FB2, pageNum is the section index — use anchor map to find the actual page
    const Theme& theme = THEME_MANAGER.current();
    const auto vp = getReaderViewport(core.settings.statusBar != 0);
    const auto config = core.settings.getRenderConfig(theme, vp.width, vp.height);
    std::string cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
    std::string anchor = "section_" + std::to_string(chapter.pageNum);

    int page = loadAnchorPage(cachePath, anchor);

    if (page < 0) {
      renderer_.clearScreen(theme.backgroundColor);
      ui::centeredMessage(renderer_, theme, core.settings.getReaderFontId(theme), "Indexing...");
      renderer_.displayBuffer();

      createOrExtendCache(core);
      page = loadAnchorPage(cachePath, anchor);

      while (page < 0 && pageCache_ && pageCache_->isPartial()) {
        const size_t pagesBefore = pageCache_->pageCount();
        createOrExtendCache(core);
        if (!pageCache_ || pageCache_->pageCount() <= pagesBefore) break;
        page = loadAnchorPage(cachePath, anchor);
      }
    }

    currentSectionPage_ = (page >= 0) ? page : 0;
  } else if (type == ContentType::Markdown || type == ContentType::Txt) {
    // For flat-page formats, pageNum is the section page index
    currentSectionPage_ = chapter.pageNum;
  }

  needsRender_ = true;
  Serial.printf("[READER] Jumped to TOC entry %d (spine/page %d)\n", tocIndex, chapter.pageNum);
}

int ReaderState::tocVisibleCount() const {
  constexpr int startY = 60;
  constexpr int bottomMargin = 70;
  const Theme& theme = THEME_MANAGER.current();
  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  return (renderer_.getScreenHeight() - startY - bottomMargin) / itemHeight;
}

void ReaderState::renderTocOverlay(Core& core) {
  const Theme& theme = THEME_MANAGER.current();
  constexpr int startY = 60;
  const int visibleCount = tocVisibleCount();

  // Adjust scroll to keep selection visible
  tocView_.ensureVisible(visibleCount);

  renderer_.clearScreen(theme.backgroundColor);
  renderer_.drawCenteredText(theme.uiFontId, 15, "Chapters", theme.primaryTextBlack, BOLD);

  // Use reader font only when external font is selected (for VN/Thai/CJK support),
  // otherwise use smaller UI font for better chapter list readability
  const ContentType type = core.content.metadata().type;
  const bool hasExternalFont = core.settings.hasExternalReaderFont(theme);
  const int tocFontId =
      (type == ContentType::Xtc || !hasExternalFont) ? theme.uiFontId : core.settings.getReaderFontId(theme);

  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  const int end = std::min(tocView_.scrollOffset + visibleCount, static_cast<int>(tocView_.chapterCount));
  for (int i = tocView_.scrollOffset; i < end; i++) {
    const int y = startY + (i - tocView_.scrollOffset) * itemHeight;
    ui::chapterItem(renderer_, theme, tocFontId, y, tocView_.chapters[i].title, tocView_.chapters[i].depth,
                    i == tocView_.selected, i == tocView_.currentChapter);
  }

  ui::buttonBar(renderer_, theme, tocView_.buttons);
  renderer_.displayBuffer();
  core.display.markDirty();
}

void ReaderState::exitToUI(Core& core) {
  Serial.println("[READER] Exiting to UI mode via restart");

  // Stop background caching first - BackgroundTask::stop() waits properly
  stopBackgroundCaching();

  // Save progress at last rendered position
  if (contentLoaded_) {
    ProgressManager::Progress progress;
    progress.spineIndex = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSpineIndex_;
    progress.sectionPage = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSectionPage_;
    progress.flatPage = currentPage_;
    ProgressManager::save(core, core.content.cacheDir(), core.content.metadata().type, progress);
    // Skip pageCache_.reset() and content.close() — ESP.restart() follows,
    // and if stopBackgroundCaching() timed out the task still uses them.
  }

  // Determine return destination from cached transition or fall back to sourceState_
  ReturnTo returnTo = ReturnTo::HOME;
  const auto& transition = getTransition();
  if (transition.isValid()) {
    returnTo = transition.returnTo;
  } else if (sourceState_ == StateId::FileList) {
    returnTo = ReturnTo::FILE_MANAGER;
  }

  // Show notification and restart
  showTransitionNotification("Returning to library...");
  saveTransition(BootMode::UI, nullptr, returnTo);

  // Brief delay to ensure SD writes complete before restart
  vTaskDelay(50 / portTICK_PERIOD_MS);
  ESP.restart();
}

}  // namespace papyrix
