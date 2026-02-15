// Tests for PageCache::loadRaw() binary format contract.
// loadRaw() reads cache header without config validation (for dump/debug tools).
//
// Rather than compiling the full PageCache class (heavy dependencies), this test
// validates the binary header format contract by writing/reading the same format.
// If both sides agree on the format, the real loadRaw() works correctly.

#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "HardwareSerial.h"
#include "SDCardManager.h"
#include "SdFat.h"
#include "Serialization.h"

namespace {

constexpr uint8_t CACHE_FILE_VERSION = 17;

// Header layout (must match PageCache.cpp):
// - version (1 byte)
// - fontId (4 bytes)
// - lineCompression (4 bytes)
// - indentLevel (1 byte)
// - spacingLevel (1 byte)
// - paragraphAlignment (1 byte)
// - hyphenation (1 byte)
// - showImages (1 byte)
// - viewportWidth (2 bytes)
// - viewportHeight (2 bytes)
// - pageCount (2 bytes)
// - isPartial (1 byte)
// - lutOffset (4 bytes)
constexpr uint32_t HEADER_SIZE = 1 + 4 + 4 + 1 + 1 + 1 + 1 + 1 + 2 + 2 + 2 + 1 + 4;

// Write a complete cache header to an FsFile buffer
void writeCacheHeader(FsFile& file, uint16_t pageCount, bool isPartial, uint8_t version = CACHE_FILE_VERSION) {
  serialization::writePod(file, version);
  uint32_t fontId = 1818981670;
  serialization::writePod(file, fontId);
  float lineCompression = 1.0f;
  serialization::writePod(file, lineCompression);
  uint8_t indentLevel = 1;
  serialization::writePod(file, indentLevel);
  uint8_t spacingLevel = 1;
  serialization::writePod(file, spacingLevel);
  uint8_t paragraphAlignment = 0;
  serialization::writePod(file, paragraphAlignment);
  uint8_t hyphenation = 1;
  serialization::writePod(file, hyphenation);
  uint8_t showImages = 1;
  serialization::writePod(file, showImages);
  uint16_t viewportWidth = 464;
  serialization::writePod(file, viewportWidth);
  uint16_t viewportHeight = 769;
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, pageCount);
  uint8_t partial = isPartial ? 1 : 0;
  serialization::writePod(file, partial);
  uint32_t lutOffset = HEADER_SIZE;
  serialization::writePod(file, lutOffset);
}

// Mirrors the loadRaw() logic from PageCache.cpp
struct LoadRawResult {
  bool success;
  uint16_t pageCount;
  bool isPartial;
};

