#include "test_utils.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Page.h>
#include <ParsedText.h>
#include <PlainTextParser.h>
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

static const EpdGlyph testGlyphs[] = {
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
    {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0}, {6, 10, 7, 0, 10, 0, 0},
};

static const EpdUnicodeInterval testIntervals[] = {{32, 126, 0}};

static const EpdFontData testFontData = {
    nullptr, testGlyphs, testIntervals, 1, 14, 11, 3, false,
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

static std::vector<std::unique_ptr<Page>> parseAll(PlainTextParser& parser, uint16_t maxPages) {
  std::vector<std::unique_ptr<Page>> allPages;

  auto onPage = [&](std::unique_ptr<Page> page) { allPages.push_back(std::move(page)); };

  parser.parsePages(onPage, maxPages);
  while (parser.hasMoreContent()) {
    parser.parsePages(onPage, maxPages);
  }
  return allPages;
}

static std::string makePlainText(int paragraphs, int wordsPerParagraph) {
  std::string text;
  for (int p = 0; p < paragraphs; p++) {
    for (int w = 0; w < wordsPerParagraph; w++) {
      if (w > 0) text += ' ';
      text += "word";
      text += std::to_string(p * wordsPerParagraph + w);
    }
    text += "\n\n";
  }
  return text;
}

int main() {
  TestUtils::TestRunner runner("TXT Batch Parsing");

  // Test 1: Batched vs unbatched produce identical word output
  {
    TestSetup setup;
    std::string content = makePlainText(20, 30);
    const std::string path = "/test_batch.txt";

    SdMan.registerFile(path, content);
    PlainTextParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedPages = parseAll(unbatched, 0);
    auto unbatchedWords = collectWords(unbatchedPages);

    SdMan.registerFile(path, content);
    PlainTextParser batched(path, setup.gfx, setup.config);
    auto batchedPages = parseAll(batched, 3);
    auto batchedWords = collectWords(batchedPages);

    runner.expectTrue(unbatchedPages.size() > 3, "unbatched: multiple pages");
    runner.expectEq(static_cast<int>(unbatchedPages.size()), static_cast<int>(batchedPages.size()),
                    "batch_eq: same page count");
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
    std::string content = makePlainText(10, 20);
    const std::string path = "/test_batch1.txt";

    SdMan.registerFile(path, content);
    PlainTextParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    PlainTextParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 1));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "batch1: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "batch1: all words identical");
  }

  // Test 3: Multiple batch resumes (many batches)
  {
    TestSetup setup;
    std::string content = makePlainText(50, 20);
    const std::string path = "/test_many_batches.txt";

    SdMan.registerFile(path, content);
    PlainTextParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    PlainTextParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 2));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "many_batches: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "many_batches: all words identical");
  }

  // Test 4: No nearly-empty pages at batch boundaries
  {
    TestSetup setup;
    std::string content = makePlainText(30, 25);
    const std::string path = "/test_no_sparse.txt";

    SdMan.registerFile(path, content);
    PlainTextParser parser(path, setup.gfx, setup.config);
    auto pages = parseAll(parser, 3);

    runner.expectTrue(pages.size() > 5, "no_sparse: enough pages to test");

    bool foundSparse = false;
    int sparsePage = -1;
    for (size_t i = 0; i < pages.size() - 1; i++) {
      if (pages[i]->elements.size() <= 2) {
        foundSparse = true;
        sparsePage = static_cast<int>(i);
        break;
      }
    }
    runner.expectTrue(!foundSparse,
                      sparsePage >= 0
                          ? ("no_sparse: page " + std::to_string(sparsePage) + " has only " +
                             std::to_string(pages[sparsePage]->elements.size()) + " element(s)")
                          : "no_sparse: all non-final pages have >2 elements");
  }

  // Test 5: Paragraph break at batch boundary preserves spacing
  {
    TestSetup setup;
    std::string content;
    for (int i = 0; i < 15; i++) {
      content += "Paragraph " + std::to_string(i) + " with several words to fill the page layout ";
      content += "and some more text to make it wrap across lines properly.\n\n";
    }
    const std::string path = "/test_para_break.txt";

    SdMan.registerFile(path, content);
    PlainTextParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    PlainTextParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 3));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "para_break: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "para_break: all words identical");
  }

  // Test 6: Mixed paragraphs with varying lengths across batch boundaries
  {
    TestSetup setup;
    std::string content;
    for (int i = 0; i < 10; i++) {
      content += "Short paragraph " + std::to_string(i) + ".\n\n";
    }
    for (int i = 0; i < 5; i++) {
      content += "Long paragraph " + std::to_string(i) + " with many extra words added here to ";
      content += "fill up lines and push text across multiple page boundaries for testing.\n\n";
    }
    for (int i = 0; i < 10; i++) {
      content += "Trailing paragraph " + std::to_string(i) + ".\n\n";
    }
    const std::string path = "/test_mixed.txt";

    SdMan.registerFile(path, content);
    PlainTextParser unbatched(path, setup.gfx, setup.config);
    auto unbatchedWords = collectWords(parseAll(unbatched, 0));

    SdMan.registerFile(path, content);
    PlainTextParser batched(path, setup.gfx, setup.config);
    auto batchedWords = collectWords(parseAll(batched, 2));

    runner.expectEq(static_cast<int>(unbatchedWords.size()), static_cast<int>(batchedWords.size()),
                    "mixed: same word count");
    runner.expectTrue(unbatchedWords == batchedWords, "mixed: all words identical");
  }

  return runner.allPassed() ? 0 : 1;
}
