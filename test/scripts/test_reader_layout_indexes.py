#!/usr/bin/env python3
"""Check that reader indexes reject page numbers from an older layout."""

import subprocess
import tempfile
from pathlib import Path

from test_reader_resume import block

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <SDCardManager.h>
#include <Serialization.h>
#include <RenderConfig.h>
#include <content/GlobalPageMetrics.h>
#include <platform_stubs.h>
namespace page_metrics = papyrix::page_metrics;
MockSerial Serial;
#ifndef TAG
#define TAG "TEST"
#endif

@CONSTANTS@
@METRICS_READER@
class ContentParser {
 public:
  void reset() { ++resets; }
  const std::vector<std::pair<std::string, uint32_t>>& getAnchorMap() const { return anchors; }
  std::vector<std::pair<std::string, uint32_t>> anchors;
  int resets = 0;
};
// Minimal model of a complete on-disk section cache
class PageCache {
 public:
  static constexpr size_t DEFAULT_CACHE_CHUNK = 4;
  explicit PageCache(std::string path) : path_(std::move(path)) {}
  bool load(const RenderConfig&) {
    ++loads;
    return loadResult;
  }
  bool isPartial() const { return partial; }
  bool extend(ContentParser&, size_t) {
    ++extends;
    return true;
  }
  bool create(ContentParser&, const RenderConfig&, size_t) {
    ++creates;
    return true;
  }
  std::string path_;
  bool loadResult = true;
  bool partial = false;
  int loads = 0, creates = 0, extends = 0;
};
class ReaderState {
 public:
  static int loadAnchorPage(const std::string&, const std::string&);
  static std::vector<std::pair<std::string, uint32_t>> loadAnchorMap(const std::string&);
  static void saveAnchorMap(const ContentParser&, const std::string&);
  static bool anchorMapCurrent(const std::string&);
  void createOrExtendCacheImpl(ContentParser&, const std::string&, const RenderConfig&);
  std::unique_ptr<PageCache> pageCache_;
};
@ANCHOR_READERS@
void check(bool value, const char* message) {
  if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void metricsFixture(uint8_t version, const RenderConfig& config) {
  FsFile file;
  check(SdMan.openFileForWrite("TEST", "/sections/metrics.bin", file), "create metrics fixture");
  serialization::writePod(file, version);
  serialization::writePod(file, config.fontId);
  serialization::writePod(file, config.lineCompression);
  serialization::writePod(file, config.indentLevel);
  serialization::writePod(file, config.spacingLevel);
  serialization::writePod(file, config.paragraphAlignment);
  serialization::writePod(file, config.hyphenation);
  serialization::writePod(file, config.showImages);
  serialization::writePod(file, config.viewportWidth);
  serialization::writePod(file, config.viewportHeight);
  serialization::writePod(file, config.sourceFingerprint);
  serialization::writePod(file, config.fontFingerprint);
  serialization::writePod(file, uint16_t{1});
  serialization::writePod(file, uint32_t{19});
  serialization::writePod(file, uint8_t{1});
  serialization::writePod(file, uint32_t{2300});
  file.close();
}
void anchorFixture(uint8_t version) {
  FsFile file;
  check(SdMan.openFileForWrite("TEST", "/sections/0.bin.anchors", file), "create anchor fixture");
  serialization::writePod(file, uint32_t{0x48434E41});
  serialization::writePod(file, version);
  serialization::writePod(file, uint16_t{1});
  check(serialization::writeStringChecked(file, "heading"), "write anchor identifier");
  serialization::writePod(file, uint32_t{7});
  file.close();
}
int main() {
  RenderConfig config;
  std::vector<page_metrics::Section> metrics;
  metricsFixture(9, config);
  check(!readMetricsIndexFile("/sections", config, 1, metrics), "old metrics must not supply exact page counts");
  metricsFixture(kMetricsIndexVersion, config);
  check(readMetricsIndexFile("/sections", config, 1, metrics), "current metrics must load");
  check(metrics.size() == 1 && metrics[0].pages == 19 && metrics[0].exact && metrics[0].byteSize == 2300,
        "current metrics preserve page counts and byte sizes");
  testSetLargestFreeBlock(sizeof(page_metrics::Section) - 1);
  check(!readMetricsIndexFile("/sections", config, 1, metrics), "low memory defers metrics decoding");
  testResetLargestFreeBlock();
  check(metrics.size() == 1 && metrics[0].pages == 19 && metrics[0].exact && metrics[0].byteSize == 2300,
        "deferred decoding preserves the previous counts");
  check(SdMan.exists("/sections/metrics.bin"), "deferred decoding preserves the stored index");
  anchorFixture(6);
  check(ReaderState::loadAnchorPage("/sections/0.bin", "heading") == -1,
        "old anchor must not choose a page before the section cache is rebuilt");
  check(ReaderState::loadAnchorMap("/sections/0.bin").empty(), "old anchors must not supply chapter ranges");
  anchorFixture(kAnchorMapVersion);
  check(ReaderState::loadAnchorPage("/sections/0.bin", "heading") == 7, "current anchor selects its page");
  const auto anchors = ReaderState::loadAnchorMap("/sections/0.bin");
  check(anchors.size() == 1 && anchors[0].first == "heading" && anchors[0].second == 7,
        "current anchor map preserves chapter ranges");

  // Regeneration gates: old, corrupt, and truncated anchor files count as
  // missing, or a complete cache with stale anchors never regenerates them.
  ContentParser parser;
  parser.anchors = {{"heading", 7}};

  SdMan.remove("/sections/0.bin.anchors");
  check(!ReaderState::anchorMapCurrent("/sections/0.bin"), "missing anchor file is not current");
  {
    ReaderState state;
    state.createOrExtendCacheImpl(parser, "/sections/0.bin", config);
    check(state.pageCache_ && state.pageCache_->creates == 1, "missing anchors force cache regeneration");
    check(ReaderState::loadAnchorPage("/sections/0.bin", "heading") == 7, "regeneration writes current anchors");
  }

  anchorFixture(6);
  check(ReaderState::loadAnchorPage("/sections/0.bin", "heading") == -1, "old anchors still do not resolve");
  check(SdMan.exists("/sections/0.bin.anchors"), "old anchor file exists beside the complete cache");
  {
    ReaderState state;
    state.pageCache_ = std::make_unique<PageCache>("/sections/0.bin");
    state.createOrExtendCacheImpl(parser, "/sections/0.bin", config);
    check(state.pageCache_ && state.pageCache_->creates == 1,
          "old anchors beside a complete cache force regeneration");
    check(ReaderState::loadAnchorPage("/sections/0.bin", "heading") == 7,
          "regenerated anchors resolve the heading");
  }

  {
    FsFile file;
    check(SdMan.openFileForWrite("TEST", "/sections/0.bin.anchors", file), "truncate anchor fixture");
    serialization::writePod(file, uint32_t{0x48434E41});
    file.close();
    check(!ReaderState::anchorMapCurrent("/sections/0.bin"), "truncated anchor header is not current");
    check(SdMan.openFileForWrite("TEST", "/sections/0.bin.anchors", file), "rewrite anchor fixture");
    serialization::writePod(file, uint32_t{0x44434241});
    serialization::writePod(file, kAnchorMapVersion);
    serialization::writePod(file, uint16_t{1});
    file.close();
    check(!ReaderState::anchorMapCurrent("/sections/0.bin"), "bad magic is not current");
  }

  {
    ContentParser empty;
    ReaderState::saveAnchorMap(empty, "/sections/0.bin");
    check(ReaderState::anchorMapCurrent("/sections/0.bin"), "current header counts as current");
    ReaderState state;
    state.pageCache_ = std::make_unique<PageCache>("/sections/0.bin");
    state.createOrExtendCacheImpl(empty, "/sections/0.bin", config);
    check(state.pageCache_ && state.pageCache_->creates == 0 && state.pageCache_->extends == 0,
          "valid empty anchor map does not rebuild a complete cache");
  }
  std::puts("PASS: old metrics and anchor maps are rejected; current layout indexes load; stale anchors regenerate");
}
'''


def main():
    reader = (ROOT / "src/states/ReaderState.cpp").read_text()
    names = ("kMetricsIndexVersion", "kMetricsIndexFilename", "kAnchorMapMagic", "kAnchorMapVersion")
    constants = "\n".join(line for line in reader.splitlines()
                          if line.startswith("constexpr ") and any(name in line for name in names))
    harness = HARNESS.replace("@CONSTANTS@", constants)
    harness = harness.replace("@METRICS_READER@", block(reader, "bool readMetricsIndexFile("))
    harness = harness.replace("@ANCHOR_READERS@", block(reader, "int ReaderState::loadAnchorPage(") + "\n" +
                              block(reader, "std::vector<std::pair<std::string, uint32_t>> ReaderState::loadAnchorMap(") +
                              "\n" + block(reader, "bool ReaderState::anchorMapCurrent(") + "\n" +
                              block(reader, "void ReaderState::saveAnchorMap(") + "\n" +
                              block(reader, "void ReaderState::createOrExtendCacheImpl("))
    with tempfile.TemporaryDirectory(prefix="papyrix-layout-indexes-") as directory:
        source = Path(directory) / "indexes.cpp"
        binary = Path(directory) / "indexes"
        source.write_text(harness)
        includes = ["test/mocks", "lib/Serialization/src", "lib/RenderTypes/src", "lib/EpdFont/src",
                    "lib/BoardSupport/include", "src"]
        subprocess.run(["c++", "-std=c++17"] +
                       ["-I" + str(ROOT / path) for path in includes] + [str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
