// Tests for the metrics.bin on-disk format used by ReaderState to skip the
// per-spine probe/scan on book open. The file stores per-section page counts
// (exact or estimated) and byte sizes used for recalibrating estimates.
//
// This test inlines the read/write logic from ReaderState.cpp (following the
// project pattern of avoiding heavy linker dependencies) and verifies:
//   - save/load roundtrip preserves pages, exact flag, byteSize
//   - version mismatch is rejected (v1 reader rejects v2 file and vice versa)
//   - config mismatch is rejected
//   - entry-count mismatch is rejected
//   - truncated file is rejected

#include "test_utils.h"

#include <cstdint>
#include <string>
#include <vector>

#include "HardwareSerial.h"
#include "RenderConfig.h"
#include "SDCardManager.h"
#include "SdFat.h"
#include "Serialization.h"

namespace {

constexpr uint8_t kMetricsIndexVersion = 2;
constexpr const char* kMetricsIndexFilename = "metrics.bin";

struct MetricsEntry {
  uint16_t pages;
  bool exact;
  uint32_t byteSize;
};

// Mirrors ReaderState::saveMetricsIndex
bool saveMetricsIndex(const std::string& sectionsDir, const RenderConfig& config,
                      const std::vector<MetricsEntry>& metrics) {
  if (metrics.empty()) return false;

  const std::string path = sectionsDir + "/" + kMetricsIndexFilename;
  FsFile file;
  if (!SdMan.openFileForWrite("MIDX", path, file)) return false;

  serialization::writePod(file, kMetricsIndexVersion);
  serialization::writePod(file, config.fontId);
  serialization::writePod(file, config.lineCompression);
  serialization::writePod(file, config.indentLevel);
  serialization::writePod(file, config.spacingLevel);
  serialization::writePod(file, config.paragraphAlignment);
  serialization::writePod(file, config.hyphenation);
  serialization::writePod(file, config.showImages);
  serialization::writePod(file, config.viewportWidth);
  serialization::writePod(file, config.viewportHeight);

  const uint16_t entryCount = static_cast<uint16_t>(metrics.size());
  serialization::writePod(file, entryCount);

  for (const auto& m : metrics) {
    serialization::writePod(file, m.pages);
    const uint8_t flags = m.exact ? 1 : 0;
    serialization::writePod(file, flags);
    serialization::writePod(file, m.byteSize);
  }

  file.close();
  return true;
}

// Mirrors ReaderState::readMetricsIndexFile
bool readMetricsIndexFile(const std::string& sectionsDir, const RenderConfig& config, int spineCount,
                          std::vector<MetricsEntry>& out) {
  const std::string path = sectionsDir + "/" + kMetricsIndexFilename;
  FsFile file;
  if (!SdMan.openFileForRead("MIDX", path, file)) return false;

  uint8_t version;
  if (!serialization::readPodChecked(file, version) || version != kMetricsIndexVersion) {
    file.close();
    return false;
  }

  RenderConfig fileConfig;
  serialization::readPod(file, fileConfig.fontId);
  serialization::readPod(file, fileConfig.lineCompression);
  serialization::readPod(file, fileConfig.indentLevel);
  serialization::readPod(file, fileConfig.spacingLevel);
  serialization::readPod(file, fileConfig.paragraphAlignment);
  serialization::readPod(file, fileConfig.hyphenation);
  serialization::readPod(file, fileConfig.showImages);
  serialization::readPod(file, fileConfig.viewportWidth);
  serialization::readPod(file, fileConfig.viewportHeight);
  if (config != fileConfig) {
    file.close();
    return false;
  }

  uint16_t entryCount;
  if (!serialization::readPodChecked(file, entryCount) || entryCount != static_cast<uint16_t>(spineCount)) {
    file.close();
    return false;
  }

  out.resize(static_cast<size_t>(spineCount));
  for (int i = 0; i < spineCount; ++i) {
    uint16_t pages;
    uint8_t flags;
    uint32_t byteSize;
    if (!serialization::readPodChecked(file, pages) || !serialization::readPodChecked(file, flags) ||
        !serialization::readPodChecked(file, byteSize)) {
      file.close();
      return false;
    }
    out[static_cast<size_t>(i)] = MetricsEntry{pages, (flags & 1) != 0, byteSize};
  }

  file.close();
  return true;
}

RenderConfig makeConfig() {
  return RenderConfig(/*fontId=*/1234, /*lineCompression=*/1.0f, /*indentLevel=*/1, /*spacingLevel=*/2,
                      /*paragraphAlignment=*/0, /*hyphenation=*/true, /*showImages=*/true,
                      /*viewportWidth=*/464, /*viewportHeight=*/765);
}

void registerOnDisk(const std::string& dir) {
  const std::string path = dir + "/" + kMetricsIndexFilename;
  SdMan.registerFile(path, SdMan.getWrittenData(path));
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("MetricsIndex");

  // Test 1: Roundtrip with all-exact entries preserves pages and byteSize
  {
    SdMan.clearWrittenFiles();
    const std::string dir = "/cache/book1";
    const RenderConfig config = makeConfig();
    std::vector<MetricsEntry> in = {
        {10, true, 20480},
        {25, true, 51200},
        {5, true, 10240},
    };
    runner.expectTrue(saveMetricsIndex(dir, config, in), "all_exact_save_ok");
    registerOnDisk(dir);

    std::vector<MetricsEntry> out;
    runner.expectTrue(readMetricsIndexFile(dir, config, 3, out), "all_exact_load_ok");
    runner.expectEq<size_t>(3, out.size(), "all_exact_count");
    runner.expectEq<uint16_t>(10, out[0].pages, "all_exact_pages_0");
    runner.expectEq<uint16_t>(25, out[1].pages, "all_exact_pages_1");
    runner.expectEq<uint16_t>(5, out[2].pages, "all_exact_pages_2");
    runner.expectTrue(out[0].exact && out[1].exact && out[2].exact, "all_exact_flags");
    runner.expectEq<uint32_t>(20480, out[0].byteSize, "all_exact_bytesize_0");
    runner.expectEq<uint32_t>(51200, out[1].byteSize, "all_exact_bytesize_1");
    runner.expectEq<uint32_t>(10240, out[2].byteSize, "all_exact_bytesize_2");
  }

  // Test 2: Roundtrip with mixed exact/estimated entries preserves byteSize
  // (this is the regression case: without byteSize, recalibration of
  // estimated entries can't happen after fast-path load)
  {
    SdMan.clearWrittenFiles();
    const std::string dir = "/cache/book2";
    const RenderConfig config = makeConfig();
    std::vector<MetricsEntry> in = {
        {10, true, 20480},    // exact, with calibration data
        {7, false, 14336},    // estimated, byteSize needed for recalibration
        {12, true, 24576},    // exact
        {3, false, 6144},     // estimated
    };
    runner.expectTrue(saveMetricsIndex(dir, config, in), "mixed_save_ok");
    registerOnDisk(dir);

    std::vector<MetricsEntry> out;
    runner.expectTrue(readMetricsIndexFile(dir, config, 4, out), "mixed_load_ok");
    runner.expectTrue(out[0].exact, "mixed_exact_0");
    runner.expectFalse(out[1].exact, "mixed_estimated_1");
    runner.expectTrue(out[2].exact, "mixed_exact_2");
    runner.expectFalse(out[3].exact, "mixed_estimated_3");
    runner.expectEq<uint32_t>(14336, out[1].byteSize, "mixed_estimated_bytesize_1");
    runner.expectEq<uint32_t>(6144, out[3].byteSize, "mixed_estimated_bytesize_3");
  }

  // Test 3: Version mismatch is rejected (simulate old v1 file)
  {
    SdMan.clearWrittenFiles();
    const std::string dir = "/cache/book3";
    const std::string path = dir + "/" + kMetricsIndexFilename;
    FsFile writer;
    writer.setBuffer("");
    uint8_t oldVersion = 1;
    serialization::writePod(writer, oldVersion);
    // Pad enough bytes so a truncation check isn't what trips us
    const RenderConfig config = makeConfig();
    serialization::writePod(writer, config.fontId);
    SdMan.registerFile(path, writer.getBuffer());

    std::vector<MetricsEntry> out;
    runner.expectFalse(readMetricsIndexFile(dir, config, 1, out), "old_version_rejected");
  }

  // Test 4: Config mismatch (different fontId) is rejected
  {
    SdMan.clearWrittenFiles();
    const std::string dir = "/cache/book4";
    const RenderConfig configA = makeConfig();
    RenderConfig configB = makeConfig();
    configB.fontId = 9999;

    std::vector<MetricsEntry> in = {{10, true, 20480}};
    runner.expectTrue(saveMetricsIndex(dir, configA, in), "config_save_ok");
    registerOnDisk(dir);

    std::vector<MetricsEntry> out;
    runner.expectFalse(readMetricsIndexFile(dir, configB, 1, out), "config_mismatch_rejected");
  }

  // Test 5: Entry-count mismatch (book gained/lost spine items) is rejected
  {
    SdMan.clearWrittenFiles();
    const std::string dir = "/cache/book5";
    const RenderConfig config = makeConfig();
    std::vector<MetricsEntry> in = {{10, true, 20480}, {20, true, 40960}};
    runner.expectTrue(saveMetricsIndex(dir, config, in), "count_save_ok");
    registerOnDisk(dir);

    std::vector<MetricsEntry> out;
    runner.expectFalse(readMetricsIndexFile(dir, config, 3, out), "count_mismatch_rejected");
  }

  // Test 6: Truncated file (saved entries shorter than declared count) is rejected
  {
    SdMan.clearWrittenFiles();
    const std::string dir = "/cache/book6";
    const std::string path = dir + "/" + kMetricsIndexFilename;
    const RenderConfig config = makeConfig();

    FsFile writer;
    writer.setBuffer("");
    serialization::writePod(writer, kMetricsIndexVersion);
    serialization::writePod(writer, config.fontId);
    serialization::writePod(writer, config.lineCompression);
    serialization::writePod(writer, config.indentLevel);
    serialization::writePod(writer, config.spacingLevel);
    serialization::writePod(writer, config.paragraphAlignment);
    serialization::writePod(writer, config.hyphenation);
    serialization::writePod(writer, config.showImages);
    serialization::writePod(writer, config.viewportWidth);
    serialization::writePod(writer, config.viewportHeight);
    uint16_t entryCount = 3;
    serialization::writePod(writer, entryCount);
    // Only one entry written despite claiming three
    uint16_t pages = 10;
    uint8_t flags = 1;
    uint32_t byteSize = 1024;
    serialization::writePod(writer, pages);
    serialization::writePod(writer, flags);
    serialization::writePod(writer, byteSize);
    SdMan.registerFile(path, writer.getBuffer());

    std::vector<MetricsEntry> out;
    runner.expectFalse(readMetricsIndexFile(dir, config, 3, out), "truncated_rejected");
  }

  // Test 7: Missing file is rejected
  {
    SdMan.clearWrittenFiles();
    std::vector<MetricsEntry> out;
    runner.expectFalse(readMetricsIndexFile("/cache/nonexistent", makeConfig(), 1, out), "missing_rejected");
  }

  return runner.allPassed() ? 0 : 1;
}
