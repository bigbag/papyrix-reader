#include <BookMetadataCache.h>
#include <Display.h>
#include <Epub.h>
#include <EpubChapterParser.h>
#include <GfxRenderer.h>
#include <HtmlSplitter.h>
#include <PageCache.h>
#include <RenderConfig.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <algorithm>
#include <memory>
#include <string>

#include "test_utils.h"

int main() {
  TestUtils::TestRunner runner("EpubIndexAnchors");
  SdMan.reset();
  testResetLargestFreeBlock();

  auto epub = std::make_shared<Epub>("/books/anchors.epub", "/cache");
  BookMetadataCache metadata(epub->getCachePath());
  runner.expectTrue(metadata.rebuildFromMemory({"Anchors", "", "en", "", ""}, {{"chapter.xhtml", -1}}, {}),
                    "Build the native spine cache");
  runner.expectTrue(epub->load(false), "Load the native spine cache");

  const std::string sections = epub->getCachePath() + "/sections/";
  std::string body;
  const std::string word(600, 'x');
  for (int i = 0; i < 120; ++i) {
    body += "<p id=\"anchor-" + std::to_string(i) + "\">" + word + "</p>\n";
  }
  SdMan.registerFile(sections + "0.body", body);
  SdMan.registerFile(sections + "0.base", "");
  runner.expectTrue(html5::scanSectionIndex(sections + "0.body", sections + "0.idx", 0, body.size(), 24 * 1024, "") > 1,
                    "Split the spine into multiple native sub-sections");
  html5::SectionIndex sectionIndex;
  runner.expectTrue(html5::readSectionIndex(sections + "0.idx", sectionIndex), "Read the subsection boundaries");
  size_t expectedAnchorRows = 120;
  for (size_t i = 1; i < sectionIndex.sections.size(); ++i) {
    for (const auto& tag : sectionIndex.sections[i - 1].tagStack) {
      if (tag.find(" id=\"anchor-") != std::string::npos) ++expectedAnchorRows;
    }
  }

  papyrix::hal::Display display;
  GfxRenderer renderer(display);
  const RenderConfig config(0, 1.0f, 0, 0, 0, false, false, 80, 24);
  EpubChapterParser parser(epub, 0, renderer, config);
  PageCache cache(sections + "0.bin");
  runner.expectTrue(cache.create(parser, config, 50), "Create the first page batch");
  auto checkPartialAnchors = [&runner, &parser]() {
    const auto& anchors = parser.getAnchorMap();
    const auto first =
        std::find_if(anchors.begin(), anchors.end(), [](const auto& entry) { return entry.first == "anchor-0"; });
    runner.expectTrue(first != anchors.end(), "A partial snapshot retains the first link target");
    if (first != anchors.end()) {
      runner.expectEq(uint32_t{0}, first->second, "A partial first-match target stays on page zero");
    }
    const auto later =
        std::find_if(anchors.begin(), anchors.end(), [](const auto& entry) { return entry.first == "anchor-49"; });
    runner.expectTrue(later != anchors.end() && later->second >= 40,
                      "A later subsection target uses a chapter-wide page offset");
    return anchors;
  };
  auto savedAnchors = checkPartialAnchors();
  bool complete = true;
  for (int batch = 0; cache.isPartial() && batch < 10; ++batch) {
    const auto before = cache.pageCount();
    if (!cache.extend(parser, 50) || cache.pageCount() <= before) {
      complete = false;
      break;
    }
    const auto& nextAnchors = parser.getAnchorMap();
    runner.expectTrue(nextAnchors.size() >= savedAnchors.size() &&
                          std::equal(savedAnchors.begin(), savedAnchors.end(), nextAnchors.begin()),
                      "A later snapshot preserves earlier rows and first-match order");
    if (cache.isPartial()) savedAnchors = checkPartialAnchors();
  }
  runner.expectTrue(complete && !cache.isPartial(), "Complete the split spine in page batches");

  const auto& anchors = parser.getAnchorMap();
  runner.expectEq(expectedAnchorRows, anchors.size(), "Resume captures each source or reopened target only once");
  for (int i = 0; i < 120; ++i) {
    const std::string id = "anchor-" + std::to_string(i);
    const auto anchor =
        std::find_if(anchors.begin(), anchors.end(), [&id](const auto& entry) { return entry.first == id; });
    runner.expectTrue(anchor != anchors.end(), ("Retain link target " + id).c_str());
    if (i == 0 && anchor != anchors.end()) {
      runner.expectEq(uint32_t{0}, anchor->second, "The first link target resolves to page zero");
    }
  }
  parser.reset();
  runner.expectTrue(parser.getAnchorMap().empty(), "Reset removes targets from the previous parse");
  return runner.allPassed() ? 0 : 1;
}
