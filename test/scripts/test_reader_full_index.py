#!/usr/bin/env python3
"""Check full-index cache misses, cache preservation, and interrupted creation."""

import subprocess
import tempfile
from pathlib import Path

from test_reader_resume import block

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ContentParser.h>
#include <Page.h>
#include <PageCache.h>
#include <PageCachePolicy.h>
#include <RenderConfig.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <content/GlobalPageMetrics.h>
#include <platform_stubs.h>
namespace page_metrics = papyrix::page_metrics;

#define TAG "TEST"
@CONSTANTS@
@PATHS@
@METRICS_READER@

void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

unsigned parsedPages = 0;
class Parser : public ContentParser {
 public:
  template<class... Args> explicit Parser(Args&&...) {}
  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& emit, uint32_t maxPages,
                  const AbortCallback& abort) override {
    uint32_t made = 0;
    while (emitted < 3 && (maxPages == 0 || made < maxPages)) {
      if (abort && abort()) return false;
      emit(std::make_unique<Page>());
      ++emitted;
      ++made;
      ++parsedPages;
    }
    return true;
  }
  bool hasMoreContent() const override { return emitted < 3; }
  bool canResume() const override { return emitted > 0; }
  void reset() override { emitted = 0; }
  uint32_t emitted = 0;
};
class EmptyParser : public ContentParser {
 public:
  bool parsePages(const std::function<void(std::unique_ptr<Page>)>&, uint32_t, const AbortCallback&) override {
    return true;
  }
  bool hasMoreContent() const override { return false; }
  bool canResume() const override { return false; }
  void reset() override {}
};
using EpubChapterParser = Parser;
using Fb2Parser = Parser;
using MarkdownParser = Parser;
using HtmlParser = Parser;
using PlainTextParser = Parser;

enum class ContentType { Epub, Fb2, Markdown, Html, PlainText, Xtc };
enum class EventType { ButtonPress };
enum class Button { Back };
struct Event { EventType type; Button button; };
struct Events { bool pop(Event&) { return false; } };
namespace papyrix::hal { struct Display { enum { HALF_REFRESH }; }; }
struct Theme { int backgroundColor = 0; };
struct Themes { Theme value; const Theme& current() const { return value; } } THEME_MANAGER;
struct Renderer {
  void clearWidthCache() {}
  void clearScreen(int) {}
  void displayBuffer(int) {}
};
struct Book {
  std::string getCachePath() const { return "/book"; }
  std::string getLanguage() const { return "en"; }
  int getSpineItemsCount() const { return 3; }
  struct Item { std::string href; };
  Item getSpineItem(int) const { return {"chapter.xhtml"}; }
  bool getItemSize(const std::string&, size_t* bytes) const { *bytes = 100; return true; }
  void getSpineItemSizes(std::vector<size_t>& sizes) const { sizes.assign(3, 100); }
  struct SectionOffset { size_t startOffset = 0; size_t endOffset = 0; };
  std::vector<SectionOffset> getSectionOffsets() const { return {{0, 100}, {100, 200}, {200, 300}}; }
};
struct Provider {
  std::shared_ptr<Book> book = std::make_shared<Book>();
  Book* getEpub() const { return book.get(); }
  std::shared_ptr<Book> getEpubShared() const { return book; }
  Book* getFb2() const { return book.get(); }
  int getSectionCount() const { return 3; }
  std::string getSectionCachePath(int section) const {
    return "/book/" + std::to_string(section) + ".bin";
  }
  std::string getSectionPath(int) const { return "/section.fb2"; }
};
struct Content {
  struct Metadata { ContentType type = ContentType::Epub; } data;
  Provider provider;
  const Metadata& metadata() const { return data; }
  Provider* asEpub() { return &provider; }
  Provider* asFb2() { return &provider; }
  const char* cacheDir() const { return "/book"; }
  uint32_t pageCount() const { return 3; }
};
struct Core {
  Content content;
  Events events;
  struct Input { void resetIdleTimer() {} } input;
  struct Cpu { void unthrottle() {} } cpu;
  struct Settings { int statusBar = 1; bool showImages = false; } settings;
};
int getReaderViewport(bool) { return 0; }
RenderConfig makeRenderConfig(const Core&, const Theme&, int) { return {}; }
class ReaderState {
 public:
  using SectionPageMetric = page_metrics::Section;
  using GlobalPageMetrics = page_metrics::Display;
  bool indexingInProgress_ = true, indexingCancelled_ = false, indexingFailed_ = false;
  bool needsRender_ = false, suppressRender_ = true;
  int indexingSpine_ = 0, indexingTotalSpines_ = 1;
  int currentSpineIndex_ = 0, currentSectionPage_ = 0, textStartIndex_ = 0;
  uint32_t currentPage_ = 0, indexingLastRenderMs_ = 0;
  std::unique_ptr<PageCache> indexingCache_;
  std::unique_ptr<ContentParser> indexingParser_;
  std::unique_ptr<PageCache> pageCache_;
  Renderer renderer_;
  std::string contentPath_ = "/book.epub";
  std::vector<SectionPageMetric> globalSectionPageMetrics_;
  bool globalSectionPageMetricsInitialized_ = false;
  void startFullBookIndexing(Core&);
  void processIndexingChunk(Core&);
  bool saveMetricsIndex(const std::string&, const RenderConfig&);
  bool loadMetricsIndex(const std::string&, const RenderConfig&, int);
  void initializeGlobalPageMetrics(Core&);
  void updateGlobalPageMetrics(Core&);
  GlobalPageMetrics resolveGlobalPageMetrics(Core&);
  void deleteMetricsIndex(Core&);
  void invalidateGlobalPageMetrics() {
    globalSectionPageMetrics_.clear();
    globalSectionPageMetricsInitialized_ = false;
  }
  void startBackgroundCaching(Core&) {}
  static void saveAnchorMap(const ContentParser&, const std::string&);
};
@FUNCTIONS@

