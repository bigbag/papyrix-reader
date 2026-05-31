// Regression test for Issue #137: a plain-text file whose "paragraphs" are
// huge (a git-patch / source-code block: hundreds of consecutive non-blank
// lines joined into one paragraph) made the TXT parser accumulate a single
// ParsedText of 1000+ words and lay it out in one shot. The transient heap for
// that one paragraph exhausted the device's ~150 KB heap and rebooted it.
//
// The device can't be unit-tested directly, so we reproduce the *mechanism*:
// parse a single giant paragraph while measuring the peak transient heap above
// a baseline, and assert it stays bounded. Without the per-block cap this peak
// scales with the paragraph (hundreds of KB) and the assertion fails.

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Page.h>
#include <ParsedText.h>
#include <PlainTextParser.h>
#include <RenderConfig.h>
#include <SDCardManager.h>

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "test_utils.h"

uint8_t GfxRenderer::frameBuffer_[EInkDisplay::BUFFER_SIZE];

void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

// ---- Heap peak tracker (per-test binary, so global new/delete is safe) ------
namespace {
size_t g_liveBytes = 0;
size_t g_peakBytes = 0;
bool g_tracking = false;
constexpr size_t kHeader = alignof(std::max_align_t);

void* trackedAlloc(size_t n) {
  void* base = std::malloc(n + kHeader);
  if (!base) return nullptr;
  *static_cast<size_t*>(base) = n;
  g_liveBytes += n;
  if (g_tracking && g_liveBytes > g_peakBytes) g_peakBytes = g_liveBytes;
  return static_cast<char*>(base) + kHeader;
}
void trackedFree(void* p) {
  if (!p) return;
  void* base = static_cast<char*>(p) - kHeader;
  g_liveBytes -= *static_cast<size_t*>(base);
  std::free(base);
}
}  // namespace

