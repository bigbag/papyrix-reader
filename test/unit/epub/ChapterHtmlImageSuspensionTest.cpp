#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Page.h>
#include <SDCardManager.h>

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "test_utils.h"

uint8_t GfxRenderer::frameBuffer_[papyrix::hal::Display::BUFFER_SIZE];

namespace {
using Pages = std::vector<std::unique_ptr<Page>>;
constexpr const char* CHAPTER = "/book/chapter.xhtml";
constexpr const char* CACHE = "/cache/images";
const auto glyphs = [] {
  std::array<EpdGlyph, 95> values{};
  for (auto& glyph : values) {
    glyph.width = 6;
    glyph.height = 10;
    glyph.advanceX = 7;
    glyph.top = 10;
  }
  return values;
}();
const EpdUnicodeInterval intervals[] = {{32, 126, 0}};
const EpdFontData fontData = {nullptr, glyphs.data(), intervals, 1, 14, 10, 3, false};

void cacheImage(const std::string& source, unsigned height) {
  constexpr unsigned width = 20;
  const unsigned pixels = ((width * 3 + 3) / 4) * 4 * height;
  std::string bmp(54 + pixels, '\0');
  const auto put = [&bmp](size_t offset, unsigned value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) bmp[offset + i] = static_cast<char>(value >> (8 * i));
  };
  bmp[0] = 'B';
  bmp[1] = 'M';
  put(2, bmp.size(), 4);
  put(10, 54, 4);
  put(14, 40, 4);
  put(18, width, 4);
  put(22, height, 4);
  put(26, 1, 2);
  put(28, 24, 2);
  put(34, pixels, 4);
  const auto path = FsHelpers::normalisePath("/book/" + source);
  SdMan.registerFile(std::string(CACHE) + "/" + std::to_string(std::hash<std::string>{}(path)) + ".bmp", bmp);
}

std::string filler(unsigned count, bool inset = false) {
  std::string result;
  for (unsigned i = 0; i < count; ++i) {
    result += inset ? "<p style=\"margin-left:14px\">" : "<p>";
    result += "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp</p>";
  }
  return result;
}

bool parse(GfxRenderer& renderer, const RenderConfig& config, const std::string& body, unsigned batch, Pages& pages) {
  SdMan.registerFile(CHAPTER, "<html><body>" + body + "</body></html>");
  unsigned created = 0;
  bool full = false;
  ChapterHtmlSlimParser parser(
      CHAPTER, renderer, config,
      [&](std::unique_ptr<Page> page) {
        if (full) return false;
        pages.push_back(std::move(page));
        full = batch != 0 && ++created >= batch;
        return !full;
      },
      nullptr, "/book/", CACHE, [](const std::string&, Print&, size_t, BuildArena*) { return false; });
  if (!parser.parseAndBuildPages()) return false;
  for (unsigned guard = 0; parser.isSuspended() && guard < 128; ++guard) {
    created = 0;
    full = false;
    if (!parser.resumeParsing()) return false;
  }
  return !parser.isSuspended() && !parser.wasAborted();
}

std::vector<std::string> snapshot(const Pages& pages) {
  std::vector<std::string> result;
  for (size_t index = 0; index < pages.size(); ++index) {
    result.push_back("page " + std::to_string(index));
    for (const auto& element : pages[index]->elements) {
      std::string line = std::to_string(element->xPos) + "," + std::to_string(element->yPos);
      if (element->getTag() == TAG_PageImage) {
        const auto& image = static_cast<const PageImage&>(*element).getImageBlock();
        line += " image " + image.getCachedBmpPath() + " " + std::to_string(image.getWidth()) + "x" +
                std::to_string(image.getHeight());
      } else {
        const auto& block = static_cast<const PageLine&>(*element).getTextBlock();
        line += " text " + std::to_string(block.getStyle());
        for (const auto& word : block.getWords()) {
          line += " [" + word.word + ":" + std::to_string(word.style) + ":" + std::to_string(word.xPos) + "]";
        }
      }
      result.push_back(std::move(line));
    }
  }
  return result;
}

std::vector<std::string> markers(const Pages& pages, const std::vector<std::string>& wanted) {
  std::vector<std::string> result;
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() == TAG_PageImage) {
        result.push_back("IMAGE");
      } else {
        for (const auto& word : static_cast<const PageLine&>(*element).getTextBlock().getWords()) {
          if (std::find(wanted.begin(), wanted.end(), word.word) != wanted.end()) result.push_back(word.word);
        }
      }
    }
  }
  return result;
}

