#!/usr/bin/env python3
"""Check book persistence after a reboot and a page render."""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <algorithm>
#include <TargetConfig.h>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include "core/ReaderButtonDispatcher.h"
#include "states/State.h"
#include "ui/TouchLayout.h"
uint32_t nowMs = 1000;
uint32_t millis() { return nowMs; }
template<class... T> void logMessage(T...) {}
#define TAG "TEST"
#define LOG_DBG(...) logMessage(__VA_ARGS__)
#define LOG_INF(...) logMessage(__VA_ARGS__)
#define LOG_ERR(...) logMessage(__VA_ARGS__)
#define tr(x) #x
constexpr int BOLD = 1;
namespace papyrix { namespace hal { struct Display { enum { FAST_REFRESH, HALF_REFRESH }; }; } }
struct Theme { int backgroundColor = 0; bool primaryTextBlack = true; int uiFontId = 0, itemHeight = 32; };
struct Themes { Theme value; Theme& current() { return value; } Theme& mutableCurrent() { return value; } } THEME_MANAGER;
struct Renderer {
  enum { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  enum class Orientation { Portrait };
  void setOrientation(Orientation) {}
  int width = 480, height = 800;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int displays = 0;
  template<class... T> void clearScreen(T...) {}
  template<class... T> void drawCenteredText(T...) {}
  template<class... T> void displayBuffer(T...) { ++displays; }
  template<class... T> void fillRect(T...) {}
  bool supportsGrayscale() { return false; }
  bool fontSupportsGrayscale(int) { return false; }
  void setRenderMode(int) {}
  void copyGrayscaleLsbBuffers() {}
  void copyGrayscaleMsbBuffers() {}
  void displayGrayBuffer(bool) {}
  void cleanupGrayscaleWithFrameBuffer() {}
};
using GfxRenderer = Renderer;
struct BitmapTurnCache { explicit BitmapTurnCache(Renderer&) {} };
struct Fonts { void unloadReaderFonts() {} } FONT_MANAGER;
namespace ui { template<class... T> void render(T&...) {} template<class... T> void centeredMessage(T&&...) {} }
namespace ui {
@TAP_GUARD@;
@MENU_VIEW@;
template<class... T> void overlayBox(T&&...) {}
}
struct Page {
  void warmGlyphs(Renderer&, int) {}
  bool hasImages() { return false; }
  bool getImageBoundingBox(int16_t&, int16_t&, int16_t&, int16_t&) { return false; }
  template<class... T> void render(T&...) {}
};
struct Cache {
  bool readable = true;
  unsigned count = 1;
  bool partial = false;
  bool isPartial() { return partial; }
  unsigned pageCount() { return count; }
  void clear() {}
  std::unique_ptr<Page> loadPage(uint32_t) { return readable ? std::make_unique<Page>() : nullptr; }
};
struct Parser { bool canResume() { return false; } };
struct Task { bool isRunning() { return false; } };
namespace page_cache { template<class... T> bool backgroundWorkPending(T...) { return false; } }
namespace papyrix {
enum class BootMode { UI, READER };
enum class ReturnTo { HOME, FILE_MANAGER, RECENT };
enum class ExitToUiMode { Graceful, Emergency };
struct ExitToUiPlan { ExitToUiMode mode = ExitToUiMode::Graceful; ReturnTo returnTo = ReturnTo::HOME; };
struct Restart { unsigned count = 0; void restart() { ++count; } } ESP;
constexpr int portTICK_PERIOD_MS = 1;
void vTaskDelay(int) {}
void saveEmergencyUiTransition(ReturnTo) {}
void showTransitionNotification(const char*) {}
@EXIT_POLICY@
struct ModeTransition {
  static constexpr uint32_t MAGIC = 0xB007BADE;
  uint32_t magic = 0;
  BootMode mode = BootMode::UI;
  ReturnTo returnTo = ReturnTo::HOME;
  char bookPath[1024] = {};
  bool isValid() const { return magic == MAGIC; }
};
struct SaveResult { bool ok_ = true; bool ok() const { return ok_; } };
struct Settings {
  enum { StartupLastDocument, StartupHome };
  enum { PowerPageTurn, PowerBookmark, NextPrev };
  int shortPwrBtn = PowerPageTurn, touchPageTurns = 0, sideButtonLayout = 0;
  uint32_t getPowerButtonDuration() const { return 400; }
  char lastBookPath[1024] = {};
  char pendingBookPath[1024] = {};
  uint8_t pendingTransition = 0, transitionReturnTo = 0, startupBehavior = StartupLastDocument;
  bool showImages = false, statusBar = false, sunlightFadingFix = false, textAntiAliasing = false;
  int getReaderFontId(Theme&) { return 0; }
  int getReaderFontId(const Theme&) { return 0; }
  bool saveToFile() const;
  SaveResult save(int&) const;
};
Settings disk;
unsigned saves = 0;
bool saveAllowed = true;
bool Settings::saveToFile() const { if (!saveAllowed) return false; disk = *this; ++saves; return true; }
SaveResult Settings::save(int&) const { SaveResult r; r.ok_ = saveToFile(); return r; }
struct Metadata { ContentType type = ContentType::Epub; const char* title = "Book"; const char* author = "Author"; };
struct Provider {
  Provider* getEpub() { return this; }
  Provider* getFb2() { return this; }
  unsigned getSpineItemsCount() const { return 1; }
  unsigned getSectionCount() { return 1; }
  int getParser() { return 0; }
};
struct Content {
  Metadata meta;
  Provider provider;
  Metadata& metadata() { return meta; }
  Provider* asEpub() { return &provider; }
  Provider* asFb2() { return &provider; }
  Provider* asXtc() { return &provider; }
  unsigned pageCount() { return 5; }
  const char* cacheDir() { return "/cache"; }
  void close() {}
  unsigned tocCount() { return 0; }
};
struct Input {
  bool pageButtonsBlocked = false;
  void setPageButtonsBlocked(bool blocked) { pageButtonsBlocked = blocked; }
};
struct Core {
  Settings settings;
  Content content;
  Input input;
  int storage = 0;
  struct {
    bool isAvailable() const { return false; }
    uint8_t brightness() const { return 0; }
    uint8_t warmth() const { return 50; }
    bool setBrightness(uint8_t) { return false; }
    bool setWarmth(uint8_t) { return false; }
  } frontLight;
  EventQueue events;
  struct { char text[1] = {}; } buf;
} core;
struct ProgressManager {
  struct Progress { int spineIndex = 0, sectionPage = 0; uint32_t flatPage = 0; };
  static inline unsigned writes = 0;
  static Progress saved;
  static void save(Core&, const char*, ContentType, const Progress& value) { saved = value; ++writes; }
};
ProgressManager::Progress ProgressManager::saved;
struct RecentBooksStore {
  unsigned adds = 0;
  std::string path;
  static RecentBooksStore& instance() { static RecentBooksStore store; return store; }
  void add(const char* value, const char*, const char*) { path = value; ++adds; }
};
struct Sd { bool exists(const char* path) { return strcmp(path, "/missing.epub") != 0; } } SdMan;
static ModeTransition cachedTransition;
static bool transitionCached = false;
bool consumeEmergencyUiTransition(ModeTransition&) { return false; }
void clearTransition();
@BOOT@
struct ReaderNavigation {
  struct Position { int spineIndex = 0, sectionPage = 0; };
  struct Skip { bool needsRender = false; Position position; };
  static bool isCoverPosition(int spine, int page) { return spine == 0 && page == -1; }
  static Skip skipEmptySection(Position, int, bool) { return {}; }
};
struct XtcPageRenderer {
  enum class RefreshRequest { GrayscaleBase, Normal };
  enum class RenderResult { Success, EndOfBook, InvalidDimensions, AllocationFailed, PageLoadFailed };
  RenderResult result = RenderResult::Success;
  template<class F> RenderResult render(int, uint32_t, F refresh) {
    if (result == RenderResult::Success) refresh(RefreshRequest::Normal);
    return result;
  }
};
struct ReaderState {
  Renderer renderer_;
  XtcPageRenderer xtcRenderer_;
  char contentPath_[1024] = "/new.epub";
  bool needsRender_ = true, contentLoaded_ = true, hasRenderedPage_ = false, resumeRegistered_ = false;
  bool indexingInProgress_ = false, bookStatsMode_ = false, menuMode_ = false, bookmarkMode_ = false, tocMode_ = false;
  bool forceDoubleRefresh_ = false, hasCover_ = false, coverSucceeds = false;
  bool thumbnailDone_ = true, coverDone_ = true, canStop = true;
  bool cacheWaitActive_ = false;
  bool loadFailed_ = false, indexingCancelled_ = false, indexingFailed_ = false, suppressRender_ = false;
  bool cancelIndexingChunk = false;
  uint32_t bookmarkNotifyMs_ = 0, indexingLastRenderMs_ = 0;
  int bookmarkCount_ = 0;
  ReaderButtonDispatcher readerButtons_;
  ui::OverlayTapGuard overlayTapGuard_;
  int currentSpineIndex_ = 0, currentSectionPage_ = 0, lastRenderedSpineIndex_ = 0, lastRenderedSectionPage_ = 0;
  int textStartIndex_ = 0, parserSpineIndex_ = -1;
  uint32_t currentPage_ = 0, lastRenderedPage_ = 0;
  int storedChapterPages = -1, completedChapterPages = -1;
  bool storedChapterPartial = false;
  std::unique_ptr<Cache> pageCache_ = std::make_unique<Cache>();
  std::unique_ptr<Parser> parser_;
  std::unique_ptr<Cache> indexingCache_;
  std::unique_ptr<Parser> indexingParser_;
  Task cacheTask_;
  struct View { bool needsRender = false; } bookStatsView_;
  ui::ReaderMenuView menuView_;
  struct Session { void updateProgress(int) {} } readingSession_;
  struct GlobalPageMetrics { int currentPage = 0, totalPages = 0; };
  struct Viewport { int marginTop = 0, marginRight = 0, marginBottom = 0, marginLeft = 0; };
  Viewport getReaderViewport(bool) { return {}; }
  void render(Core&);
  StateTransition update(Core&);
  void exit(Core&);
  void exitToUI(Core&);
  StateId sourceState_ = StateId::Home;
  void flushReadingSession() {}
  void invalidateGlobalPageMetrics() {}
  void saveBookmarks(Core&) {}
  void renderCurrentPage(Core&);
  void endCacheWait(Core&);
  void renderCachedPage(Core&);
  void renderXtcPage(Core&);
  void onPageRendered(Core&);
  bool stopBackgroundCaching(bool = false) { return canStop; }
  void startBackgroundCaching(Core&) {}
  void processIndexingChunk(Core&) {
    indexingInProgress_ = false;
    indexingCancelled_ = cancelIndexingChunk;
  }
  void handleBookStatsInput(Core&, const Event&) { assert(false); }
  void handleMenuInput(Core&, const Event&);
  void handleBookmarkInput(Core&, const Event&) { assert(false); }
  void handleTocInput(Core&, const Event&) { assert(false); }
  void enterMenuMode(Core&);
  void exitMenuMode();
  void handleMenuAction(Core&, ui::ReaderMenuView::Item);
  void enterTocMode(Core&) {}
  void enterBookmarkMode(Core&) {}
  void enterBookStatsMode(Core&) {}
  void navigateNext(Core&) { assert(false); }
  void navigatePrev(Core&) { assert(false); }
  void navigateNextChapter(Core&) { assert(false); }
  void navigatePrevChapter(Core&) { assert(false); }
  void addBookmark(Core&) { assert(false); }
  void showBookmarkNotification(Core&) { assert(false); }
  bool renderCoverPage(Core&) { return coverSucceeds; }
  void loadCacheFromDisk(Core&) {
    if (storedChapterPages < 0) return;
    pageCache_ = std::make_unique<Cache>();
    pageCache_->count = static_cast<unsigned>(storedChapterPages);
    pageCache_->partial = storedChapterPartial;
  }
  void createOrExtendCache(Core&) {
    if (completedChapterPages < 0) return;
    pageCache_ = std::make_unique<Cache>();
    pageCache_->count = static_cast<unsigned>(completedChapterPages);
  }
  bool ensurePageCached(Core&, uint32_t page) { return pageCache_ && page < pageCache_->count; }
  void renderPageContents(Core&, Page&, int, int, int, int) {}
  void renderStatusBar(Core&, int, int, int) {}
  void renderIndexingScreen(Core&) {}
  void renderBookmarkOverlay(Core&) {}
  void renderTocOverlay(Core&) {}
  void displayWithRefresh(Core&) { renderer_.displayBuffer(); }
  void displayGrayscaleBase(Core&) { renderer_.displayBuffer(); }
  void updateReadingProgress(GlobalPageMetrics&) {}
};
@READER@
void coldBoot() { core.settings = disk; cachedTransition = {}; transitionCached = false; }
void reset() {
  nowMs = 1000;
  core = {};
  strcpy(core.settings.lastBookPath, "/good.epub");
  disk = core.settings;
  saves = 0;
  saveAllowed = true;
  RecentBooksStore::instance() = {};
  ESP.count = 0;
  ProgressManager::writes = 0;
}
void check(bool condition, const char* message) {
  if (!condition) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}  // namespace papyrix
int main() {
  using namespace papyrix;
  for (bool cover : {false, true}) {
    for (bool landscape : {false, true}) {
      for (bool pageTaps : {false, true}) {
        reset();
        saveTransition(BootMode::READER, "/new.epub", ReturnTo::RECENT);
        coldBoot();
        check(detectBootMode() == BootMode::READER, "touch exit starts from a reader boot");
        ReaderState reader;
        reader.sourceState_ = StateId::Recent;
        reader.currentSectionPage_ = cover ? -1 : 0;
        reader.renderer_.width = landscape ? 800 : 480;
        reader.renderer_.height = landscape ? 480 : 800;
        core.settings.touchPageTurns = pageTaps;
        check(core.events.push(Event::tap({int16_t(reader.renderer_.width / 2), 100})), "menu tap is queued");
        reader.update(core);
        check(reader.menuMode_, "touch opens the reader menu from text and cover");
        if (PAPYRIX_TARGET_X4PRO) {
          check(core.events.push(Event::tap({0, 0})), "outside tap is queued");
          reader.update(core);
          check(!reader.menuMode_ && ESP.count == 0, "outside tap returns to reading without closing the book");
          check(core.events.push(Event::tap({0, 100})), "trailing page tap is queued");
          reader.update(core);
          nowMs += 300;
          check(core.events.push(Event::tap({int16_t(reader.renderer_.width / 2), 100})), "menu reopens");
          reader.update(core);
          check(reader.menuMode_, "menu opens after the close guard expires");
          check(core.events.push(Event::buttonPress(Button::Back)), "physical Back closes the menu");
          reader.update(core);
          check(!reader.menuMode_ && ESP.count == 0, "physical Back returns from the menu to reading");
          check(core.events.push(Event::buttonPress(Button::Back)), "physical Back exits the book");
        } else {
          const auto back = reader.menuView_.itemBounds(static_cast<int>(ui::ReaderMenuView::Item::Back),
              reader.renderer_.width, reader.renderer_.height, THEME_MANAGER.current().itemHeight);
          check(core.events.push(Event::tap({int16_t(back.x + back.width / 2),
                                             int16_t(back.y + back.height / 2)})), "Back tap is queued");
        }
        reader.update(core);
        check(ESP.count == 1, "reader exit remains available for each input layout");
        coldBoot();
        check(detectBootMode() == BootMode::UI, "touch reader exit boots into UI mode");
        check(cachedTransition.returnTo == ReturnTo::RECENT, "touch reader exit keeps the return destination");
      }
    }
  }
  for (bool duringChunk : {false, true}) {
    reset();
    saveTransition(BootMode::READER, "/new.epub", ReturnTo::RECENT);
    coldBoot();
    check(detectBootMode() == BootMode::READER, "indexing starts after the reader boot");
    ReaderState indexing;
    indexing.sourceState_ = StateId::FileList;
    indexing.indexingInProgress_ = true;
    indexing.cancelIndexingChunk = duringChunk;
    if (!duringChunk) check(core.events.push(Event::buttonPress(Button::Back)), "Back enters the event queue");
    const auto next = indexing.update(core);
    check(next.next == StateId::Reader, "indexing cancellation cannot request an absent UI state");
    check(ESP.count == 1, "indexing cancellation restarts the reader");
    check(ProgressManager::writes == 0, "cancel before the first page preserves saved progress");
    coldBoot();
    check(detectBootMode() == BootMode::UI, "indexing cancellation boots into UI mode");
    check(cachedTransition.returnTo == ReturnTo::RECENT, "indexing cancellation retains the return destination");
    check(strcmp(disk.lastBookPath, "/good.epub") == 0, "indexing cancellation retains the previous resume target");
  }
  reset();
  ReaderState indexed;
  indexed.indexingInProgress_ = true;
  check(indexed.update(core).next == StateId::Reader && ESP.count == 0,
        "completed indexing stays in the reader without a restart");
  puts("PASS: indexing cancellation reboots into UI without replacing saved reading progress");
  reset();
  for (auto type : {ContentType::Epub, ContentType::Fb2}) {
    core.content.meta.type = type;
    for (int pages : {1, 17}) {
      ReaderState cached;
      cached.pageCache_.reset();
      cached.currentSectionPage_ = INT16_MAX;
      cached.storedChapterPages = pages;
      cached.render(core);
      check(cached.renderer_.displays == 1, "cached backward chapter jump must not show an indexing screen");
      check(cached.hasRenderedPage_ && cached.lastRenderedSectionPage_ == pages - 1,
            "cached backward chapter jump renders the final page");
    }
    for (bool hasPartialCache : {false, true}) {
      ReaderState uncached;
      uncached.pageCache_.reset();
      uncached.currentSectionPage_ = INT16_MAX;
      uncached.storedChapterPages = hasPartialCache ? 5 : -1;
      uncached.storedChapterPartial = hasPartialCache;
      uncached.completedChapterPages = 17;
      uncached.render(core);
      check(uncached.hasRenderedPage_ && uncached.lastRenderedSectionPage_ == 16,
            "uncached backward chapter jump completes the chapter before selecting its final page");
    }
  }
  puts("PASS: backward chapter transitions use complete caches and finish partial or missing caches");
  reset();
  saveTransition(BootMode::READER, "/new.epub", ReturnTo::RECENT);
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "pending open must retain known-good book");
  coldBoot();
  check(detectBootMode() == BootMode::READER, "pending book opens after reboot");
  check(strcmp(cachedTransition.bookPath, "/new.epub") == 0, "reader receives pending book");
  check(cachedTransition.returnTo == ReturnTo::RECENT, "return destination survives reboot");
  coldBoot();
  check(detectBootMode() == BootMode::UI, "reset before first page must not repeat reader attempt");
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "failed attempt retains known-good book");

