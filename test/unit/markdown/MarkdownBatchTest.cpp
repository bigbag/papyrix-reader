#include "test_utils.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <MarkdownParser.h>
#include <Page.h>
#include <RenderConfig.h>
#include <SDCardManager.h>

#include <string>
#include <vector>

uint8_t GfxRenderer::frameBuffer_[EInkDisplay::BUFFER_SIZE];

void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

static constexpr int FONT_ID = 42;
static constexpr uint16_t VIEWPORT_W = 300;
static constexpr uint16_t VIEWPORT_H = 200;

// Fixed-width ASCII glyphs (space through '~', codepoints 32-126)
static const EpdGlyph testGlyphs[] = {
    // Each glyph: width=6, height=10, advanceX=7, left=0, top=10, dataLength=0, dataOffset=0
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},  // 95 glyphs total (32-126)
};

static const EpdUnicodeInterval testIntervals[] = {{32, 126, 0}};

static const EpdFontData testFontData = {
    nullptr,        // bitmap (not needed for width measurement)
    testGlyphs,     // glyph array
    testIntervals,  // intervals
    1,              // intervalCount
    14,             // advanceY (line height)
    11,             // ascender
    3,              // descender
    false,          // is2Bit
};

struct TestSetup {
  EInkDisplay display{0, 0, 0, 0, 0, 0};
  GfxRenderer gfx{display};
  EpdFont font{&testFontData};
  RenderConfig config;

  TestSetup() {
    gfx.begin();
    EpdFontFamily family(&font, &font, &font, &font);
    gfx.insertFont(FONT_ID, family);

    config.fontId = FONT_ID;
    config.viewportWidth = VIEWPORT_W;
    config.viewportHeight = VIEWPORT_H;
    config.paragraphAlignment = 0;
    config.spacingLevel = 1;
    config.lineCompression = 1.0f;
    config.hyphenation = false;
  }
};

static std::vector<std::string> collectWords(const std::vector<std::unique_ptr<Page>>& pages) {
  std::vector<std::string> words;
  for (auto& page : pages) {
    for (auto& elem : page->elements) {
      if (elem->getTag() == TAG_PageLine) {
        auto& tb = static_cast<PageLine*>(elem.get())->getTextBlock();
        for (auto& wd : tb.getWords()) {
          words.push_back(wd.word);
        }
      }
    }
  }
  return words;
}

static std::vector<std::unique_ptr<Page>> parseAll(MarkdownParser& parser, uint16_t maxPages) {
  std::vector<std::unique_ptr<Page>> allPages;

  auto onPage = [&](std::unique_ptr<Page> page) { allPages.push_back(std::move(page)); };

  parser.parsePages(onPage, maxPages);
  while (parser.hasMoreContent()) {
    parser.parsePages(onPage, maxPages);
  }
  return allPages;
}

static std::string makeMarkdown(int paragraphs, int wordsPerParagraph) {
  std::string md;
  for (int p = 0; p < paragraphs; p++) {
    for (int w = 0; w < wordsPerParagraph; w++) {
      if (w > 0) md += ' ';
      md += "word";
      md += std::to_string(p * wordsPerParagraph + w);
    }
    md += "\n\n";
  }
  return md;
}

