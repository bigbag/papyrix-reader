#include <BookMetadataCache.h>
#include <Display.h>
#include <Epub.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <EpubChapterParser.h>
#include <GfxRenderer.h>
#include <Page.h>
#include <PageCache.h>
#include <RenderConfig.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "test_utils.h"

namespace {
constexpr const char* bookPath = "/books/anchors.epub";
constexpr const char* chapterHref = "OEBPS/chapter.xhtml";
using Snapshot = std::vector<std::string>;

std::string storedChapter(const std::string& html) {
  std::string zip;
  const std::string name = chapterHref;
  auto put16 = [&](uint16_t n) {
    zip.push_back(static_cast<char>(n));
    zip.push_back(static_cast<char>(n >> 8));
  };
  auto put32 = [&](uint32_t n) { for (int i = 0; i < 4; ++i) zip.push_back(static_cast<char>(n >> (i * 8))); };
  put32(0x04034b50);
  for (uint16_t n : {20, 0, 0, 0, 0}) put16(n);
  put32(0);
  put32(html.size());
  put32(html.size());
  put16(name.size());
  put16(0);
  zip += name;
  zip += html;
  const uint32_t central = zip.size();
  put32(0x02014b50);
  for (uint16_t n : {20, 20, 0, 0, 0, 0}) put16(n);
  put32(0);
  put32(html.size());
  put32(html.size());
  put16(name.size());
  for (int i = 0; i < 4; ++i) put16(0);
  put32(0);
  put32(0);
  zip += name;
  const uint32_t centralSize = zip.size() - central;
  put32(0x06054b50);
  put16(0);
  put16(0);
  put16(1);
  put16(1);
  put32(centralSize);
  put32(central);
  put16(0);
  return zip;
}

void appendPage(Snapshot& result, const Page& page) {
  result.emplace_back("PAGE");
  for (const auto& element : page.elements) {
    std::string line = std::to_string(element->xPos) + "," + std::to_string(element->yPos);
    if (element->getTag() == TAG_PageLine) {
      const auto& block = static_cast<const PageLine&>(*element).getTextBlock();
      line += " style=" + std::to_string(block.getStyle());
      for (const auto& word : block.getWords()) {
        line += " [" + word.word + ":" + std::to_string(word.style) + ":" + std::to_string(word.xPos) + "]";
      }
    } else if (element->getTag() == TAG_PageImage) {
      const auto& image = static_cast<const PageImage&>(*element).getImageBlock();
      line += " image=" + image.getCachedBmpPath() + ":" + std::to_string(image.getWidth()) + "x" +
              std::to_string(image.getHeight());
    }
    result.push_back(std::move(line));
  }
}

void registerImage(const std::string& cachePath) {
  constexpr unsigned pixels = 60 * 40;
  std::string bmp(54 + pixels, '\0');
  auto put = [&](size_t offset, unsigned value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) bmp[offset + i] = static_cast<char>(value >> (8 * i));
  };
  bmp[0] = 'B';
  bmp[1] = 'M';
  put(2, bmp.size(), 4);
  put(10, 54, 4);
  put(14, 40, 4);
  put(18, 20, 4);
  put(22, 40, 4);
  put(26, 1, 2);
  put(28, 24, 2);
  put(34, pixels, 4);
  SdMan.registerFile(cachePath + "/" + std::to_string(std::hash<std::string>{}("OEBPS/pic.png")) + ".bmp", bmp);
}

class TimedEpubParser : public EpubChapterParser {
 public:
  using EpubChapterParser::EpubChapterParser;
  bool slow = false;
  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& callback, uint32_t maxPages,
                  const AbortCallback& abort) override {
    return EpubChapterParser::parsePages([&](std::unique_ptr<Page> page) {
      if (slow) testSetManualMillis(millis() + 1000);
      callback(std::move(page));
    }, maxPages, abort);
  }
};

}  // namespace

