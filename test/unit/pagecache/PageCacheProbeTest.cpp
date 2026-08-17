// Tests for PageCache::probe() — reads cache header and validates config
// without loading the full cache. Used by global page metrics to count pages
// across EPUB/FB2 spine sections.
//
// Mirrors the probe() logic by writing the same binary format and exercising
// the config-matching and edge-case paths.

#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "HardwareSerial.h"
#include "SDCardManager.h"
#include "SdFat.h"
#include "Serialization.h"

#include <RenderConfig.h>

namespace {

constexpr uint8_t CACHE_FILE_VERSION = 22;
constexpr uint32_t kHeaderSize = 43;

RenderConfig defaultConfig() {
  return RenderConfig(1818981670, 1.0f, 1, 1, 0, true, true, 464, 769, 0x12345678u, 0x89ABCDEFu);
}

void writeCacheHeader(FsFile& file, const RenderConfig& config, uint32_t pageCount, bool isPartial,
                      uint8_t version = CACHE_FILE_VERSION) {
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
  serialization::writePod(file, pageCount);
  uint8_t partial = isPartial ? 1 : 0;
  serialization::writePod(file, partial);
  uint32_t lutOffset = kHeaderSize;
  serialization::writePod(file, lutOffset);
  uint32_t bytesConsumed = 0;
  serialization::writePod(file, bytesConsumed);
  uint32_t totalBytes = 0;
  serialization::writePod(file, totalBytes);
  serialization::writePod(file, config.sourceFingerprint);
  serialization::writePod(file, config.fontFingerprint);
  const uint32_t pagePosition = kHeaderSize;
  for (uint32_t i = 0; i < pageCount; i++) serialization::writePod(file, pagePosition);
}

// Mirror of PageCache::probe() — reads header and validates config match
struct ProbeResult {
  bool valid = false;
  bool partial = false;
  uint32_t pageCount = 0;
};

ProbeResult probe(const std::string& cachePath, const RenderConfig& config) {
  ProbeResult result;

  FsFile file;
  if (!SdMan.openFileForRead("PROBE", cachePath, file)) {
    return result;
  }

  const size_t fileSize = file.size();
  if (fileSize < kHeaderSize) {
    file.close();
    return result;
  }

  uint8_t version;
  RenderConfig fileConfig;
  uint8_t partial;
  uint32_t lutOffset;
  uint32_t bytesConsumed;
  uint32_t totalBytes;
  const bool headerValid = serialization::readPodChecked(file, version) &&
                           serialization::readPodChecked(file, fileConfig.fontId) &&
                           serialization::readPodChecked(file, fileConfig.lineCompression) &&
                           serialization::readPodChecked(file, fileConfig.indentLevel) &&
                           serialization::readPodChecked(file, fileConfig.spacingLevel) &&
                           serialization::readPodChecked(file, fileConfig.paragraphAlignment) &&
                           serialization::readPodChecked(file, fileConfig.hyphenation) &&
                           serialization::readPodChecked(file, fileConfig.showImages) &&
                           serialization::readPodChecked(file, fileConfig.viewportWidth) &&
                           serialization::readPodChecked(file, fileConfig.viewportHeight) &&
                           serialization::readPodChecked(file, result.pageCount) &&
                           serialization::readPodChecked(file, partial) &&
                           serialization::readPodChecked(file, lutOffset) &&
                           serialization::readPodChecked(file, bytesConsumed) &&
                           serialization::readPodChecked(file, totalBytes) &&
                           serialization::readPodChecked(file, fileConfig.sourceFingerprint) &&
                           serialization::readPodChecked(file, fileConfig.fontFingerprint);
  const size_t lutSize = static_cast<size_t>(result.pageCount) * sizeof(uint32_t);
  if (!headerValid || version != CACHE_FILE_VERSION || config != fileConfig || partial > 1 ||
      lutOffset < kHeaderSize || lutOffset > fileSize || lutSize > fileSize - lutOffset) {
    file.close();
    return result;
  }

  result.partial = partial != 0;
  file.close();
  result.valid = true;
  return result;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("PageCache::probe");

  const auto cfg = defaultConfig();

  // Valid complete cache with matching config
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 42, false);
    SdMan.registerFile("/cache/complete.bin", writer.getBuffer());