int main() {
  TestUtils::TestRunner runner("Markdown Batch Parsing");

  // Test 1: Batched vs unbatched produce identical word output
  {
    TestSetup setup;
    std::string content = makeMarkdown(20, 30);
    const std::string path = "/test_batch.md";

    SdMan.registerFile(path, content);
    MarkdownParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedPages = parseAll(unbatched, 0);
    auto unbatchedWords = collectWords(unbatchedPages);

    SdMan.registerFile(path, content);
    MarkdownParser batched(path, setup.gfx, setup.config);
    auto batchedPages = parseAll(batched, 3);
    auto batchedWords = collectWords(batchedPages);

    runner.expectTrue(unbatchedPages.size() > 3, "unbatched: multiple pages");
    runner.expectTrue(batchedPages.size() >= unbatchedPages.size(),
                      "batch_eq: batched has at least as many pages");
    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "batch_eq: same word count");

    bool allMatch = true;
    int firstMismatch = -1;
    for (size_t i = 0; i < unbatchedWords.size() && i < batchedWords.size(); i++) {
      if (unbatchedWords[i] != batchedWords[i]) {
        allMatch = false;
        firstMismatch = static_cast<int>(i);
        break;
      }
    }
    runner.expectTrue(allMatch,
                      firstMismatch >= 0
                          ? ("batch_eq: words match (first mismatch at " + std::to_string(firstMismatch) + ": '" +
                             unbatchedWords[firstMismatch] + "' vs '" + batchedWords[firstMismatch] + "')")
                          : "batch_eq: words match");
  }

  // Test 2: Small batch size (1 page per batch) — stress test resume logic
  {
    TestSetup setup;
    std::string content = makeMarkdown(10, 20);
    const std::string path = "/test_batch1.md";

    SdMan.registerFile(path, content);
    MarkdownParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    MarkdownParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 1));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "batch1: same word count");
    bool allMatch = unbatchedWords == batchedWords;
    runner.expectTrue(allMatch, "batch1: all words identical");
  }

  // Test 3: Heading at paragraph boundary
  {
    TestSetup setup;
    std::string content;
    for (int i = 0; i < 15; i++) {
      content += "Paragraph " + std::to_string(i) + " with several words to fill the page layout ";
      content += "and some more text to make it wrap across lines properly.\n\n";
    }
    content += "### Section Heading\n\n";
    for (int i = 0; i < 10; i++) {
      content += "After heading paragraph " + std::to_string(i) + " with more words here.\n\n";
    }
    const std::string path = "/test_heading.md";

    SdMan.registerFile(path, content);
    MarkdownParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    MarkdownParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 3));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "heading: same word count");

    bool hasSection = false;
    bool hasAfter = false;
    for (auto& w : batchedWords) {
      if (w == "Section") hasSection = true;
      if (w == "After") hasAfter = true;
    }
    runner.expectTrue(hasSection, "heading: 'Section' present in batched output");
    runner.expectTrue(hasAfter, "heading: 'After' present in batched output");

    bool allMatch = unbatchedWords == batchedWords;
    runner.expectTrue(allMatch, "heading: all words identical");
  }

  // Test 4: List items across batch boundaries
  {
    TestSetup setup;
    std::string content;
    for (int i = 0; i < 8; i++) {
      content += "Filler paragraph " + std::to_string(i) + " to push the list further down.\n\n";
    }
    for (int i = 0; i < 10; i++) {
      content += "- List item " + std::to_string(i) + " with some extra text\n";
    }
    content += "\n";
    const std::string path = "/test_list.md";

    SdMan.registerFile(path, content);
    MarkdownParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    MarkdownParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 2));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "list: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "list: all words identical");
  }

  // Test 5: Multiple batch resumes (many batches)
  {
    TestSetup setup;
    std::string content = makeMarkdown(50, 20);
    const std::string path = "/test_many_batches.md";

    SdMan.registerFile(path, content);
    MarkdownParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    MarkdownParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 2));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "many_batches: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "many_batches: all words identical");
  }

  // Test 6: Blockquote across batch boundary
  {
    TestSetup setup;
    std::string content;
    for (int i = 0; i < 10; i++) {
      content += "Filler text paragraph " + std::to_string(i) + " to fill pages before quote.\n\n";
    }
    content += "> This is a blockquote that should survive batching intact.\n\n";
    content += "Normal text after blockquote.\n\n";
    const std::string path = "/test_quote.md";

    SdMan.registerFile(path, content);
    MarkdownParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    MarkdownParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 3));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "blockquote: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "blockquote: all words identical");
  }

  return runner.allPassed() ? 0 : 1;
}
