#include "ReaderState.h"

#include <Arduino.h>
#include <CoverHelpers.h>
#include <Epub/Page.h>
#include <EpubChapterParser.h>
#include <GfxRenderer.h>
#include <MarkdownParser.h>
#include <PageCache.h>
#include <PlainTextParser.h>
#include <esp_system.h>

#include <cstring>

#include "../Battery.h"
#include "../config.h"
#include "../content/ProgressManager.h"
#include "../content/ReaderNavigation.h"
#include "../core/BootMode.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

namespace papyrix {

namespace {
constexpr int horizontalPadding = 5;
constexpr int statusBarMargin = 19;

// Cache path helpers
inline std::string epubSectionCachePath(const std::string& epubCachePath, int spineIndex) {
  return epubCachePath + "/sections/" + std::to_string(spineIndex) + ".bin";
}

inline std::string contentCachePath(const char* cacheDir, int fontId) {
  return std::string(cacheDir) + "/pages_" + std::to_string(fontId) + ".bin";
}
}  // namespace

int ReaderState::calcFirstContentSpine(bool hasCover, int textStartIndex, size_t spineCount) {
  if (hasCover && textStartIndex == 0 && spineCount > 1) {
    return 1;
  }
  return textStartIndex;
}

// Template implementation for cache creation/extension
template <typename ParserT>
void ReaderState::createOrExtendCacheImpl(ParserT& parser, const std::string& cachePath, const RenderConfig& config) {
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  if (!pageCache_) {
    pageCache_.reset(new PageCache(cachePath));
    if (pageCache_->load(config)) {
      if (pageCache_->isPartial()) {
        pageCache_->extend(parser, PageCache::DEFAULT_CACHE_CHUNK);
      }
      xSemaphoreGive(cacheMutex_);
      return;
    }
  }
  if (pageCache_->isPartial()) {
    pageCache_->extend(parser, PageCache::DEFAULT_CACHE_CHUNK);
  } else {
    pageCache_->create(parser, config, PageCache::DEFAULT_CACHE_CHUNK);
  }
  xSemaphoreGive(cacheMutex_);
}

// Template implementation for background caching (handles stop request checks)
template <typename ParserT>
void ReaderState::backgroundCacheImpl(ParserT& parser, const std::string& cachePath, const RenderConfig& config) {
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  pageCache_.reset(new PageCache(cachePath));
  bool loaded = pageCache_->load(config);
  bool needsExtend = loaded && pageCache_->needsExtension(currentSectionPage_);
  xSemaphoreGive(cacheMutex_);

  if (!loaded || needsExtend) {
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    if (!cacheTaskStopRequested_) {
      if (needsExtend) {
        pageCache_->extend(parser, PageCache::DEFAULT_CACHE_CHUNK);
      } else {
        pageCache_->create(parser, config, PageCache::DEFAULT_CACHE_CHUNK);
      }
    }
    xSemaphoreGive(cacheMutex_);
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
      tocView_{},
      cacheMutex_(xSemaphoreCreateMutex()) {
  contentPath_[0] = '\0';
}

ReaderState::~ReaderState() {
  stopBackgroundCaching();
  if (cacheMutex_) {
    vSemaphoreDelete(cacheMutex_);
    cacheMutex_ = nullptr;
  }
}

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
  needsRender_ = true;
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  pageCache_.reset();
  xSemaphoreGive(cacheMutex_);
  cacheTaskComplete_ = false;
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
}

void ReaderState::exit(Core& core) {
  Serial.println("[READER] Exiting");

  // Stop background caching task first
  stopBackgroundCaching();

  if (contentLoaded_) {
    // Save progress at last rendered position (not current requested position)
    ProgressManager::Progress progress;
    // If on cover, save as (0, 0) - cover is implicit start
    progress.spineIndex = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSpineIndex_;
    progress.sectionPage = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSectionPage_;
    progress.flatPage = currentPage_;
    ProgressManager::save(core, core.content.cacheDir(), core.content.metadata().type, progress);

    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    pageCache_.reset();
    xSemaphoreGive(cacheMutex_);
    core.content.close();
  }

  contentLoaded_ = false;
  contentPath_[0] = '\0';

  // Reset orientation to Portrait for UI
  renderer_.setOrientation(GfxRenderer::Orientation::Portrait);
}

StateTransition ReaderState::update(Core& core) {
  if (!contentLoaded_) {
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
      stopBackgroundCaching();
      xSemaphoreTake(cacheMutex_, portMAX_DELAY);
      pageCache_.reset();
      xSemaphoreGive(cacheMutex_);
      cacheTaskComplete_ = false;
    }
    currentSectionPage_ = 0;
    needsRender_ = true;
    return;
  }

  ContentType type = core.content.metadata().type;
  ReaderNavigation::Position pos;
  pos.spineIndex = currentSpineIndex_;
  pos.sectionPage = currentSectionPage_;
  pos.flatPage = currentPage_;
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  auto result = ReaderNavigation::next(type, pos, pageCache_.get(), core.content.pageCount());
  xSemaphoreGive(cacheMutex_);
  applyNavResult(result);
}

void ReaderState::navigatePrev(Core& core) {
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
      stopBackgroundCaching();
      xSemaphoreTake(cacheMutex_, portMAX_DELAY);
      pageCache_.reset();  // Don't need cache for cover
      xSemaphoreGive(cacheMutex_);
      cacheTaskComplete_ = false;
      needsRender_ = true;
    }
    return;  // At start of book either way
  }

  // Prevent going back from cover
  if (currentSpineIndex_ == 0 && currentSectionPage_ == -1) {
    return;  // Already at cover
  }

  ContentType type = core.content.metadata().type;
  ReaderNavigation::Position pos;
  pos.spineIndex = currentSpineIndex_;
  pos.sectionPage = currentSectionPage_;
  pos.flatPage = currentPage_;
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  auto result = ReaderNavigation::prev(type, pos, pageCache_.get());
  xSemaphoreGive(cacheMutex_);
  applyNavResult(result);
}