void check(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void finish(ReaderState& state, Core& core) {
  for (unsigned calls = 0; state.indexingInProgress_ && calls < 10; ++calls) {
    state.processIndexingChunk(core);
  }
  check(!state.indexingInProgress_ && !state.indexingFailed_, "indexing completes");
}
std::string cachePath(Core& core, int spine = 0) {
  return core.content.data.type == ContentType::Epub
      ? epubSectionCachePath("/book", spine) : core.content.provider.getSectionCachePath(spine);
}
std::string metricsDir(ContentType type) { return type == ContentType::Epub ? "/book/sections" : "/book"; }
void checkPersistedMetrics(ContentType type, const RenderConfig& config, int sections, uint32_t pagesEach,
                           const char* message) {
  std::vector<page_metrics::Section> saved;
  check(readMetricsIndexFile(metricsDir(type), config, sections, saved), message);
  check(saved.size() == static_cast<size_t>(sections), message);
  for (const auto& entry : saved) {
    check(entry.exact && entry.pages == pagesEach, message);
  }
}
int main() {
  const RenderConfig config{};
  testSetManualMillis(0);
  for (ContentType type : {ContentType::Epub, ContentType::Fb2}) {
    Core core;
    core.content.data.type = type;
    const std::string path = cachePath(core);
    SdMan.reset();
    parsedPages = 0;
    ReaderState fresh;
    fresh.startFullBookIndexing(core);
    // Start may read an absent index and first caches. The zero-retry rule
    // below covers the chunk loop that creates the missing caches.
    testResetDelayStats();
    finish(fresh, core);
    std::printf("Missing-cache retry delay: %u ms\n", testDelayTotalMs());
    check(testDelayTotalMs() == 0, "new chapter caches must not incur SD read retry waits");
    check(parsedPages == 9, "each new chapter is parsed once");
    for (int i = 0; i < 3; ++i) {
      PageCache cached(cachePath(core, i));
      check(cached.load(config) && !cached.isPartial() && cached.pageCount() == 3,
            "new chapters have complete caches");
      check(cached.loadPage(2) != nullptr, "the final new page is readable");
    }
    checkPersistedMetrics(type, config, 3, 3, "completed indexing persists the exact counts it already found");
    const auto firstPage = fresh.resolveGlobalPageMetrics(core);
    check(firstPage.totalPages == 9 && firstPage.totalIsExact && firstPage.currentPage == 1,
          "the first page after indexing resolves the exact whole-book total");
    ReaderState reopened;
    reopened.currentSpineIndex_ = 2;
    reopened.currentSectionPage_ = 1;
    const auto reopenedTotal = reopened.resolveGlobalPageMetrics(core);
    check(reopenedTotal.totalPages == 9 && reopenedTotal.totalIsExact && reopenedTotal.currentPage == 8,
          "the next open resolves the whole-book total from the persisted counts");

    SdMan.reset();
    const std::string completePath = cachePath(core, 1);
    Parser completeParser;
    PageCache completeSeed(completePath);
    check(completeSeed.create(completeParser, config, 0), "create the existing middle chapter");
    const std::string completeBytes = SdMan.getWrittenData(completePath);
    parsedPages = 0;
    ReaderState complete;
    complete.startFullBookIndexing(core);
    complete.processIndexingChunk(core);
    // Fail exclusive creation and the first read of the existing middle chapter.
    SdMan.setOpenFailCount(2);
    finish(complete, core);
    check(parsedPages == 6 && SdMan.getWrittenData(completePath) == completeBytes,
          "transient open failures preserve the existing chapter between two new chapters");
    checkPersistedMetrics(type, config, 3, 3, "already-complete sections keep exact persisted counts");

    SdMan.reset();
    Parser partialParser;
    PageCache partialSeed(path);
    check(partialSeed.create(partialParser, config, 2), "create partial cache");
    const std::string partialBytes = SdMan.getWrittenData(path);
    ReaderState partial;
    partial.startFullBookIndexing(core);
    partial.processIndexingChunk(core);
    check(partial.indexingCache_ && partial.indexingCache_->pageCount() == 2 &&
              SdMan.getWrittenData(path) == partialBytes,
          "a partial cache is loaded without truncation");
    finish(partial, core);
    PageCache resumed(path);
    check(resumed.load(config) && !resumed.isPartial() && resumed.pageCount() == 3 &&
              resumed.loadPage(0) && resumed.loadPage(2),
          "partial indexing preserves earlier pages and appends the remaining page");
    checkPersistedMetrics(type, config, 3, 3, "a resumed partial section publishes its completed count");

    SdMan.reset();
    testSetLargestFreeBlock(1);
    ReaderState interrupted;
    interrupted.startFullBookIndexing(core);
    interrupted.processIndexingChunk(core);
    testResetLargestFreeBlock();
    check(interrupted.indexingFailed_ && !PageCache::probe(path, config).valid,
          "an interrupted empty cache cannot publish a valid page count");
    check(!SdMan.exists(metricsDir(type) + "/" + kMetricsIndexFilename),
          "failed indexing does not publish metrics");
    ReaderState retry;
    retry.startFullBookIndexing(core);
    finish(retry, core);
    PageCache recovered(path);
    check(recovered.load(config) && recovered.pageCount() == 3 && recovered.loadPage(2),
          "indexing recovers after interruption before parsing");
    checkPersistedMetrics(type, config, 3, 3, "recovered indexing publishes its completed count");

    SdMan.reset();
    EmptyParser emptyParser;
    PageCache emptySeed(path);
    check(emptySeed.create(emptyParser, config, 0) && !emptySeed.isPartial() && emptySeed.pageCount() == 0,
          "create a complete zero-page section cache");
    ReaderState zeroPage;
    zeroPage.startFullBookIndexing(core);
    finish(zeroPage, core);
    {
      std::vector<page_metrics::Section> saved;
      check(readMetricsIndexFile(metricsDir(type), config, 3, saved) && saved.size() == 3 && saved[0].exact &&
                saved[0].pages == 0 && saved[1].exact && saved[1].pages == 3 && saved[2].exact &&
                saved[2].pages == 3,
            "a complete zero-page section stays exact in the persisted counts");
      zeroPage.currentSpineIndex_ = 2;
      const auto zeroTotal = zeroPage.resolveGlobalPageMetrics(core);
      check(zeroTotal.totalPages == 6 && zeroTotal.totalIsExact && zeroTotal.currentPage == 4,
            "a complete zero-page section adds no pages to the book total");
    }

    SdMan.reset();
    Parser deferredParser;
    PageCache deferredSeed(path);
    check(deferredSeed.create(deferredParser, config, 2), "create a partial cache for the deferred case");
    ReaderState deferred;
    deferred.startFullBookIndexing(core);
    deferred.processIndexingChunk(core);
    check(deferred.indexingCache_ && deferred.indexingCache_->pageCount() == 2,
          "the deferred case starts from the loaded partial cache");
    testSetLargestFreeBlock(5000);
    deferred.processIndexingChunk(core);
    testResetLargestFreeBlock();
    finish(deferred, core);
    check(!SdMan.exists(metricsDir(type) + "/" + kMetricsIndexFilename),
          "a section skipped for low memory does not publish exact metrics");
    ReaderState deferredReopen;
    const auto deferredTotal = deferredReopen.resolveGlobalPageMetrics(core);
    check(!deferredTotal.totalIsExact, "the next open reports an estimated total after a skipped section");

    SdMan.reset();
    for (int i = 0; i < 3; ++i) {
      Parser seedParser;
      PageCache seed(cachePath(core, i));
      check(seed.create(seedParser, config, 0), "create complete caches for the failed-write case");
    }
    ReaderState previousMetrics;
    previousMetrics.globalSectionPageMetrics_.assign(3, page_metrics::Section{1, false, 100});
    check(previousMetrics.saveMetricsIndex(metricsDir(type), config), "create an older estimated index");
    SdMan.setOpenFileForWriteFailCount(1);
    ReaderState unwritten;
    unwritten.startFullBookIndexing(core);
    finish(unwritten, core);
    check(!SdMan.exists(metricsDir(type) + "/" + kMetricsIndexFilename),
          "a failed metrics write does not publish a partial index");
    const auto sessionPage = unwritten.resolveGlobalPageMetrics(core);
    check(sessionPage.totalPages == 9 && sessionPage.totalIsExact && sessionPage.currentPage == 1,
          "a failed metrics write keeps the counts for this session");
    ReaderState lowHeapReopen;
    ReaderState lowHeapLive;
    lowHeapLive.pageCache_ = std::make_unique<PageCache>(path);
    check(lowHeapLive.pageCache_->load(config), "load a complete chapter before memory pressure");
    lowHeapLive.currentSectionPage_ = 1;
    testSetLargestFreeBlock(sizeof(page_metrics::Section) * 3 - 1);
    const auto unavailableTotal = lowHeapReopen.resolveGlobalPageMetrics(core);
    check(!unavailableTotal.totalIsExact && !SdMan.exists(metricsDir(type) + "/" + kMetricsIndexFilename),
          "a low-memory reopen defers metrics allocation and persistence");
    const auto chapterFallback = lowHeapLive.resolveGlobalPageMetrics(core);
    check(chapterFallback.currentPage == 2 && chapterFallback.totalPages == 3 && !chapterFallback.totalIsExact,
          "a complete chapter is not an exact book total when metrics allocation is deferred");
    testResetLargestFreeBlock();
    const auto recoveredTotal = lowHeapReopen.resolveGlobalPageMetrics(core);
    check(recoveredTotal.totalPages == 3 && !recoveredTotal.totalIsExact,
          "metrics estimation resumes when memory is available");
    ReaderState unwrittenReopen;
    const auto fallbackTotal = unwrittenReopen.resolveGlobalPageMetrics(core);
    check(!fallbackTotal.totalIsExact, "the next open falls back when the metrics write failed");

    RenderConfig otherConfig = config;
    otherConfig.fontId ^= 1;
    SdMan.reset();
    Parser oldParser;
    PageCache old(path);
    check(old.create(oldParser, otherConfig, 0), "create a previous-font cache");
    ReaderState changedFont;
    changedFont.startFullBookIndexing(core);
    finish(changedFont, core);
    check(PageCache::probe(path, config).valid && !PageCache::probe(path, otherConfig).valid,
          "full indexing replaces an incompatible cache with the current layout");
  }
  {
    SdMan.reset();
    Core core;
    core.content.data.type = ContentType::PlainText;
    ReaderState document;
    document.pageCache_ = std::make_unique<PageCache>("/plain.bin");
    Parser parser;
    check(document.pageCache_->create(parser, config, 0), "cache a complete single-section document");
    document.currentSectionPage_ = 1;
    testSetLargestFreeBlock(sizeof(page_metrics::Section) - 1);
    const auto total = document.resolveGlobalPageMetrics(core);
    testResetLargestFreeBlock();
    check(total.currentPage == 2 && total.totalPages == 3 && total.totalIsExact,
          "a complete single-section document keeps its exact total under memory pressure");
  }
  std::puts("PASS: full indexing avoids missing-cache waits and preserves cache recovery");
}
'''


def main():
    reader = (ROOT / "src/states/ReaderState.cpp").read_text()
    names = ("kAnchorMapMagic", "kAnchorMapVersion", "kMetricsIndexVersion", "kMetricsIndexFilename",
             "kHotMinFreeHeap", "kHotMinLargestBlock", "kColdMinFreeHeap", "kColdMinLargestBlock", "kIndexingChunk")
    constants = "\n".join(line for line in reader.splitlines()
                          if line.startswith(("constexpr ", "static constexpr ")) and
                          any(name in line for name in names))
    paths = "\n".join(block(reader, signature) for signature in (
        "inline std::string epubSectionCachePath(", "inline std::string contentCachePath("))
    functions = "\n".join(block(reader, signature) for signature in (
        "void ReaderState::saveAnchorMap(", "void ReaderState::startFullBookIndexing(",
        "void ReaderState::processIndexingChunk(", "bool ReaderState::saveMetricsIndex(", "void ReaderState::deleteMetricsIndex(",
        "bool ReaderState::loadMetricsIndex(", "void ReaderState::initializeGlobalPageMetrics(",
        "void ReaderState::updateGlobalPageMetrics(", "ReaderState::GlobalPageMetrics ReaderState::resolveGlobalPageMetrics("))
    harness = HARNESS.replace("@CONSTANTS@", constants).replace("@PATHS@", paths).replace("@FUNCTIONS@", functions)
    harness = harness.replace("@METRICS_READER@", block(reader, "bool readMetricsIndexFile("))

    # Use the production read retries with the host filesystem and deterministic clock.
    storage = (ROOT / "lib/SDCardManager/src/SDCardManager.cpp").read_text()
    mock = (ROOT / "test/mocks/SDCardManager.h").read_text()
    signature = "bool openFileForRead(const char* moduleName, const char* path, FsFile& file)"
    read_method = block(storage, "bool SDCardManager::" + signature[5:])
    read_method = read_method.replace("SDCardManager::", "").replace("sd.open(", "open(")
    mock = mock.replace(block(mock, signature), read_method)
    # Cache names are numeric. The whitespace-name fallback does not apply.
    mock = '''#include <cassert>
#include <platform_stubs.h>
#include <SdFat.h>
inline bool openByDirScan(const char* path, int, FsFile&) {
  const char* slash = strrchr(path, '/');
  assert(slash && slash[1] != ' ');
  return false;
}
''' + mock
    includes = ["test/mocks", "test/common", "src", "lib/PageCache/src", "lib/RenderTypes/src",
                "lib/Serialization/src", "lib/GfxRenderer/src", "lib/EpdFont/src", "lib/Utf8/src",
                "lib/BoardSupport/include", "lib/EInkDisplay/include"]
    sources = ["lib/PageCache/src/PageCache.cpp", "lib/RenderTypes/src/Page.cpp",
               "lib/RenderTypes/src/blocks/TextBlock.cpp", "lib/Utf8/src/Utf8.cpp",
               "src/content/GlobalPageMetrics.cpp", "test/mocks/platform_stubs.cpp"]
    with tempfile.TemporaryDirectory(prefix="papyrix-full-index-") as directory:
        source = Path(directory) / "index.cpp"
        binary = Path(directory) / "index"
        source.write_text(harness)
        (Path(directory) / "SDCardManager.h").write_text(mock)
        subprocess.run(["c++", "-std=c++17", "-DTEST_BUILD", "-I" + directory] +
                       ["-I" + str(ROOT / path) for path in includes] + [str(source)] +
                       [str(ROOT / path) for path in sources] + ["-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