LoadRawResult loadRaw(const std::string& path) {
  LoadRawResult result = {false, 0, false};

  FsFile file;
  if (!SdMan.openFileForRead("CACHE", path, file)) {
    return result;
  }

  uint8_t version;
  serialization::readPod(file, version);
  if (version != CACHE_FILE_VERSION) {
    file.close();
    return result;
  }

  // Skip config fields, read pageCount and isPartial
  file.seek(HEADER_SIZE - 4 - 1 - 2);
  serialization::readPod(file, result.pageCount);
  uint8_t partial;
  serialization::readPod(file, partial);
  result.isPartial = (partial != 0);

  file.close();
  result.success = true;
  return result;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("PageCacheLoadRaw");

  // Test 1: Valid complete cache (isPartial=false)
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 42, false);
    SdMan.registerFile("/cache/complete.bin", writer.getBuffer());

    auto result = loadRaw("/cache/complete.bin");
    runner.expectTrue(result.success, "complete_cache_success");
    runner.expectEq(static_cast<uint16_t>(42), result.pageCount, "complete_cache_page_count");
    runner.expectFalse(result.isPartial, "complete_cache_not_partial");
  }

  // Test 2: Valid partial cache (isPartial=true)
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 10, true);
    SdMan.registerFile("/cache/partial.bin", writer.getBuffer());

    auto result = loadRaw("/cache/partial.bin");
    runner.expectTrue(result.success, "partial_cache_success");
    runner.expectEq(static_cast<uint16_t>(10), result.pageCount, "partial_cache_page_count");
    runner.expectTrue(result.isPartial, "partial_cache_is_partial");
  }

  // Test 3: Version mismatch
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 5, false, 99);  // Wrong version
    SdMan.registerFile("/cache/bad_version.bin", writer.getBuffer());

    auto result = loadRaw("/cache/bad_version.bin");
    runner.expectFalse(result.success, "version_mismatch_fails");
  }

  // Test 4: Non-existent file
  {
    auto result = loadRaw("/cache/nonexistent.bin");
    runner.expectFalse(result.success, "nonexistent_file_fails");
  }

  // Test 5: Zero page count
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 0, false);
    SdMan.registerFile("/cache/zero_pages.bin", writer.getBuffer());

    auto result = loadRaw("/cache/zero_pages.bin");
    runner.expectTrue(result.success, "zero_pages_success");
    runner.expectEq(static_cast<uint16_t>(0), result.pageCount, "zero_pages_count");
    runner.expectFalse(result.isPartial, "zero_pages_not_partial");
  }

  // Test 6: Large page count
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 1000, true);
    SdMan.registerFile("/cache/large.bin", writer.getBuffer());

    auto result = loadRaw("/cache/large.bin");
    runner.expectTrue(result.success, "large_page_count_success");
    runner.expectEq(static_cast<uint16_t>(1000), result.pageCount, "large_page_count");
    runner.expectTrue(result.isPartial, "large_page_count_partial");
  }

  // Test 7: Max uint16_t page count
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 65535, false);
    SdMan.registerFile("/cache/max_pages.bin", writer.getBuffer());

    auto result = loadRaw("/cache/max_pages.bin");
    runner.expectTrue(result.success, "max_pages_success");
    runner.expectEq(static_cast<uint16_t>(65535), result.pageCount, "max_pages_count");
  }

  // Test 8: Header size is exactly 25 bytes
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 1, false);
    runner.expectEq(static_cast<uint32_t>(25), static_cast<uint32_t>(writer.getBuffer().size()),
                    "header_size_25_bytes");
  }

  // Test 9: Seek position is correct (HEADER_SIZE - 4 - 1 - 2 = 18)
  // pageCount starts at byte 18, isPartial at byte 20
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 0x1234, true);
    std::string buf = writer.getBuffer();

    // Verify pageCount at offset 18 (little-endian)
    runner.expectEq(static_cast<uint8_t>(0x34), static_cast<uint8_t>(buf[18]), "pagecount_low_byte");
    runner.expectEq(static_cast<uint8_t>(0x12), static_cast<uint8_t>(buf[19]), "pagecount_high_byte");
    // Verify isPartial at offset 20
    runner.expectEq(static_cast<uint8_t>(1), static_cast<uint8_t>(buf[20]), "ispartial_byte");
  }

  // Test 10: Old version (version 16) is rejected
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 5, false, 16);
    SdMan.registerFile("/cache/old_version.bin", writer.getBuffer());

    auto result = loadRaw("/cache/old_version.bin");
    runner.expectFalse(result.success, "old_version_rejected");
  }

  // Test 11: Version 0 is rejected
  {
    FsFile writer;
    writer.setBuffer("");
    writeCacheHeader(writer, 5, false, 0);
    SdMan.registerFile("/cache/version_0.bin", writer.getBuffer());

    auto result = loadRaw("/cache/version_0.bin");
    runner.expectFalse(result.success, "version_0_rejected");
  }

  // Test 12: Different config values don't affect loadRaw (it skips config)
  {
    // Write header with specific config, then verify loadRaw ignores it
    FsFile writer;
    writer.setBuffer("");
    // Write version
    serialization::writePod(writer, CACHE_FILE_VERSION);
    // Write different config values than default
    uint32_t fontId = 12345;
    serialization::writePod(writer, fontId);
    float lineCompression = 0.8f;
    serialization::writePod(writer, lineCompression);
    uint8_t indentLevel = 3;
    serialization::writePod(writer, indentLevel);
    uint8_t spacingLevel = 2;
    serialization::writePod(writer, spacingLevel);
    uint8_t paragraphAlignment = 2;
    serialization::writePod(writer, paragraphAlignment);
    uint8_t hyphenation = 0;
    serialization::writePod(writer, hyphenation);
    uint8_t showImages = 0;
    serialization::writePod(writer, showImages);
    uint16_t viewportWidth = 320;
    serialization::writePod(writer, viewportWidth);
    uint16_t viewportHeight = 480;
    serialization::writePod(writer, viewportHeight);
    // Page count and partial
    uint16_t pageCount = 77;
    serialization::writePod(writer, pageCount);
    uint8_t partial = 0;
    serialization::writePod(writer, partial);
    uint32_t lutOffset = HEADER_SIZE;
    serialization::writePod(writer, lutOffset);

    SdMan.registerFile("/cache/diff_config.bin", writer.getBuffer());

    auto result = loadRaw("/cache/diff_config.bin");
    runner.expectTrue(result.success, "diff_config_success");
    runner.expectEq(static_cast<uint16_t>(77), result.pageCount, "diff_config_page_count");
    runner.expectFalse(result.isPartial, "diff_config_not_partial");
  }

  SdMan.clearFiles();
  return runner.allPassed() ? 0 : 1;
}
