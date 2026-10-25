#!/usr/bin/env python3
"""Check cache ownership when background parsing stops before it starts."""

import subprocess
import tempfile
from pathlib import Path

from test_reader_resume import block

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <ContentParser.h>
#include <Page.h>
#include <PageCache.h>
#include <PageCachePolicy.h>
#include <RenderConfig.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <platform_stubs.h>

#define TAG "TEST"
@CONSTANTS@

void ImageBlock::render(GfxRenderer&, int, int, int) const {}
bool ImageBlock::serialize(FsFile&) const { return false; }
std::unique_ptr<ImageBlock> ImageBlock::deserialize(FsFile&) { return nullptr; }

class Parser : public ContentParser {
 public:
  bool parsePages(const std::function<void(std::unique_ptr<Page>)>& emit, uint32_t maxPages,
                  const AbortCallback& abort) override {
    uint32_t made = 0;
    while (emitted < 10 && (maxPages == 0 || made < maxPages)) {
      if (abort && abort()) return false;
      emit(std::make_unique<Page>());
      ++emitted;
      ++made;
    }
    return true;
  }
  bool hasMoreContent() const override { return emitted < 10; }
  bool canResume() const override { return emitted > 0; }
  void reset() override { emitted = 0; ++resets; }
  uint32_t emitted = 0;
  unsigned resets = 0;
};

class ReaderState {
 public:
  struct Task {
    mutable unsigned checks = 0;
    bool shouldStop() const { return ++checks >= 2; }
  } cacheTask_;
  std::unique_ptr<PageCache> pageCache_;
  static bool anchorMapCurrent(const std::string&);
  static void saveAnchorMap(const ContentParser&, const std::string&);
  void backgroundCacheImpl(ContentParser&, const std::string&, const RenderConfig&, uint32_t);
};
@FUNCTIONS@

int main() {
  const RenderConfig config{};
  const std::string path = "/cache/chapter.bin";
  for (bool partial : {false, true}) {
    SdMan.reset();
    Parser parser;
    PageCache seed(path);
    assert(seed.create(parser, config, partial ? 5 : 0));
    ReaderState::saveAnchorMap(parser, path);
    const uint32_t before = seed.pageCount();
    const unsigned resets = parser.resets;
    ReaderState state;
    state.backgroundCacheImpl(parser, path, config, 0);
    assert(state.pageCache_ && state.pageCache_->pageCount() == before);
    assert(state.pageCache_->loadPage(before - 1));
    if (partial) {
      assert(state.pageCache_->extend(parser, 2));
      assert(state.pageCache_->pageCount() == 7 && parser.emitted == 7);
      assert(parser.resets == resets);
      assert(state.pageCache_->loadPage(5));
    }
  }
  SdMan.reset();
  Parser parser;
  ReaderState missing;
  missing.backgroundCacheImpl(parser, path, config, 0);
  assert(!missing.pageCache_);
  PageCache seed(path);
  assert(seed.create(parser, config, 5));
  ReaderState staleAnchors;
  staleAnchors.backgroundCacheImpl(parser, path, config, 0);
  assert(!staleAnchors.pageCache_);
  std::puts("PASS: cancellation retains readable caches and hot resume; invalid caches stay unpublished");
}
'''


def main():
    reader = (ROOT / "src/states/ReaderState.cpp").read_text()
    names = ("kAnchorMapMagic", "kAnchorMapVersion", "kHotMinFreeHeap", "kHotMinLargestBlock",
             "kColdMinFreeHeap", "kColdMinLargestBlock")
    constants = "\n".join(line for line in reader.splitlines()
                          if line.startswith("constexpr ") and any(name in line for name in names))
    functions = "\n".join(block(reader, signature) for signature in (
        "bool ReaderState::anchorMapCurrent(", "void ReaderState::saveAnchorMap(",
        "void ReaderState::backgroundCacheImpl("))
    harness = HARNESS.replace("@CONSTANTS@", constants).replace("@FUNCTIONS@", functions)
    includes = ["test/mocks", "test/common", "src", "lib/PageCache/src", "lib/RenderTypes/src",
                "lib/Serialization/src", "lib/GfxRenderer/src", "lib/EpdFont/src", "lib/Utf8/src",
                "lib/BoardSupport/include", "lib/EInkDisplay/include"]
    sources = ["lib/PageCache/src/PageCache.cpp", "lib/RenderTypes/src/Page.cpp",
               "lib/RenderTypes/src/blocks/TextBlock.cpp", "lib/Utf8/src/Utf8.cpp",
               "test/mocks/platform_stubs.cpp"]
    with tempfile.TemporaryDirectory(prefix="papyrix-background-cache-") as directory:
        source = Path(directory) / "cache.cpp"
        binary = Path(directory) / "cache"
        source.write_text(harness)
        subprocess.run(["c++", "-std=c++17", "-DTEST_BUILD"] +
                       ["-I" + str(ROOT / path) for path in includes] + [str(source)] +
                       [str(ROOT / path) for path in sources] + ["-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