    auto result = probe("/cache/complete.bin", cfg);
    runner.expectTrue(result.valid, "complete_cache_valid");
    runner.expectEq(static_cast<uint32_t>(42), result.pageCount, "complete_cache_page_count");
    runner.expectFalse(result.partial, "complete_cache_not_partial");
  }

  // Valid partial cache
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 10, true);
    SdMan.registerFile("/cache/partial.bin", writer.getBuffer());

    auto result = probe("/cache/partial.bin", cfg);
    runner.expectTrue(result.valid, "partial_cache_valid");
    runner.expectEq(static_cast<uint32_t>(10), result.pageCount, "partial_cache_page_count");
    runner.expectTrue(result.partial, "partial_cache_is_partial");
  }

  // Config mismatch: different fontId
  {
    RenderConfig differentCfg = cfg;
    differentCfg.fontId = 99999;

    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 20, false);
    SdMan.registerFile("/cache/font_mismatch.bin", writer.getBuffer());

    auto result = probe("/cache/font_mismatch.bin", differentCfg);
    runner.expectFalse(result.valid, "font_mismatch_invalid");
  }

  // Config mismatch: different viewport
  {
    RenderConfig differentCfg = cfg;
    differentCfg.viewportWidth = 320;
    differentCfg.viewportHeight = 480;

    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 20, false);
    SdMan.registerFile("/cache/viewport_mismatch.bin", writer.getBuffer());

    auto result = probe("/cache/viewport_mismatch.bin", differentCfg);
    runner.expectFalse(result.valid, "viewport_mismatch_invalid");
  }

  // Config mismatch: different hyphenation
  {
    RenderConfig differentCfg = cfg;
    differentCfg.hyphenation = !cfg.hyphenation;

    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 20, false);
    SdMan.registerFile("/cache/hyphen_mismatch.bin", writer.getBuffer());

    auto result = probe("/cache/hyphen_mismatch.bin", differentCfg);
    runner.expectFalse(result.valid, "hyphenation_mismatch_invalid");
  }

  // Matching config returns valid
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 100, false);
    SdMan.registerFile("/cache/exact_match.bin", writer.getBuffer());

    auto result = probe("/cache/exact_match.bin", cfg);
    runner.expectTrue(result.valid, "exact_config_match_valid");
    runner.expectEq(static_cast<uint32_t>(100), result.pageCount, "exact_config_match_pages");
  }

  // Version mismatch
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 5, false, 17);
    SdMan.registerFile("/cache/old_version.bin", writer.getBuffer());

    auto result = probe("/cache/old_version.bin", cfg);
    runner.expectFalse(result.valid, "old_version_invalid");
  }

  // Non-existent file
  {
    auto result = probe("/cache/nonexistent.bin", cfg);
    runner.expectFalse(result.valid, "nonexistent_file_invalid");
  }

  // File too small (truncated header)
  {
    SdMan.registerFile("/cache/truncated.bin", std::string(10, '\0'));

    auto result = probe("/cache/truncated.bin", cfg);
    runner.expectFalse(result.valid, "truncated_file_invalid");
  }

  // LUT declared by the header must fit in the file
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 2, false);
    std::string truncated = writer.getBuffer().substr(0, writer.getBuffer().size() - sizeof(uint32_t));
    SdMan.registerFile("/cache/truncated_lut.bin", truncated);

    auto result = probe("/cache/truncated_lut.bin", cfg);
    runner.expectFalse(result.valid, "truncated_lut_invalid");
  }

  // Zero page count
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 0, false);
    SdMan.registerFile("/cache/zero_pages.bin", writer.getBuffer());

    auto result = probe("/cache/zero_pages.bin", cfg);
    runner.expectTrue(result.valid, "zero_pages_valid");
    runner.expectEq(static_cast<uint32_t>(0), result.pageCount, "zero_pages_count");
  }

  // Page count exceeds the legacy uint16_t limit
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 70000, true);
    SdMan.registerFile("/cache/max_pages.bin", writer.getBuffer());

    auto result = probe("/cache/max_pages.bin", cfg);
    runner.expectTrue(result.valid, "wide_page_count_valid");
    runner.expectEq(static_cast<uint32_t>(70000), result.pageCount, "wide_page_count");
    runner.expectTrue(result.partial, "wide_page_count_partial");
  }

  // Header size includes both fingerprints
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 0, false);
    runner.expectEq(static_cast<uint32_t>(kHeaderSize), static_cast<uint32_t>(writer.getBuffer().size()),
                    "header_size_43_bytes");
  }

  SdMan.clearFiles();
  return runner.allPassed() ? 0 : 1;
}
