// Layer-2 safety net for Issue #137: ParsedText::layoutAndExtractLines must lay
// out an over-long block in bounded windows so the transient heap (most notably
// the pre-split duplicate of the word list) stays O(cap), not O(paragraph).
// This protects every format that can hand a single huge block to layout
// (EPUB/FB2/HTML), even where the parser has no word cap of its own.

#include <GfxRenderer.h>
#include <ParsedText.h>

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "test_utils.h"

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

static constexpr uint16_t kViewport = 60;  // mock metrics: 6px/char, 4px space
static constexpr int kFontId = 1;

static std::unique_ptr<ParsedText> makeBlock(int wordCount, bool hyphenation) {
  auto pt = std::make_unique<ParsedText>(TextBlock::LEFT_ALIGN, 0, hyphenation, true, false);
  for (int i = 0; i < wordCount; i++) {
    pt->addWord("w" + std::to_string(i), EpdFontFamily::REGULAR);
  }
  return pt;
}

static std::vector<std::string> expectedWords(int wordCount) {
  std::vector<std::string> v;
  v.reserve(wordCount);
  for (int i = 0; i < wordCount; i++) v.push_back("w" + std::to_string(i));
  return v;
}

int main() {
  TestUtils::TestRunner runner("ParsedText Windowed Layout (Issue #137)");
  GfxRenderer renderer;

  // --- Peak transient bounded for a block far larger than the cap ---
  // Hyphenation on so preSplitOversizedWords duplicates the word list; without
  // windowing that duplicate is O(block) and the peak blows past the bound.
  {
    const int kWords = 4000;
    auto pt = makeBlock(kWords, /*hyphenation=*/true);

    int lineCount = 0;
    auto onLine = [&](std::shared_ptr<TextBlock>) { lineCount++; };  // drop immediately

    size_t baseline = trackBegin();
    bool ok = pt->layoutAndExtractLines(renderer, kFontId, kViewport, onLine);
    size_t peakDelta = trackEnd(baseline);

    runner.expectTrue(ok, "window_peak: layout completed");
    runner.expectTrue(lineCount > 1, "window_peak: produced lines");
    runner.expectTrue(pt->isEmpty(), "window_peak: all words consumed");

    constexpr size_t kBoundBytes = 128 * 1024;
    runner.expectTrue(peakDelta < kBoundBytes, "window_peak: transient bounded (" + std::to_string(peakDelta / 1024) +
                                                   " KB, limit " + std::to_string(kBoundBytes / 1024) + " KB)");
  }

  // --- No word lost or duplicated across window boundaries ---
  {
    const int kWords = 2000;
    auto pt = makeBlock(kWords, /*hyphenation=*/false);
    std::vector<std::string> got;
    pt->layoutAndExtractLines(renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) {
      for (auto& wd : l->getWords()) got.push_back(wd.word);
    });
    runner.expectTrue(pt->isEmpty(), "roundtrip: consumed");
    runner.expectTrue(got == expectedWords(kWords), "roundtrip: all words in order");
  }

  // --- Resume across windows: abort repeatedly, every word emitted once ---
  {
    const int kWords = 1500;
    auto pt = makeBlock(kWords, /*hyphenation=*/false);
    std::vector<std::string> got;
    int guard = 0;
    while (!pt->isEmpty() && guard++ < 100000) {
      int collected = 0;
      pt->layoutAndExtractLines(
          renderer, kFontId, kViewport,
          [&](std::shared_ptr<TextBlock> l) {
            for (auto& wd : l->getWords()) got.push_back(wd.word);
            collected++;
          },
          true, [&]() -> bool { return collected >= 8; });
    }
    runner.expectTrue(pt->isEmpty(), "resume: fully drained");
    runner.expectTrue(got == expectedWords(kWords), "resume: all words once, in order");
  }

  // --- includeLastLine=false on a windowed block: only the final line defers ---
  {
    const int kWords = 1500;
    auto pt = makeBlock(kWords, /*hyphenation=*/false);
    std::vector<std::string> got;
    bool ok = pt->layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> l) {
          for (auto& wd : l->getWords()) got.push_back(wd.word);
        },
        false);
    runner.expectTrue(ok, "exclude_last: returns true");
    runner.expectFalse(pt->isEmpty(), "exclude_last: final line's words remain");

    // Draining the rest must recover exactly the remaining words, in order.
    pt->layoutAndExtractLines(renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) {
      for (auto& wd : l->getWords()) got.push_back(wd.word);
    });
    runner.expectTrue(pt->isEmpty(), "exclude_last: drained on second pass");
    runner.expectTrue(got == expectedWords(kWords), "exclude_last: no words lost across the boundary");
  }

  // --- Indentation is applied exactly once across windows ---
  {
    const int kWords = 1500;
    auto pt = std::make_unique<ParsedText>(TextBlock::LEFT_ALIGN, 2, /*hyphenation=*/false, true, false);
    for (int i = 0; i < kWords; i++) pt->addWord("w" + std::to_string(i), EpdFontFamily::REGULAR);

    std::vector<std::string> got;
    pt->layoutAndExtractLines(renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock> l) {
      for (auto& wd : l->getWords()) got.push_back(wd.word);
    });

    runner.expectTrue(pt->isEmpty(), "indent_window: consumed");
    runner.expectTrue(got.size() == static_cast<size_t>(kWords), "indent_window: all words present");

    const std::string emSpace = "\xe2\x80\x83";  // U+2003
    int indentCount = 0;
    for (auto& w : got) {
      if (w.rfind(emSpace, 0) == 0) indentCount++;
    }
    runner.expectTrue(indentCount == 1,
                      "indent_window: indent applied exactly once (" + std::to_string(indentCount) + ")");
    if (!got.empty()) {
      runner.expectEqual(emSpace + "w0", got[0], "indent_window: first word carries the indent");
    }
  }

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
