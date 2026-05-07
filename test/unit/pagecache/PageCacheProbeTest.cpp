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

constexpr uint8_t CACHE_FILE_VERSION = 19;
constexpr uint32_t kHeaderSize = 33;

RenderConfig defaultConfig() {
  return RenderConfig(1818981670, 1.0f, 1, 1, 0, true, true, 464, 769);
}

void writeCacheHeader(FsFile& file, const RenderConfig& config, uint16_t pageCount, bool isPartial,
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
}

// Mirror of PageCache::probe() — reads header and validates config match
struct ProbeResult {
  bool valid = false;
  bool partial = false;
  uint16_t pageCount = 0;
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
  serialization::readPod(file, version);
  if (version != CACHE_FILE_VERSION) {
    file.close();
    return result;
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
    return result;
  }

  serialization::readPod(file, result.pageCount);
  uint8_t partial;
  serialization::readPod(file, partial);
  result.partial = (partial != 0);

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
    runner.expectEq(static_cast<uint16_t>(42), result.pageCount, "complete_cache_page_count");
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
    runner.expectEq(static_cast<uint16_t>(10), result.pageCount, "partial_cache_page_count");
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
    runner.expectEq(static_cast<uint16_t>(100), result.pageCount, "exact_config_match_pages");
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

  // Zero page count
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 0, false);
    SdMan.registerFile("/cache/zero_pages.bin", writer.getBuffer());

    auto result = probe("/cache/zero_pages.bin", cfg);
    runner.expectTrue(result.valid, "zero_pages_valid");
    runner.expectEq(static_cast<uint16_t>(0), result.pageCount, "zero_pages_count");
  }

  // Max uint16_t page count
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 65535, true);
    SdMan.registerFile("/cache/max_pages.bin", writer.getBuffer());

    auto result = probe("/cache/max_pages.bin", cfg);
    runner.expectTrue(result.valid, "max_pages_valid");
    runner.expectEq(static_cast<uint16_t>(65535), result.pageCount, "max_pages_count");
    runner.expectTrue(result.partial, "max_pages_partial");
  }

  // Header size is exactly kHeaderSize (33 bytes)
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, cfg, 1, false);
    runner.expectEq(static_cast<uint32_t>(kHeaderSize), static_cast<uint32_t>(writer.getBuffer().size()),
                    "header_size_33_bytes");
  }

  SdMan.clearFiles();
  return runner.allPassed() ? 0 : 1;
}