  reset();
  check(detectBootMode() == BootMode::READER, "last document startup opens known-good book");
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "startup guard does not erase known-good path");
  coldBoot();
  check(detectBootMode() == BootMode::UI, "automatic startup also prevents failed-render loops");
  reset();
  saveTransition(BootMode::READER, "/missing.epub", ReturnTo::FILE_MANAGER);
  coldBoot();
  check(detectBootMode() == BootMode::UI, "missing pending book returns to UI");
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "missing book preserves previous target");

  for (auto type : {ContentType::Epub, ContentType::Fb2, ContentType::Txt, ContentType::Markdown, ContentType::Html, ContentType::Xtc}) {
    reset();
    core.content.meta.type = type;
    core.settings.pendingTransition = 1;
    ReaderState state;
    if (type == ContentType::Xtc) state.xtcRenderer_.result = XtcPageRenderer::RenderResult::PageLoadFailed;
    else state.pageCache_->readable = false;
    state.render(core);
    check(strcmp(disk.lastBookPath, "/good.epub") == 0, "failed page cannot replace resume target");
    check(RecentBooksStore::instance().adds == 0, "failed page stays out of recents");
    state.pageCache_ = std::make_unique<Cache>();
    state.xtcRenderer_.result = XtcPageRenderer::RenderResult::Success;
    state.needsRender_ = true;
    state.render(core);
    check(strcmp(disk.lastBookPath, "/new.epub") == 0, "successful page records new resume target");
    check(disk.pendingTransition == 0, "successful page clears boot-attempt guard");
    check(RecentBooksStore::instance().path == "/new.epub", "successful page enters recents");
    const unsigned afterFirstPage = saves;
    state.currentPage_ = 1;
    state.needsRender_ = true;
    state.render(core);
    check(saves == afterFirstPage && RecentBooksStore::instance().adds == 1, "later pages do not repeat book registration");
    check(state.lastRenderedPage_ == 1, "successful flat-page position advances");
    state.currentPage_ = 2;
    state.pageCache_->readable = false;
    state.xtcRenderer_.result = XtcPageRenderer::RenderResult::PageLoadFailed;
    state.needsRender_ = true;
    state.render(core);
    check(state.lastRenderedPage_ == 1, "failed page does not advance saved position");
    ProgressManager::writes = 0;
    state.exitToUI(core);
    check(ProgressManager::writes == 1 && ProgressManager::saved.flatPage == 1,
          "library exit stores the last successful page, not the failed request");
    ProgressManager::writes = 0;
    state.exit(core);
    check(ProgressManager::writes == 1 && ProgressManager::saved.flatPage == 1,
          "exit stores the last successful page, not the failed request");
  }

  reset();
  core.content.meta.type = ContentType::Epub;
  core.settings.pendingTransition = 1;
  ReaderState flaky;
  saveAllowed = false;
  flaky.render(core);
  check(flaky.lastRenderedPage_ == 0, "rendered page advances position despite the failed settings save");
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "failed save retains the previous resume target");
  check(RecentBooksStore::instance().adds == 0, "failed save stays out of recents");
  ProgressManager::writes = 0;
  flaky.exit(core);
  check(ProgressManager::writes == 1 && ProgressManager::saved.flatPage == 0,
        "exit stores the rendered page even when the settings save failed");

  reset();
  core.content.meta.type = ContentType::Epub;
  core.settings.pendingTransition = 1;
  ReaderState retry;
  saveAllowed = false;
  retry.render(core);
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "storage failure defers resume registration");
  saveAllowed = true;
  retry.currentPage_ = 1;
  retry.needsRender_ = true;
  retry.render(core);
  check(strcmp(disk.lastBookPath, "/new.epub") == 0, "later page retries the resume save after storage recovers");
  check(disk.pendingTransition == 0, "recovered save clears the boot-attempt guard");
  check(RecentBooksStore::instance().adds == 1 && RecentBooksStore::instance().path == "/new.epub",
        "recovered save registers the book exactly once");
  const unsigned afterRecovery = saves;
  retry.currentPage_ = 2;
  retry.needsRender_ = true;
  retry.render(core);
  check(saves == afterRecovery && RecentBooksStore::instance().adds == 1,
        "later pages do not repeat book registration after recovery");
  check(retry.lastRenderedPage_ == 2, "flat-page position keeps advancing after recovery");
  ProgressManager::writes = 0;
  retry.exit(core);
  check(ProgressManager::writes == 1 && ProgressManager::saved.flatPage == 2,
        "exit after recovery stores the newest successful page");
  for (int mode = 0; mode < 5; ++mode) {
    reset();
    ReaderState state;
    state.indexingInProgress_ = mode == 0;
    state.menuMode_ = mode == 1;
    state.bookmarkMode_ = mode == 2;
    state.tocMode_ = mode == 3;
    state.bookStatsMode_ = mode == 4;
    state.render(core);
    check(strcmp(disk.lastBookPath, "/good.epub") == 0, "overlay does not register a book");
  }
  reset();
  ReaderState empty;
  empty.pageCache_->count = 0;
  empty.render(core);
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "empty book does not register");
  ProgressManager::writes = 0;
  empty.exitToUI(core);
  check(ProgressManager::writes == 0, "library exit without a page preserves saved progress");
  ProgressManager::writes = 0;
  empty.exit(core);
  check(ProgressManager::writes == 0, "exit without a page leaves saved progress unchanged");
  reset();
  ReaderState busy;
  busy.canStop = false;
  busy.render(core);
  check(strcmp(disk.lastBookPath, "/good.epub") == 0, "busy cache owner does not register a book");
  reset();
  ReaderState cover;
  core.settings.showImages = true;
  cover.currentSectionPage_ = -1;
  cover.coverSucceeds = true;
  cover.render(core);
  check(strcmp(disk.lastBookPath, "/new.epub") == 0, "rendered cover records the book");
  reset();
  saveAllowed = false;
  check(detectBootMode() == BootMode::UI, "failed boot guard write must not start reader");
  puts("PASS: reboot handoff preserves the previous book; successful pages record resume and recents;"
       " failed saves retry on later pages");
}
'''


def main():
    source_root = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT
    boot = (source_root / "src/core/BootMode.cpp").read_text()
    reader = (source_root / "src/states/ReaderState.cpp").read_text()
    boot_methods = ["BootMode detectBootMode(", "void saveTransition(", "void clearTransition(",
                    "const ModeTransition& getTransition("]
    reader_methods = ["void ReaderState::render(", "void ReaderState::renderCurrentPage(",
                      "void ReaderState::renderCachedPage(", "void ReaderState::renderXtcPage(",
                      "void ReaderState::exit(", "void ReaderState::exitToUI(",
                      "void ReaderState::endCacheWait(", "StateTransition ReaderState::update(",
                      "void ReaderState::enterMenuMode(", "void ReaderState::exitMenuMode(",
                      "void ReaderState::handleMenuInput(", "void ReaderState::handleMenuAction("]
    if "void ReaderState::onPageRendered(" in reader:
        reader_methods.append("void ReaderState::onPageRendered(")
    harness = HARNESS.replace("@BOOT@", "\n".join(block(boot, name) for name in boot_methods))
    harness = harness.replace("@READER@", "\n".join(block(reader, name) for name in reader_methods))
    views = (ROOT / "src/ui/views/ReaderViews.h").read_text()
    harness = harness.replace("@TAP_GUARD@", block(views, "class OverlayTapGuard"))
    harness = harness.replace("@MENU_VIEW@", block(views, "struct ReaderMenuView"))
    policy = (ROOT / "src/core/ExitToUiTransition.cpp").read_text()
    harness = harness.replace("@EXIT_POLICY@", block(policy, "ExitToUiPlan planExitToUi("))
    with tempfile.TemporaryDirectory(prefix="papyrix-reader-resume-") as directory:
        path = Path(directory)
        source = path / "resume.cpp"
        source.write_text(harness)
        binary = path / "resume"
        for target in ("X4PRO", "STICKY", "MURPHY_M4"):
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", f"-DPAPYRIX_TARGET_{target}=1",
                            "-I", str(ROOT / "src"), "-I", str(ROOT / "lib/BoardSupport/include"),
                            "-I", str(ROOT / "lib/EInkDisplay/include"),
                            str(source), str(ROOT / "src/core/ReaderButtonDispatcher.cpp"),
                            str(ROOT / "src/ui/TouchLayout.cpp"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
