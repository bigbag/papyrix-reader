// Layer-2 safety net for Issue #137: ParsedText::layoutAndExtractLines must lay
// out an over-long block in bounded windows so the transient heap used by word
// preprocessing and layout workspaces stays O(cap), not O(paragraph).
// This protects every format that can hand a single huge block to layout
// (EPUB/FB2/HTML), even where the parser has no word cap of its own.

#include <BuildArena.h>
#include <GfxRenderer.h>
#include <ParsedText.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
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
size_t g_trackedAllocations = 0;
bool g_tracking = false;
constexpr size_t kHeader = alignof(std::max_align_t);

void* trackedAlloc(size_t n) {
  void* base = std::malloc(n + kHeader);
  if (!base) return nullptr;
  *static_cast<size_t*>(base) = n;
  g_liveBytes += n;
  if (g_tracking) {
    ++g_trackedAllocations;
    if (g_liveBytes > g_peakBytes) g_peakBytes = g_liveBytes;
  }
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
  g_trackedAllocations = 0;
  g_tracking = true;
  return g_liveBytes;
}

struct HeapSample {
  size_t peakBytes;
  size_t allocations;
};

HeapSample trackEnd(size_t baseline) {
  g_tracking = false;
  return {g_peakBytes - baseline, g_trackedAllocations};
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
  // Hyphenation stays enabled so preprocessing and layout exercise their full
  // working set in every bounded window.
  {
    const int kWords = 4000;
    auto pt = makeBlock(kWords, /*hyphenation=*/true);

    int lineCount = 0;
    auto onLine = [&](std::shared_ptr<TextBlock>) { lineCount++; };  // drop immediately

    const size_t baseline = trackBegin();
    const bool ok = pt->layoutAndExtractLines(renderer, kFontId, kViewport, onLine);
    const HeapSample sample = trackEnd(baseline);

    std::fprintf(stderr, "LAYOUT_BASELINE peak=%zu allocations=%zu\n", sample.peakBytes, sample.allocations);
    runner.expectTrue(ok, "window_peak: layout completed");
    runner.expectTrue(lineCount > 1, "window_peak: produced lines");
    runner.expectTrue(pt->isEmpty(), "window_peak: all words consumed");
    runner.expectTrue(sample.peakBytes <= 24 * 1024,
                      "window_peak: transient heap at most 24 KiB (" + std::to_string(sample.peakBytes) + ")");
    runner.expectTrue(sample.allocations <= 11312,
                      "window_peak: allocations at most 75% baseline (" + std::to_string(sample.allocations) + ")");
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

  auto flatten = [&](ParsedText& parsed, BuildArena* arena) {
    std::vector<std::string> out;
    const bool ok = parsed.layoutAndExtractLines(
        renderer, kFontId, kViewport,
        [&](std::shared_ptr<TextBlock> line) {
          for (const auto& word : line->getWords()) out.push_back(word.word);
        },
        true, nullptr, arena);
    runner.expectTrue(ok, "arena layout completed");
    return out;
  };

  {
    auto heap = makeBlock(512, true);
    auto arenaBacked = makeBlock(512, true);
    const auto expected = flatten(*heap, nullptr);
    uint8_t frame[48000] = {};
    BuildArena arena(frame, sizeof(frame));
    const auto actual = flatten(*arenaBacked, &arena);
    runner.expectTrue(actual == expected, "48 KiB arena output matches heap");
    runner.expectTrue(arena.highWater() > 0, "arena workspace used");
    runner.expectEq<uint32_t>(0, arena.fallbackCount(), "48 KiB arena needs no fallback");
    std::fprintf(stderr, "ARENA_X4 high=%zu/%zu fallbacks=%u releases=%u\n", arena.highWater(), arena.capacity(),
                 arena.fallbackCount(), arena.releaseFailures());
  }

  {
    auto heap = makeBlock(512, true);
    auto arenaBacked = makeBlock(512, true);
    const auto expected = flatten(*heap, nullptr);
    uint8_t frame[52272] = {};
    BuildArena arena(frame, sizeof(frame));
    auto inputScope = arena.scope();
    runner.expectTrue(arena.alloc(4097, alignof(uint8_t)) != nullptr, "FB2 input buffer reserved");
    const auto actual = flatten(*arenaBacked, &arena);
    runner.expectTrue(actual == expected, "X3 nested layout output matches heap");
    runner.expectEq<uint32_t>(0, arena.fallbackCount(), "X3 nested arena needs no fallback");
    std::fprintf(stderr, "ARENA_X3 high=%zu/%zu fallbacks=%u releases=%u\n", arena.highWater(), arena.capacity(),
                 arena.fallbackCount(), arena.releaseFailures());
  }

  {
    auto heap = makeBlock(512, true);
    auto arenaBacked = makeBlock(512, true);
    const auto expected = flatten(*heap, nullptr);
    uint8_t tiny[128] = {};
    BuildArena arena(tiny, sizeof(tiny));
    const auto actual = flatten(*arenaBacked, &arena);
    runner.expectTrue(actual == expected, "tiny arena falls back without output changes");
    runner.expectEq<uint32_t>(1, arena.fallbackCount(), "tiny arena fallback counted once");
  }

  std::array<long long, 20> layoutMicros{};
  for (size_t sampleIndex = 0; sampleIndex < layoutMicros.size(); ++sampleIndex) {
    auto timed = makeBlock(4000, true);
    int lines = 0;
    const auto started = std::chrono::steady_clock::now();
    timed->layoutAndExtractLines(renderer, kFontId, kViewport, [&](std::shared_ptr<TextBlock>) { ++lines; });
    const auto stopped = std::chrono::steady_clock::now();
    layoutMicros[sampleIndex] =
        std::chrono::duration_cast<std::chrono::microseconds>(stopped - started).count();
    runner.expectTrue(lines > 0, "timing: produced lines");
  }
  std::sort(layoutMicros.begin(), layoutMicros.end());
  const long long medianMicros = layoutMicros[layoutMicros.size() / 2];
  std::fprintf(stderr, "LAYOUT_TIME median_us=%lld samples=%zu\n", medianMicros, layoutMicros.size());

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