void ReaderState::applyNavResult(const ReaderNavigation::NavResult& result) {
  currentSpineIndex_ = result.position.spineIndex;
  currentSectionPage_ = result.position.sectionPage;
  currentPage_ = result.position.flatPage;
  needsRender_ = result.needsRender;
  if (result.needsCacheReset) {
    stopBackgroundCaching();
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    pageCache_.reset();
    xSemaphoreGive(cacheMutex_);
    cacheTaskComplete_ = false;
  }
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
        startBackgroundCaching(core);
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
      renderCachedPage(core);
      break;
    case ContentType::Xtc:
      renderXtcPage(core);
      break;
    default:
      break;
  }

  core.display.markDirty();
}

void ReaderState::renderCachedPage(Core& core) {
  Theme& theme = THEME_MANAGER.mutableCurrent();
  ContentType type = core.content.metadata().type;
  const auto vp = getReaderViewport();

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

  // Create or load cache if needed
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  bool cacheExists = (pageCache_ != nullptr);
  xSemaphoreGive(cacheMutex_);

  if (!cacheExists) {
    // Check if background task completed
    if (cacheTaskComplete_) {
      Serial.println("[READER] Using cache from background task");
    } else {
      // Try to load existing cache silently first
      loadCacheFromDisk(core);

      // Check if current page is now cached
      xSemaphoreTake(cacheMutex_, portMAX_DELAY);
      bool pageIsCached =
          pageCache_ && currentSectionPage_ >= 0 && currentSectionPage_ < static_cast<int>(pageCache_->pageCount());
      xSemaphoreGive(cacheMutex_);

      if (!pageIsCached) {
        // Current page not cached - show "Indexing..." and create/extend
        renderer_.clearScreen(theme.backgroundColor);
        ui::centeredMessage(renderer_, theme, core.settings.getReaderFontId(theme), "Indexing...");
        renderer_.displayBuffer();

        createOrExtendCache(core);

        // Clear overlay
        renderer_.clearScreen(theme.backgroundColor);
      }
    }

    // Clamp page number (handle negative values and out-of-bounds)
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    if (pageCache_) {
      const int cachedPages = static_cast<int>(pageCache_->pageCount());
      if (currentSectionPage_ < 0) {
        currentSectionPage_ = 0;
      } else if (currentSectionPage_ >= cachedPages) {
        currentSectionPage_ = cachedPages > 0 ? cachedPages - 1 : 0;
      }
    }
    xSemaphoreGive(cacheMutex_);
  }

  // Check if we need to extend cache
  if (!ensurePageCached(core, currentSectionPage_)) {
    renderer_.drawCenteredText(core.settings.getReaderFontId(theme), 300, "Failed to load page", theme.primaryTextBlack,
                               BOLD);
    renderer_.displayBuffer();
    return;
  }

  // Load and render page (cache is now guaranteed to exist)
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  size_t pageCount = pageCache_ ? pageCache_->pageCount() : 0;
  auto page = pageCache_ ? pageCache_->loadPage(currentSectionPage_) : nullptr;
  xSemaphoreGive(cacheMutex_);

  if (!page) {
    Serial.println("[READER] Failed to load page, clearing cache");
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    if (pageCache_) {
      pageCache_->clear();
      pageCache_.reset();
    }
    xSemaphoreGive(cacheMutex_);
    needsRender_ = true;
    return;
  }

  const int fontId = core.settings.getReaderFontId(theme);

  renderPageContents(core, *page, vp.marginTop, vp.marginRight, vp.marginBottom, vp.marginLeft);
  renderStatusBar(core, vp.marginRight, vp.marginBottom, vp.marginLeft);

  displayWithRefresh(core);

  // Grayscale text rendering (anti-aliasing)
  if (core.settings.textAntiAliasing && renderer_.fontSupportsGrayscale(fontId) && renderer_.storeBwBuffer()) {
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
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  if (!pageCache_) {
    xSemaphoreGive(cacheMutex_);
    return false;
  }

  // If page is already cached, we're good
  size_t pageCount = pageCache_->pageCount();
  bool needsExtension = pageCache_->needsExtension(pageNum);
  bool isPartial = pageCache_->isPartial();
  xSemaphoreGive(cacheMutex_);

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

  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  pageCount = pageCache_ ? pageCache_->pageCount() : 0;
  xSemaphoreGive(cacheMutex_);

  return pageNum < pageCount;
}

void ReaderState::loadCacheFromDisk(Core& core) {
  const Theme& theme = THEME_MANAGER.current();
  ContentType type = core.content.metadata().type;

  const auto vp = getReaderViewport();
  const auto config = core.settings.getRenderConfig(theme, vp.width, vp.height);

  std::string cachePath;
  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) {
      Serial.println("[READER] loadCacheFromDisk: no epub provider");
      return;
    }
    cachePath = epubSectionCachePath(provider->getEpub()->getCachePath(), currentSpineIndex_);
  } else if (type == ContentType::Markdown || type == ContentType::Txt) {
    cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
  } else {
    Serial.printf("[READER] loadCacheFromDisk: unsupported content type %d\n", static_cast<int>(type));
    return;
  }

  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  if (!pageCache_) {
    pageCache_.reset(new PageCache(cachePath));
    if (!pageCache_->load(config)) {
      pageCache_.reset();
    }
  }
  xSemaphoreGive(cacheMutex_);
}