int main() {
  TestUtils::TestRunner runner("EpubIndexAnchors");
  papyrix::hal::Display display;
  GfxRenderer renderer(display);
  const RenderConfig config(1, 1.0f, 0, 0, 0, false, true, 400, 560, 11);

  for (char quote : {'\'', '"'}) {
    SdMan.reset();
    testResetLargestFreeBlock();
    std::string html = "<html><head><title>Continuous</title></head><body><p id='start'>FIRST ";
    std::string expectedWords = "FIRST ";
    for (int i = 0; i < 5450; ++i) {
      html += "aa bb cc dd ";
      expectedWords += "aa bb cc dd ";
    }
    html += "<a id='middle' href='#end' title=" + std::string(1, quote) + std::string(300, 'x') + "я" + quote +
            "><em>INSIDE</em></a> ";
    expectedWords += "INSIDE ";
    for (int i = 0; i < 6550; ++i) {
      html += "ee ff gg hh ";
      expectedWords += "ee ff gg hh ";
    }
    html += "LAST</p><img src='pic.png'/><p id='end' style='page-break-before:always'>FINAL</p></body></html>";
    expectedWords += "LAST FINAL ";
    SdMan.registerFile(bookPath, storedChapter(html));
    SdMan.registerFile("/direct.xhtml", html);
    auto epub = std::make_shared<Epub>(bookPath, "/cache");
    BookMetadataCache metadata(epub->getCachePath());
    runner.expectTrue(metadata.rebuildFromMemory({"Anchors", "", "en", "", ""}, {{chapterHref, -1}}, {}),
                      "Create native spine metadata");
    runner.expectTrue(epub->load(false), "Load native spine metadata");
    const std::string images = epub->getCachePath() + "/images";
    registerImage(images);

    Snapshot reference;
    std::string referenceWords;
    ChapterHtmlSlimParser direct("/direct.xhtml", renderer, config, [&](std::unique_ptr<Page> page) {
      appendPage(reference, *page);
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageLine) continue;
        for (const auto& word : static_cast<const PageLine&>(*element).getTextBlock().getWords()) {
          referenceWords += word.word + " ";
        }
      }
      return true;
    }, nullptr, "OEBPS/", images,
    [&](const std::string& href, Print& out, size_t chunk, BuildArena* scratch) {
      return epub->readItemContentsToStream(href, out, chunk, nullptr, scratch);
    });
    runner.expectTrue(direct.parseAndBuildPages() && !direct.wasAborted(), "Parse complete source XHTML");
    runner.expectEqual(expectedWords, referenceWords, "Every source word survives in order");
    runner.expectTrue(std::any_of(reference.begin(), reference.end(),
                                  [](const auto& row) { return row.find(" image=") != std::string::npos; }),
                      "The relative image remains in the chapter");
    const auto expectedAnchors = direct.getAnchorMap();
    runner.expectEq(size_t{3}, expectedAnchors.size(), "Each source target occurs exactly once");

    for (unsigned mode = 0; mode < 5; ++mode) {
      const unsigned batch = mode == 0 ? 0 : mode == 1 ? 1 : mode == 4 ? 30 : 5;
      const bool cold = mode >= 3;
      const std::string path = epub->getCachePath() + "/pages-" + std::to_string(mode) + ".bin";
      TimedEpubParser parser(epub, 0, renderer, config, images);
      auto cache = std::make_unique<PageCache>(path);
      bool ok = cache->create(parser, config, batch);
      if (mode == 4) {
        testSetManualMillis(0);
        parser.slow = true;
      }
      for (unsigned guard = 0; ok && cache->isPartial() && guard < 1000; ++guard) {
        if (!cold) runner.expectTrue(parser.canResume(), "A batch boundary retains the live parser");
        if (cold) {
          parser.reset();
          cache = std::make_unique<PageCache>(path);
          ok = cache->load(config);
        }
        ok = ok && cache->extend(parser, batch);
      }
      if (mode == 4) {
        runner.expectTrue(millis() > 20000, "Slow cold replay exceeds the stall timeout while making progress");
        testUseRealtimeMillis();
      }
      runner.expectTrue(ok && !cache->isPartial(), "Complete chapter in mode " + std::to_string(mode));
      Snapshot actual;
      for (uint32_t i = 0; i < cache->pageCount(); ++i) {
        auto page = cache->loadPage(i);
        runner.expectTrue(page != nullptr, "Load committed page");
        if (page) appendPage(actual, *page);
      }
      if (actual != reference) {
        const size_t common = std::min(actual.size(), reference.size());
        size_t at = 0;
        while (at < common && actual[at] == reference[at]) ++at;
        printf("First layout difference at row %zu (mode %u):\nexpected: %s\nactual: %s\n", at, mode,
               at < reference.size() ? reference[at].c_str() : "<end>",
               at < actual.size() ? actual[at].c_str() : "<end>");
      }
      runner.expectTrue(actual == reference, "No artificial section page in mode " + std::to_string(mode));
      runner.expectTrue(parser.getAnchorMap() == expectedAnchors, "Stable source anchor targets");
      parser.reset();
      runner.expectTrue(parser.getAnchorMap().empty(), "Reset clears previous anchors");
    }
    const std::string recoveryPath = epub->getCachePath() + "/recovery.bin";
    for (unsigned fault = 0; fault < 3; ++fault) {
      EpubChapterParser parser(epub, 0, renderer, config, images);
      PageCache cache(recoveryPath);
      runner.expectTrue(cache.create(parser, config, 5), "Create committed recovery batch");
      Snapshot savedPages;
      for (uint32_t page = 0; page < 5; ++page) appendPage(savedPages, *cache.loadPage(page));
      parser.reset();
      if (fault == 1) testSetLargestFreeBlock(1024);
      if (fault == 2) testSetManualMillis(0);
      cache.extend(parser, 5, [&]() {
        if (fault == 2) testSetManualMillis(millis() + 20001);
        return fault == 0;
      });
      testResetLargestFreeBlock();
      testUseRealtimeMillis();
      PageCache reloaded(recoveryPath);
      runner.expectTrue(reloaded.load(config) && reloaded.isPartial(), "Interruption preserves a partial cache");
      runner.expectEq(uint32_t{5}, reloaded.pageCount(), "Interruption keeps the committed count");
      Snapshot recoveredPages;
      for (uint32_t page = 0; page < reloaded.pageCount(); ++page) {
        auto loaded = reloaded.loadPage(page);
        runner.expectTrue(loaded != nullptr, "Committed page survives interruption");
        if (loaded) appendPage(recoveredPages, *loaded);
      }
      runner.expectTrue(savedPages == recoveredPages, "Interruption preserves committed page contents");
      parser.reset();
      for (unsigned guard = 0; reloaded.isPartial() && guard < 100; ++guard) {
        if (!reloaded.extend(parser, 50)) break;
      }
      Snapshot completed;
      for (uint32_t page = 0; page < reloaded.pageCount(); ++page) {
        auto loaded = reloaded.loadPage(page);
        if (loaded) appendPage(completed, *loaded);
      }
      runner.expectTrue(!reloaded.isPartial() && completed == reference, "Retry preserves every source page");
    }
    {
      unsigned pages = 0;
      ChapterHtmlSlimParser badRead("/direct.xhtml", renderer, config, [&](auto) { ++pages; return true; });
      SdMan.setFailBufferReadAfter(0);
      runner.expectFalse(badRead.parseAndBuildPages(), "Negative SD read fails parsing");
      runner.expectEq(0u, pages, "A failed SD read produces no page");
      SdMan.setFailBufferReadAfter(SIZE_MAX);
      ChapterHtmlSlimParser badSeek("/direct.xhtml", renderer, config, [&](auto) { ++pages; return false; });
      runner.expectTrue(badSeek.parseAndBuildPages() && badSeek.isSuspended(), "Prepare a suspended parser");
      const unsigned committed = pages;
      SdMan.setFailSeekAfter(0);
      runner.expectFalse(badSeek.resumeParsing(), "Failed resume seek stops parsing");
      runner.expectEq(committed, pages, "Failed resume seek cannot duplicate source pages");
      SdMan.setFailSeekAfter(SIZE_MAX);
    }
    const std::string sentinel = epub->getCachePath() + "/progress.bin";
    SdMan.registerFile(sentinel, "USER-PROGRESS");
    for (unsigned failure = 0; failure < 4; ++failure) {
      const uint32_t fingerprint = 100 + failure;
      const std::string finalPath =
          epub->getCachePath() + "/chapters/" + std::to_string(fingerprint) + "_0.xhtml";
      FsFile interrupted = SdMan.open((finalPath + ".tmp").c_str(), O_WRONLY | O_CREAT | O_TRUNC);
      interrupted.write("<html>INTERRUPTED", 17);
      interrupted.close();
      if (failure == 0) SdMan.setWriteLimit(10);
      if (failure == 1) SdMan.setSyncResult(false);
      if (failure == 2) SdMan.setRenameResult(false);
      unsigned checks = 0;
      auto abort = [&]() { return failure == 3 && ++checks > 30; };
      std::string prepared;
      runner.expectFalse(epub->prepareChapterHtml(0, fingerprint, prepared, nullptr, nullptr, abort),
                         "Failed preparation does not publish a chapter");
      runner.expectTrue(prepared.empty() && !SdMan.exists(finalPath.c_str()), "No incomplete final is reusable");
      SdMan.setWriteLimit(SIZE_MAX);
      SdMan.setSyncResult(true);
      SdMan.setRenameResult(true);
      runner.expectTrue(epub->prepareChapterHtml(0, fingerprint, prepared, nullptr, nullptr, nullptr),
                        "Preparation retries after interruption");
      runner.expectEqual(html, SdMan.getWrittenData(prepared), "Retry produces complete XHTML");
    }
    std::string prepared;
    SdMan.registerFile(bookPath, storedChapter("<html><body><p>Replacement</p></body></html>"));
    runner.expectTrue(epub->prepareChapterHtml(0, 12, prepared, nullptr, nullptr, nullptr), "Prepare new generation");
    runner.expectEqual("<html><body><p>Replacement</p></body></html>", SdMan.getWrittenData(prepared),
                       "A new source identity does not reuse old text");
    runner.expectTrue(epub->prepareChapterHtml(0, 0, prepared, nullptr, nullptr, nullptr), "Prepare unknown identity");
    SdMan.registerFile(bookPath, storedChapter("<html><body><p>Changed again</p></body></html>"));
    runner.expectTrue(epub->prepareChapterHtml(0, 0, prepared, nullptr, nullptr, nullptr), "Regenerate unknown identity");
    runner.expectEqual("<html><body><p>Changed again</p></body></html>", SdMan.getWrittenData(prepared),
                       "Fingerprint zero never reuses stale text");
    FsFile saved = SdMan.open(sentinel.c_str());
    char progress[14] = {};
    runner.expectEq(13, saved.read(progress, 13), "User progress remains readable");
    runner.expectEqual("USER-PROGRESS", std::string(progress, 13), "Preparation preserves user data");
  }
  return runner.allPassed() ? 0 : 1;
}
