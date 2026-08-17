#include <ContentParser.h>
#include <Page.h>
#include <PageCache.h>
#include <PageCachePolicy.h>
#include <RenderConfig.h>
#include <Serialization.h>

#include <cstring>
#include <cstdint>
#include <memory>
#include <type_traits>

static_assert(std::is_same_v<decltype(std::declval<PageCache>().pageCount()), uint32_t>,
              "PageCache page counts must exceed the 16-bit legacy limit");
static_assert(std::is_same_v<decltype(PageCache::ProbeResult{}.pageCount), uint32_t>,
              "probed page counts must preserve the on-disk width");

#include "SDCardManager.h"
#include "test_utils.h"

// Page.cpp references image-page methods even though these tests serialize only
// empty pages. Keep the test target focused by supplying inert link stubs.
void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

namespace {

class CountingParser final : public ContentParser {
 public:
  CountingParser(uint32_t totalPages, bool resumable) : totalPages_(totalPages), resumable_(resumable) {}

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& onPageComplete, uint32_t maxPages,
                  const AbortCallback& shouldAbort) override {
    uint32_t produced = 0;
    while (emittedPages_ < totalPages_ && (maxPages == 0 || produced < maxPages)) {
      if (shouldAbort && shouldAbort()) return false;
      onPageComplete(std::make_unique<Page>());
      ++emittedPages_;
      ++produced;
    }
    return true;
  }

  bool hasMoreContent() const override { return emittedPages_ < totalPages_; }
  bool canResume() const override { return resumable_ && emittedPages_ > 0; }
  void reset() override { emittedPages_ = 0; }

 private:
  uint32_t totalPages_;
  uint32_t emittedPages_ = 0;
  bool resumable_;
};

bool writeSyntheticCache(const char* path, const RenderConfig& config, uint32_t pageCount) {
  FsFile file;
  if (!SdMan.openFileForWrite("CACHE", path, file)) return false;

  constexpr uint8_t version = 22;
  constexpr uint32_t headerSize = 43;
  constexpr uint32_t lutOffset = 45;
  constexpr uint32_t pagePosition = headerSize;
  const uint8_t partial = 1;
  const uint32_t bytesConsumed = 1;
  const uint32_t totalBytes = 2;
  bool ok = serialization::writePodChecked(file, version) &&
            serialization::writePodChecked(file, config.fontId) &&
            serialization::writePodChecked(file, config.lineCompression) &&
            serialization::writePodChecked(file, config.indentLevel) &&
            serialization::writePodChecked(file, config.spacingLevel) &&
            serialization::writePodChecked(file, config.paragraphAlignment) &&
            serialization::writePodChecked(file, config.hyphenation) &&
            serialization::writePodChecked(file, config.showImages) &&
            serialization::writePodChecked(file, config.viewportWidth) &&
            serialization::writePodChecked(file, config.viewportHeight) &&
            serialization::writePodChecked(file, pageCount) &&
            serialization::writePodChecked(file, partial) &&
            serialization::writePodChecked(file, lutOffset) &&
            serialization::writePodChecked(file, bytesConsumed) &&
            serialization::writePodChecked(file, totalBytes) &&
            serialization::writePodChecked(file, config.sourceFingerprint) &&
            serialization::writePodChecked(file, config.fontFingerprint);
  const uint16_t emptyPageElements = 0;
  ok = ok && serialization::writePodChecked(file, emptyPageElements);
  for (uint32_t i = 0; ok && i < pageCount; ++i) {
    ok = serialization::writePodChecked(file, pagePosition);
  }
  ok = ok && file.sync();
  file.close();
  return ok;
}