void* operator new(std::size_t n) {
  void* p = trackedAlloc(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n) {
  void* p = trackedAlloc(n);
  if (!p) throw std::bad_alloc();
  return p;
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return trackedAlloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return trackedAlloc(n); }
void operator delete(void* p) noexcept { trackedFree(p); }
void operator delete[](void* p) noexcept { trackedFree(p); }
void operator delete(void* p, std::size_t) noexcept { trackedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { trackedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { trackedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { trackedFree(p); }

namespace {
size_t trackBegin() {
  g_peakBytes = g_liveBytes;
  g_tracking = true;
  return g_liveBytes;
}
size_t trackEnd(size_t baseline) {
  g_tracking = false;
  return g_peakBytes - baseline;
}
}  // namespace
// -----------------------------------------------------------------------------

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

  TestSetup(bool hyphenation) {
    gfx.begin();
    EpdFontFamily family(&font, &font, &font, &font);
    gfx.insertFont(FONT_ID, family);

    config.fontId = FONT_ID;
    config.viewportWidth = VIEWPORT_W;
    config.viewportHeight = VIEWPORT_H;
    config.paragraphAlignment = 3;  // JUSTIFIED — exercises the justified path
    config.spacingLevel = 1;
    config.lineCompression = 1.0f;
    config.hyphenation = hyphenation;
  }
};

// One giant paragraph: N words on consecutive non-blank lines (no blank line),
// exactly the structure of a git-patch diff block in Issue #137.
static std::string makeGiantParagraph(int n) {
  std::string text;
  text.reserve(static_cast<size_t>(n) * 9);
  for (int i = 0; i < n; i++) {
    text += "word";
    text += std::to_string(i);
    text += '\n';
  }
  return text;
}

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

static std::vector<std::string> expectedWords(int n) {
  std::vector<std::string> words;
  words.reserve(n);
  for (int i = 0; i < n; i++) {
    words.push_back("word" + std::to_string(i));
  }
  return words;
}

int main() {
  TestUtils::TestRunner runner("TXT Large Paragraph (Issue #137)");

  // Test 1: peak transient heap stays bounded while laying out a giant paragraph.
  // Pages are dropped immediately (as the device does — it flushes to SD), so the
  // measured peak reflects the parser's per-paragraph working set, not retained
  // output. Without the cap this peak scales with the paragraph (~360 KB for
  // 5000 words) and exceeds the bound.
  {
    constexpr int kWords = 5000;
    TestSetup setup(/*hyphenation=*/false);
    std::string content = makeGiantParagraph(kWords);
    const std::string path = "/giant_para.txt";
    SdMan.registerFile(path, content);

    PlainTextParser parser(path, setup.gfx, setup.config);

    int pageCount = 0;
    auto onPage = [&](std::unique_ptr<Page>) { pageCount++; };  // drop immediately

    size_t baseline = trackBegin();
    parser.parsePages(onPage, 0);
    while (parser.hasMoreContent()) {
      parser.parsePages(onPage, 0);
    }
    size_t peakDelta = trackEnd(baseline);

    runner.expectTrue(pageCount > 1, "giant: produced multiple pages");

    // Bound chosen to sit well above the fixed per-block working set (~85 KB on a
    // 64-bit host, ~half that on the 32-bit device) yet far below the unbounded
    // peak this paragraph used to reach (~430 KB and growing with paragraph size).
    constexpr size_t kBoundBytes = 192 * 1024;
    runner.expectTrue(peakDelta < kBoundBytes, "giant: peak transient bounded (" + std::to_string(peakDelta / 1024) +
                                                   " KB, limit " + std::to_string(kBoundBytes / 1024) + " KB)");
  }

  // Test 2: no word is lost or duplicated when a huge paragraph is split.
  {
    constexpr int kWords = 3000;
    TestSetup setup(/*hyphenation=*/false);
    std::string content = makeGiantParagraph(kWords);
    const std::string path = "/giant_roundtrip.txt";
    SdMan.registerFile(path, content);

    PlainTextParser parser(path, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> pages;
    auto onPage = [&](std::unique_ptr<Page> p) { pages.push_back(std::move(p)); };
    parser.parsePages(onPage, 0);
    while (parser.hasMoreContent()) {
      parser.parsePages(onPage, 0);
    }

    auto got = collectWords(pages);
    runner.expectTrue(got == expectedWords(kWords), "roundtrip: all words present and in order");
  }

  // Test 3: batched and unbatched parses of a huge paragraph yield identical
  // words (the cap-driven mid-paragraph flush must survive batch resume).
  {
    constexpr int kWords = 3000;
    TestSetup setup(/*hyphenation=*/false);
    std::string content = makeGiantParagraph(kWords);

    const std::string p1 = "/giant_unbatched.txt";
    SdMan.registerFile(p1, content);
    PlainTextParser unbatched(p1, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> uPages;
    auto onU = [&](std::unique_ptr<Page> p) { uPages.push_back(std::move(p)); };
    unbatched.parsePages(onU, 0);
    while (unbatched.hasMoreContent()) unbatched.parsePages(onU, 0);

    const std::string p2 = "/giant_batched.txt";
    SdMan.registerFile(p2, content);
    PlainTextParser batched(p2, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> bPages;
    auto onB = [&](std::unique_ptr<Page> p) { bPages.push_back(std::move(p)); };
    batched.parsePages(onB, 2);
    while (batched.hasMoreContent()) batched.parsePages(onB, 2);

    runner.expectTrue(collectWords(uPages) == collectWords(bPages), "batch_eq: identical words across batches");
  }

  // Test 4: with hyphenation enabled (preSplit duplicates the word list) the peak
  // must still stay bounded.
  {
    constexpr int kWords = 5000;
    TestSetup setup(/*hyphenation=*/true);
    std::string content = makeGiantParagraph(kWords);
    const std::string path = "/giant_hyphen.txt";
    SdMan.registerFile(path, content);

    PlainTextParser parser(path, setup.gfx, setup.config);
    int pageCount = 0;
    auto onPage = [&](std::unique_ptr<Page>) { pageCount++; };

    size_t baseline = trackBegin();
    parser.parsePages(onPage, 0);
    while (parser.hasMoreContent()) parser.parsePages(onPage, 0);
    size_t peakDelta = trackEnd(baseline);

    runner.expectTrue(pageCount > 1, "giant_hyphen: produced multiple pages");
    constexpr size_t kBoundBytes = 192 * 1024;
    runner.expectTrue(peakDelta < kBoundBytes, "giant_hyphen: peak transient bounded (" +
                                                   std::to_string(peakDelta / 1024) + " KB, limit " +
                                                   std::to_string(kBoundBytes / 1024) + " KB)");
  }

  return runner.allPassed() ? 0 : 1;
}
