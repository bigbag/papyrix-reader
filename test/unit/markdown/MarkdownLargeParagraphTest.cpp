// Regression test for Issue #137 (Markdown sibling of the TXT case): a markdown
// "paragraph" made of hundreds of consecutive non-blank lines accumulates into a
// single block of thousands of words. The parser must drain it at the word cap
// so the transient heap stays bounded instead of exhausting the device heap.

#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <MarkdownParser.h>
#include <Page.h>
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

  explicit TestSetup(bool hyphenation) {
    gfx.begin();
    EpdFontFamily family(&font, &font, &font, &font);
    gfx.insertFont(FONT_ID, family);

    config.fontId = FONT_ID;
    config.viewportWidth = VIEWPORT_W;
    config.viewportHeight = VIEWPORT_H;
    config.paragraphAlignment = 3;  // JUSTIFIED
    config.spacingLevel = 1;
    config.lineCompression = 1.0f;
    config.hyphenation = hyphenation;
  }
};

// One giant markdown paragraph: N space-separated words soft-wrapped across many
// non-blank lines (10 words per line, no blank line) so the parser merges them
// into a single block — a realistic "wall of text" that trips Issue #137. Every
// word is followed by a space (the newline only wraps), so each word flushes
// cleanly at the space — avoiding an unrelated soft-break word-join quirk.
static std::string makeGiantParagraph(int n) {
  std::string md;
  md.reserve(static_cast<size_t>(n) * 10);
  for (int i = 0; i < n; i++) {
    md += "word";
    md += std::to_string(i);
    md += ' ';
    if (i % 10 == 9) md += '\n';
  }
  return md;
}

static std::vector<std::string> expectedWords(int n) {
  std::vector<std::string> v;
  v.reserve(n);
  for (int i = 0; i < n; i++) v.push_back("word" + std::to_string(i));
  return v;
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

int main() {
  TestUtils::TestRunner runner("Markdown Large Paragraph (Issue #137)");

  // Test 1: peak transient bounded while draining a giant paragraph.
  {
    constexpr int kWords = 5000;
    TestSetup setup(/*hyphenation=*/false);
    std::string content = makeGiantParagraph(kWords);
    const std::string path = "/giant_para.md";
    SdMan.registerFile(path, content);

    MarkdownParser parser(path, setup.gfx, setup.config);
    int pageCount = 0;
    auto onPage = [&](std::unique_ptr<Page>) { pageCount++; };

    size_t baseline = trackBegin();
    parser.parsePages(onPage, 0);
    while (parser.hasMoreContent()) parser.parsePages(onPage, 0);
    size_t peakDelta = trackEnd(baseline);

    runner.expectTrue(pageCount > 1, "giant: produced multiple pages");
    constexpr size_t kBoundBytes = 192 * 1024;
    runner.expectTrue(peakDelta < kBoundBytes, "giant: peak transient bounded (" + std::to_string(peakDelta / 1024) +
                                                   " KB, limit " + std::to_string(kBoundBytes / 1024) + " KB)");
  }

  // Test 2: batched and unbatched drain of a huge paragraph yield identical words.
  {
    constexpr int kWords = 3000;
    TestSetup setup(/*hyphenation=*/false);
    std::string content = makeGiantParagraph(kWords);

    const std::string p1 = "/giant_unbatched.md";
    SdMan.registerFile(p1, content);
    MarkdownParser unbatched(p1, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> uPages;
    auto onU = [&](std::unique_ptr<Page> p) { uPages.push_back(std::move(p)); };
    unbatched.parsePages(onU, 0);
    while (unbatched.hasMoreContent()) unbatched.parsePages(onU, 0);
    auto uWords = collectWords(uPages);

    const std::string p2 = "/giant_batched.md";
    SdMan.registerFile(p2, content);
    MarkdownParser batched(p2, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> bPages;
    auto onB = [&](std::unique_ptr<Page> p) { bPages.push_back(std::move(p)); };
    batched.parsePages(onB, 2);
    while (batched.hasMoreContent()) batched.parsePages(onB, 2);
    auto bWords = collectWords(bPages);

    runner.expectTrue(uWords == expectedWords(kWords), "roundtrip: all words present and in order");
    runner.expectTrue(uWords == bWords, "batch_eq: identical words across batches");
  }

  // Diagnostic: a SUB-CAP multi-line paragraph (cap never fires) across batches.
  {
    constexpr int kWords = 300;  // < kMaxWordsPerBlock
    TestSetup setup(/*hyphenation=*/false);
    std::string content = makeGiantParagraph(kWords);
    const std::string p1 = "/sub_unbatched.md";
    SdMan.registerFile(p1, content);
    MarkdownParser u(p1, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> up;
    auto onu = [&](std::unique_ptr<Page> p) { up.push_back(std::move(p)); };
    u.parsePages(onu, 0);
    while (u.hasMoreContent()) u.parsePages(onu, 0);
    const std::string p2 = "/sub_batched.md";
    SdMan.registerFile(p2, content);
    MarkdownParser b(p2, setup.gfx, setup.config);
    std::vector<std::unique_ptr<Page>> bp;
    auto onb = [&](std::unique_ptr<Page> p) { bp.push_back(std::move(p)); };
    b.parsePages(onb, 1);
    while (b.hasMoreContent()) b.parsePages(onb, 1);
    runner.expectTrue(collectWords(up) == collectWords(bp), "sub_cap_batch_eq: multi-line paragraph resumes cleanly");
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