bool readCommittedSpan(const char* path, uint32_t& pageCount, uint32_t& lutOffset, uint32_t& fileSize) {
  FsFile file;
  if (!SdMan.openFileForRead("CACHE", path, file) || !file.seek(18) ||
      !serialization::readPodChecked(file, pageCount)) {
    return false;
  }
  uint8_t partial;
  const bool ok = serialization::readPodChecked(file, partial) &&
                  serialization::readPodChecked(file, lutOffset);
  fileSize = static_cast<uint32_t>(file.size());
  file.close();
  return ok;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("PageCacheIntegration");
  const RenderConfig config{};

  {
    SdMan.reset();
    constexpr const char* path = "/cache/page-boundary.bin";
    CountingParser parser(2, false);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 0), "page boundary fixture is created");

    std::string bytes = SdMan.getWrittenData(path);
    uint32_t lutOffset = 0;
    memcpy(&lutOffset, bytes.data() + 23, sizeof(lutOffset));
    uint32_t wrongNextPage = 44;
    memcpy(bytes.data() + lutOffset + sizeof(uint32_t), &wrongNextPage, sizeof(wrongNextPage));
    SdMan.registerFile(path, bytes);

    PageCache corrupted(path);
    runner.expectTrue(corrupted.load(config), "cache with in-range page boundary loads");
    runner.expectTrue(corrupted.loadPage(0) == nullptr, "page record must end at the next LUT boundary");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/fingerprint.bin";
    RenderConfig originalConfig{};
    originalConfig.sourceFingerprint = 0x12345678u;
    originalConfig.fontFingerprint = 0x89ABCDEFu;
    CountingParser parser(1, false);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, originalConfig, 0), "fingerprinted cache is created");

    RenderConfig replacedSource = originalConfig;
    replacedSource.sourceFingerprint ^= 1u;
    PageCache sourceMismatch(path);
    runner.expectFalse(sourceMismatch.load(replacedSource), "replaced source invalidates page cache");
    runner.expectFalse(SdMan.exists(path), "source mismatch removes the stale cache");

    CountingParser recreatedParser(1, false);
    PageCache recreated(path);
    runner.expectTrue(recreated.create(recreatedParser, originalConfig, 0),
                      "font mismatch gets an independent cache fixture");
    RenderConfig replacedFont = originalConfig;
    replacedFont.fontFingerprint ^= 1u;
    PageCache fontMismatch(path);
    runner.expectFalse(fontMismatch.load(replacedFont), "replaced font invalidates page cache");
    runner.expectFalse(SdMan.exists(path), "font mismatch removes the stale cache");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/probe.bin";
    runner.expectTrue(writeSyntheticCache(path, config, 1), "probe fixture is created");

    const auto validProbe = PageCache::probe(path, config);
    runner.expectTrue(validProbe.valid, "real probe accepts a matching cache");
    runner.expectEq(static_cast<uint32_t>(1), validProbe.pageCount, "real probe returns the page count");
    runner.expectTrue(validProbe.partial, "real probe returns partial state");

    RenderConfig mismatch = config;
    mismatch.fontFingerprint ^= 1u;
    runner.expectFalse(PageCache::probe(path, mismatch).valid, "real probe rejects a font mismatch");
    runner.expectTrue(SdMan.exists(path), "probe rejects without deleting the cache");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/cold-large.bin";
    CountingParser initialParser(1002, false);
    PageCache initialCache(path);
    runner.expectTrue(initialCache.create(initialParser, config, 1000), "large partial cache is created");
    runner.expectTrue(initialCache.isPartial(), "large initial cache remains partial");

    PageCache reloadedCache(path);
    runner.expectTrue(reloadedCache.load(config), "large partial cache reloads after parser reset");
    CountingParser coldParser(1002, false);
    runner.expectTrue(reloadedCache.extend(coldParser, 5), "cold extend continues beyond one thousand pages");
    runner.expectEq(static_cast<uint32_t>(1002), reloadedCache.pageCount(),
                    "cold extend preserves and appends all pages");
    runner.expectTrue(reloadedCache.loadPage(999) != nullptr,
                      "cold extend keeps the former last page readable");
    runner.expectFalse(reloadedCache.isPartial(), "cold extend marks exhausted content complete");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/lut-size-overflow.bin";
    CountingParser parser(0, false);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 0), "LUT overflow fixture is created");

    std::string bytes = SdMan.getWrittenData(path);
    constexpr size_t kPageCountOffset = 18;
    const uint32_t overflowingPageCount = UINT32_C(1) << 30;
    memcpy(bytes.data() + kPageCountOffset, &overflowingPageCount, sizeof(overflowingPageCount));
    SdMan.registerFile(path, bytes);

    PageCache corrupted(path);
    runner.expectFalse(corrupted.load(config), "page count whose LUT byte size overflows is rejected");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/background-read-ahead.bin";
    CountingParser parser(12, true);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 5), "background read-ahead fixture is created");
    runner.expectTrue(cache.isPartial(), "background read-ahead fixture is partial");
    runner.expectFalse(cache.needsExtension(0), "foreground threshold is not reached on the first page");

    if (page_cache::backgroundShouldExtend(true, cache.isPartial(), parser.canResume(), cache.pageCount(), 0)) {
      runner.expectTrue(cache.extend(parser, PageCache::DEFAULT_CACHE_CHUNK),
                        "background path extends a partial cache before the foreground threshold");
    }
    runner.expectEq(static_cast<uint32_t>(10), cache.pageCount(),
                    "background path appends the next read-ahead chunk");
    runner.expectTrue(cache.loadPage(4) != nullptr,
                      "hot extend keeps the former last page readable");
    runner.expectTrue(cache.isPartial(), "background path leaves remaining content partial");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/wide-load.bin";
    runner.expectTrue(writeSyntheticCache(path, config, 70000), "wide cache fixture is written");
    PageCache cache(path);
    runner.expectTrue(cache.load(config), "real cache load accepts more than 65535 LUT entries");
    runner.expectEq(static_cast<uint32_t>(70000), cache.pageCount(), "real cache load preserves wide page count");
    runner.expectTrue(cache.loadPage(69999) != nullptr, "wide cache loads a page through its 32-bit LUT");
    CountingParser parser(70001, false);
    runner.expectTrue(cache.extend(parser, 5), "cold extend resumes beyond the legacy page limit");
    runner.expectEq(static_cast<uint32_t>(70001), cache.pageCount(), "cold extend preserves the wide page count");
    runner.expectTrue(cache.loadPage(70000) != nullptr, "cold extend commits the appended wide-index page");
    uint32_t committedCount = 0;
    uint32_t committedLut = 0;
    uint32_t committedSize = 0;
    runner.expectTrue(readCommittedSpan(path, committedCount, committedLut, committedSize),
                      "cold extend header remains readable");
    runner.expectEq(static_cast<uint32_t>(70001), committedCount, "cold extend header count is committed");
    runner.expectEq(committedLut + committedCount * static_cast<uint32_t>(sizeof(uint32_t)), committedSize,
                    "cold extend commits a complete LUT span");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/hot-header-rollback.bin";
    CountingParser parser(4, true);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 2), "hot header rollback fixture is created");

    uint32_t oldCount = 0;
    uint32_t oldLut = 0;
    uint32_t oldSize = 0;
    runner.expectTrue(readCommittedSpan(path, oldCount, oldLut, oldSize), "hot committed header is readable");
    PageCache::setFailHeaderCommitOnce(true);
    runner.expectFalse(cache.extend(parser, 2), "hot partial header commit fails");
    runner.expectTrue(SdMan.exists(path), "hot header rollback preserves the cache file");

    PageCache recovered(path);
    runner.expectTrue(recovered.load(config), "hot header rollback restores a readable cache");
    runner.expectEq(oldCount, recovered.pageCount(), "hot header rollback restores page count");
    runner.expectTrue(recovered.isPartial(), "hot header rollback restores partial state");
    runner.expectTrue(recovered.loadPage(oldCount - 1) != nullptr, "hot header rollback preserves the last page");
    uint32_t recoveredCount = 0;
    uint32_t recoveredLut = 0;
    uint32_t recoveredSize = 0;
    runner.expectTrue(readCommittedSpan(path, recoveredCount, recoveredLut, recoveredSize),
                      "hot rolled-back span is readable");
    runner.expectEq(oldLut, recoveredLut, "hot header rollback restores LUT offset");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/cold-header-rollback.bin";
    CountingParser initialParser(4, false);
    PageCache initialCache(path);
    runner.expectTrue(initialCache.create(initialParser, config, 2), "cold header rollback fixture is created");

    PageCache cache(path);
    runner.expectTrue(cache.load(config), "cold header rollback fixture reloads");
    uint32_t oldCount = 0;
    uint32_t oldLut = 0;
    uint32_t oldSize = 0;
    runner.expectTrue(readCommittedSpan(path, oldCount, oldLut, oldSize), "cold committed header is readable");
    CountingParser coldParser(4, false);
    PageCache::setFailHeaderCommitOnce(true);
    runner.expectFalse(cache.extend(coldParser, 2), "cold partial header commit fails");
    runner.expectTrue(SdMan.exists(path), "cold header rollback preserves the cache file");

    PageCache recovered(path);
    runner.expectTrue(recovered.load(config), "cold header rollback restores a readable cache");
    runner.expectEq(oldCount, recovered.pageCount(), "cold header rollback restores page count");
    runner.expectTrue(recovered.isPartial(), "cold header rollback restores partial state");
    runner.expectTrue(recovered.loadPage(oldCount - 1) != nullptr, "cold header rollback preserves the last page");
    uint32_t recoveredCount = 0;
    uint32_t recoveredLut = 0;
    uint32_t recoveredSize = 0;
    runner.expectTrue(readCommittedSpan(path, recoveredCount, recoveredLut, recoveredSize),
                      "cold rolled-back span is readable");
    runner.expectEq(oldLut, recoveredLut, "cold header rollback restores LUT offset");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/hot-seek-end-failure.bin";
    CountingParser parser(4, true);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 2), "hot seek failure fixture is created");

    SdMan.setSeekEndResult(false);
    runner.expectFalse(cache.extend(parser, 2), "hot extend reports seek-to-end failure");
    SdMan.setSeekEndResult(true);
    runner.expectTrue(SdMan.exists(path), "seek-to-end failure preserves the cache file");
    PageCache recovered(path);
    runner.expectTrue(recovered.load(config), "cache remains readable after seek-to-end failure");
    runner.expectEq(static_cast<uint32_t>(2), recovered.pageCount(),
                    "seek-to-end failure preserves committed page count");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/rollback-sync-failure.bin";
    CountingParser parser(4, true);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 2), "rollback sync failure fixture is created");

    SdMan.setSyncResult(false);
    runner.expectFalse(cache.extend(parser, 2), "extension fails when commit and rollback cannot sync");
    SdMan.setSyncResult(true);
    runner.expectFalse(SdMan.exists(path), "cache is removed when committed state cannot be restored durably");
  }

  {
    SdMan.reset();
    constexpr const char* path = "/cache/rollback.bin";
    CountingParser parser(4, true);
    PageCache cache(path);
    runner.expectTrue(cache.create(parser, config, 2), "rollback fixture cache is created");
    runner.expectTrue(cache.isPartial(), "rollback fixture cache is partial");

    // Fail the second appended page after the first one has already reached
    // the file. The cache must not commit a parser position with a missing page.
    PageCache::setFailSerializeInterval(2);
    runner.expectFalse(cache.extend(parser, 2), "hot extend reports append failure");
    PageCache::setFailSerializeInterval(0);
    runner.expectTrue(SdMan.exists(path), "failed append preserves the previous cache file");

    PageCache recovered(path);
    runner.expectTrue(recovered.load(config), "previous cache remains readable after failed append");
    runner.expectEq(static_cast<uint32_t>(2), recovered.pageCount(), "rollback keeps the committed page count");
    runner.expectTrue(recovered.isPartial(), "rollback keeps the committed partial state");
    runner.expectTrue(recovered.extend(parser, 2), "retry restarts the parser from the committed cache boundary");
    runner.expectEq(static_cast<uint32_t>(4), recovered.pageCount(), "retry recovers every source page");
    runner.expectFalse(recovered.isPartial(), "successful retry completes the cache");
  }

  return runner.allPassed() ? 0 : 1;
}