void ReaderState::createOrExtendCache(Core& core) {
  const Theme& theme = THEME_MANAGER.current();
  ContentType type = core.content.metadata().type;

  const auto vp = getReaderViewport();
  const auto config = core.settings.getRenderConfig(theme, vp.width, vp.height);

  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (!provider || !provider->getEpub()) return;

    auto epub = provider->getEpubShared();
    std::string imageCachePath = core.settings.showImages ? (epub->getCachePath() + "/images") : "";
    std::string cachePath = epubSectionCachePath(epub->getCachePath(), currentSpineIndex_);
    EpubChapterParser parser(epub, currentSpineIndex_, renderer_, config, imageCachePath);
    createOrExtendCacheImpl(parser, cachePath, config);
  } else if (type == ContentType::Markdown) {
    std::string cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
    MarkdownParser parser(contentPath_, renderer_, config);
    createOrExtendCacheImpl(parser, cachePath, config);
  } else {
    std::string cachePath = contentCachePath(core.content.cacheDir(), config.fontId);
    PlainTextParser parser(contentPath_, renderer_, config);
    createOrExtendCacheImpl(parser, cachePath, config);
  }
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
  if (type == ContentType::Epub) {
    auto* provider = core.content.asEpub();
    if (provider && provider->getEpub()) {
      data.currentPage = currentSectionPage_ + 1;
      xSemaphoreTake(cacheMutex_, portMAX_DELAY);
      if (pageCache_) {
        data.totalPages = pageCache_->pageCount();
        data.isPartial = pageCache_->isPartial();
      } else {
        data.isPartial = true;
      }
      xSemaphoreGive(cacheMutex_);
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
  const int fontId = core.settings.getReaderFontId(theme);

  auto result = xtcRenderer_.render(provider->getParser(), currentPage_, [this, &core]() { displayWithRefresh(core); });

  switch (result) {
    case XtcPageRenderer::RenderResult::Success:
      break;
    case XtcPageRenderer::RenderResult::EndOfBook:
      ui::centeredMessage(renderer_, theme, fontId, "End of book");
      break;
    case XtcPageRenderer::RenderResult::InvalidDimensions:
      ui::centeredMessage(renderer_, theme, fontId, "Invalid file");
      break;
    case XtcPageRenderer::RenderResult::AllocationFailed:
      ui::centeredMessage(renderer_, theme, fontId, "Memory error");
      break;
    case XtcPageRenderer::RenderResult::PageLoadFailed:
      ui::centeredMessage(renderer_, theme, fontId, "Page load error");
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

ReaderState::Viewport ReaderState::getReaderViewport() const {
  Viewport vp{};
  renderer_.getOrientedViewableTRBL(&vp.marginTop, &vp.marginRight, &vp.marginBottom, &vp.marginLeft);
  vp.marginLeft += horizontalPadding;
  vp.marginRight += horizontalPadding;
  vp.marginBottom += statusBarMargin;
  vp.width = renderer_.getScreenWidth() - vp.marginLeft - vp.marginRight;
  vp.height = renderer_.getScreenHeight() - vp.marginTop - vp.marginBottom;
  return vp;
}

bool ReaderState::renderCoverPage(Core& core) {
  Serial.printf("[%lu] [RDR] Generating cover for reader...\n", millis());
  std::string coverPath = core.content.generateCover(core.settings.coverDithering);
  if (coverPath.empty()) {
    Serial.printf("[%lu] [RDR] No cover available, skipping cover page\n", millis());
    return false;
  }

  Serial.printf("[%lu] [RDR] Rendering cover page from: %s\n", millis(), coverPath.c_str());
  const auto vp = getReaderViewport();
  int pagesUntilRefresh = pagesUntilFullRefresh_;
  const bool turnOffScreen = core.settings.sunlightFadingFix != 0;

  bool rendered = CoverHelpers::renderCoverFromBmp(renderer_, coverPath, vp.marginTop, vp.marginRight, vp.marginBottom,
                                                   vp.marginLeft, pagesUntilRefresh,
                                                   core.settings.getPagesPerRefreshValue(), turnOffScreen);

  pagesUntilFullRefresh_ = pagesUntilRefresh;
  return rendered;
}

void ReaderState::startBackgroundCaching(Core& core) {
  if (cacheTaskHandle_) {
    return;  // Already running
  }

  Serial.println("[READER] Starting background page cache task");
  coreForCacheTask_ = &core;
  cacheTaskComplete_ = false;

  xTaskCreate(&ReaderState::cacheTaskTrampoline, "PageCache", 8192, this, 0, &cacheTaskHandle_);
}

void ReaderState::cacheTaskTrampoline(void* param) { static_cast<ReaderState*>(param)->cacheTaskLoop(); }

void ReaderState::cacheTaskLoop() {
  const Theme& theme = THEME_MANAGER.current();

  Serial.println("[READER] Background cache task started");

  if (cacheTaskStopRequested_) {
    Serial.println("[READER] Background cache task aborted (stop requested)");
    vTaskSuspend(nullptr);
    return;
  }

  // Snapshot shared state under mutex to avoid race condition
  xSemaphoreTake(cacheMutex_, portMAX_DELAY);
  bool cacheExists = (pageCache_ != nullptr);
  Core* corePtr = coreForCacheTask_;
  int sectionPage = currentSectionPage_;
  int spineIndex = currentSpineIndex_;
  bool coverExists = hasCover_;
  int textStart = textStartIndex_;
  xSemaphoreGive(cacheMutex_);

  if (!cacheExists && !cacheTaskStopRequested_ && corePtr) {
    Core& coreRef = *corePtr;
    ContentType type = coreRef.content.metadata().type;

    if (type == ContentType::Epub) {
      auto* provider = coreRef.content.asEpub();
      if (provider && provider->getEpub() && !cacheTaskStopRequested_) {
        const auto vp = getReaderViewport();
        const auto config = coreRef.settings.getRenderConfig(theme, vp.width, vp.height);
        const auto* epub = provider->getEpub();
        std::string imageCachePath = coreRef.settings.showImages ? (epub->getCachePath() + "/images") : "";
        // When on cover page (sectionPage=-1), cache the first content spine
        int spineToCache = spineIndex;
        if (sectionPage == -1) {
          spineToCache = calcFirstContentSpine(coverExists, textStart, epub->getSpineItemsCount());
        }
        std::string cachePath = epubSectionCachePath(epub->getCachePath(), spineToCache);
        EpubChapterParser parser(provider->getEpubShared(), spineToCache, renderer_, config, imageCachePath);
        backgroundCacheImpl(parser, cachePath, config);
      }
    } else if (type == ContentType::Markdown && !cacheTaskStopRequested_) {
      const auto vp = getReaderViewport();
      const auto config = coreRef.settings.getRenderConfig(theme, vp.width, vp.height);
      std::string cachePath = contentCachePath(coreRef.content.cacheDir(), config.fontId);
      MarkdownParser parser(contentPath_, renderer_, config);
      backgroundCacheImpl(parser, cachePath, config);
    } else if (type == ContentType::Txt && !cacheTaskStopRequested_) {
      const auto vp = getReaderViewport();
      const auto config = coreRef.settings.getRenderConfig(theme, vp.width, vp.height);
      std::string cachePath = contentCachePath(coreRef.content.cacheDir(), config.fontId);
      PlainTextParser parser(contentPath_, renderer_, config);
      backgroundCacheImpl(parser, cachePath, config);
    }
  }

  if (!cacheTaskStopRequested_) {
    cacheTaskComplete_ = true;
    Serial.println("[READER] Background cache task completed");
  } else {
    Serial.println("[READER] Background cache task stopped");
  }

  // Suspend self - will be deleted by stopBackgroundCaching()
  vTaskSuspend(nullptr);
}

void ReaderState::stopBackgroundCaching() {
  if (cacheTaskHandle_) {
    Serial.println("[READER] Stopping background cache task");
    cacheTaskStopRequested_ = true;

    // Wait for task to complete or suspend (max 500ms)
    for (int i = 0; i < 50 && eTaskGetState(cacheTaskHandle_) == eRunning; i++) {
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(cacheTaskHandle_);
    cacheTaskHandle_ = nullptr;
    cacheTaskStopRequested_ = false;
  }
}

// ============================================================================
// TOC Overlay Mode
// ============================================================================

void ReaderState::enterTocMode(Core& core) {
  if (core.content.tocCount() == 0) {
    return;
  }

  populateTocView(core);
  int currentIdx = findCurrentTocEntry(core);
  if (currentIdx >= 0) {
    tocView_.setCurrentChapter(static_cast<uint8_t>(currentIdx));
  }

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
  if (e.type != EventType::ButtonPress) {
    return;
  }

  switch (e.button) {
    case Button::Up:
    case Button::Left:
      tocView_.moveUp();
      needsRender_ = true;
      break;

    case Button::Down:
    case Button::Right:
      tocView_.moveDown();
      needsRender_ = true;
      break;

    case Button::Center:
      jumpToTocEntry(core, tocView_.selected);
      exitTocMode();
      break;

    case Button::Back:
      exitTocMode();
      break;

    case Button::Power:
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
    if (provider && provider->getEpub()) {
      return provider->getEpub()->getTocIndexForSpineIndex(currentSpineIndex_);
    }
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
    // For EPUB, pageNum is spine index
    if (static_cast<int>(chapter.pageNum) != currentSpineIndex_) {
      currentSpineIndex_ = chapter.pageNum;
      currentSectionPage_ = 0;
      stopBackgroundCaching();
      xSemaphoreTake(cacheMutex_, portMAX_DELAY);
      pageCache_.reset();
      xSemaphoreGive(cacheMutex_);
      cacheTaskComplete_ = false;
    }
  } else if (type == ContentType::Xtc) {
    // For XTC, pageNum is page index
    currentPage_ = chapter.pageNum;
  }

  needsRender_ = true;
  Serial.printf("[READER] Jumped to TOC entry %d (spine/page %d)\n", tocIndex, chapter.pageNum);
}

void ReaderState::renderTocOverlay(Core& core) {
  const Theme& theme = THEME_MANAGER.current();
  constexpr int startY = 60;
  const int availableHeight = renderer_.getScreenHeight() - startY - 20;
  const int itemHeight = theme.itemHeight + theme.itemSpacing;
  const int visibleCount = availableHeight / itemHeight;

  // Adjust scroll to keep selection visible
  tocView_.ensureVisible(visibleCount);

  renderer_.clearScreen(theme.backgroundColor);
  renderer_.drawCenteredText(theme.uiFontId, 15, "Chapters", theme.primaryTextBlack, BOLD);

  const int end = std::min(tocView_.scrollOffset + visibleCount, static_cast<int>(tocView_.chapterCount));
  for (int i = tocView_.scrollOffset; i < end; i++) {
    const int y = startY + (i - tocView_.scrollOffset) * itemHeight;
    ui::chapterItem(renderer_, theme, y, tocView_.chapters[i].title, tocView_.chapters[i].depth, i == tocView_.selected,
                    i == tocView_.currentChapter);
  }

  renderer_.displayBuffer();
  core.display.markDirty();
}

void ReaderState::exitToUI(Core& core) {
  Serial.println("[READER] Exiting to UI mode via restart");

  // Stop background caching first
  stopBackgroundCaching();

  // Save progress at last rendered position
  if (contentLoaded_) {
    ProgressManager::Progress progress;
    progress.spineIndex = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSpineIndex_;
    progress.sectionPage = (lastRenderedSectionPage_ == -1) ? 0 : lastRenderedSectionPage_;
    progress.flatPage = currentPage_;
    ProgressManager::save(core, core.content.cacheDir(), core.content.metadata().type, progress);

    // Close content
    xSemaphoreTake(cacheMutex_, portMAX_DELAY);
    pageCache_.reset();
    xSemaphoreGive(cacheMutex_);
    core.content.close();
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