bool centeredPlaceholder(const Pages& pages, const std::string& expected) {
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& block = static_cast<const PageLine&>(*element).getTextBlock();
      for (const auto& word : block.getWords()) {
        if (word.word == expected)
          return block.getStyle() == TextBlock::CENTER_ALIGN && word.style == EpdFontFamily::ITALIC;
      }
    }
  }
  return false;
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("Chapter HTML suspension");
  SdMan.reset();
  papyrix::hal::Display display(0, 0, 0, 0, 0, 0);
  GfxRenderer renderer(display);
  EpdFont font(&fontData);
  renderer.insertFont(1, EpdFontFamily(&font, &font, &font, &font));
  const RenderConfig config(1, 1.0f, 0, 1, 0, false, true, 300, 120);
  cacheImage("tall.png", 90);
  cacheImage("small.png", 40);
  const struct {
    const char* name;
    const char* markup;
    const char* expected;
  } cases[] = {
      {"tall", "<img src=\"tall.png\"/>", "IMAGE"},
      {"small", "<img src=\"small.png\"/>", "IMAGE"},
      {"fallback", "<img src=\"missing.png\" alt=\"missing image label\"/>", "[Image: missing image label]"},
      {"table", "<table><tr><td>CELL</td></tr></table>", "[Table omitted]"},
  };
  for (const auto& item : cases) {
    for (unsigned count = 0; count <= 12; ++count) {
      const auto label = std::string(item.name) + " boundary " + std::to_string(count);
      const auto body = filler(count, true) + "<div>tailword" + item.markup + "</div><p>afterword</p>" + filler(2);
      const std::vector<std::string> expected = {"tailword", item.expected, "afterword"};
      Pages reference;
      runner.expectTrue(parse(renderer, config, body, 0, reference), label + " parses without a batch limit");
      runner.expectTrue(markers(reference, expected) == expected, label + " retains content once in document order");
      for (unsigned batch : {1U, 2U, 5U}) {
        const auto batchLabel = label + " batch " + std::to_string(batch);
        Pages batched;
        runner.expectTrue(parse(renderer, config, body, batch, batched), batchLabel + " resumes to completion");
        runner.expectTrue(snapshot(batched) == snapshot(reference),
                          batchLabel + " preserves content, style and geometry");
        if (std::string(item.expected) != "IMAGE") {
          runner.expectTrue(centeredPlaceholder(batched, item.expected),
                            batchLabel + " keeps the placeholder centered and italic");
        }
      }
    }
  }

  const auto body = filler(6) + "<div>first<img src=\"tall.png\"/></div>" + filler(6) +
                    "<div>second<img src=\"small.png\"/></div>" + filler(6) + "<p>afterword</p>";
  const std::vector<std::string> expected = {"first", "IMAGE", "second", "IMAGE", "afterword"};
  Pages reference;
  runner.expectTrue(parse(renderer, config, body, 0, reference), "multiple images parse without a batch limit");
  runner.expectTrue(markers(reference, expected) == expected, "multiple images retain document order and occur once");
  Pages batched;
  runner.expectTrue(parse(renderer, config, body, 1, batched), "multiple images resume through repeated stops");
  runner.expectTrue(snapshot(batched) == snapshot(reference), "repeated stops retain every page and element");

  SdMan.registerFile(CHAPTER, "<html><body><div>tailword<img src=\"tall.png\"/></div><p>afterword</p></body></html>");
  bool abort = false;
  Pages cancelled;
  ChapterHtmlSlimParser parser(
      CHAPTER, renderer, config,
      [&](std::unique_ptr<Page> page) {
        cancelled.push_back(std::move(page));
        return false;
      },
      nullptr, "/book/", CACHE, [](const std::string&, Print&, size_t, BuildArena*) { return false; }, nullptr,
      [&] { return abort; });
  runner.expectTrue(parser.parseAndBuildPages() && parser.isSuspended(),
                    "image insertion can suspend after preceding text");
  abort = true;
  runner.expectTrue(parser.resumeParsing() && parser.wasAborted() && !parser.isSuspended(),
                    "cancellation ends a suspended parse");
  runner.expectTrue(markers(cancelled, {"tailword", "afterword"}) == std::vector<std::string>{"tailword"},
                    "cancellation does not publish the pending image or later text");
  runner.expectFalse(parser.resumeParsing(), "cancelled parser cannot resume");
  return runner.allPassed() ? 0 : 1;
}
