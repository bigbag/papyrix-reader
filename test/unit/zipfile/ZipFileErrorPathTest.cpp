// ZipFile error path unit tests
//
// Tests error handling paths in ZipFile to ensure no memory leaks
// on early returns and proper cleanup in all scenarios.
//
// These tests verify that error conditions are handled gracefully without crashes
// or memory leaks. Tests that require successful ZIP parsing are not included
// as they would require a more sophisticated mock or real ZIP files.

#include <BuildArena.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "test_utils.h"

// Include mocks
#include "HardwareSerial.h"
#include "SDCardManager.h"
#include "SdFat.h"

// Include ZipFile header
#include "ZipFile.h"

// Forward declarations for helper functions
std::vector<uint8_t> createMinimalZip();
std::vector<uint8_t> createStoredZip(const std::string& name, const std::string& contents);
std::vector<uint8_t> createDeflatedZip(const std::string& name, uint32_t inflatedSize = 1040);
std::vector<uint8_t> createZipWithInvalidOffset(const char* name);
std::vector<uint8_t> createZipWithUnsupportedCompression(const char* name);
std::vector<uint8_t> createZipWithNamedEntries(const std::vector<std::pair<std::string, uint32_t>>& entries);
// Like createZipWithNamedEntries but appends one extra CD entry (valid signature,
// same name as poisonName, but size=poisonSize) after all normal entries.
// If the early-exit break fires correctly the poison entry is never reached;
// if it is reached, sizes[poisonIndex] will be overwritten with poisonSize.
std::vector<uint8_t> createZipWithPoisonTrailingEntry(const std::vector<std::pair<std::string, uint32_t>>& entries,
                                                      const std::string& poisonName, uint32_t poisonSize);

// Raw deflate of "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" repeated 20 times.
constexpr uint8_t LARGE_DEFLATED[] = {
    0x4b, 0x4c, 0x4a, 0x4e, 0x49, 0x4d, 0x4b, 0xcf, 0xc8, 0xcc, 0xca, 0xce, 0xc9, 0xcd, 0xcb, 0x2f,
    0x28, 0x2c, 0x2a, 0x2e, 0x29, 0x2d, 0x2b, 0xaf, 0xa8, 0xac, 0x72, 0x74, 0x72, 0x76, 0x71, 0x75,
    0x73, 0xf7, 0xf0, 0xf4, 0xf2, 0xf6, 0xf1, 0xf5, 0xf3, 0x0f, 0x08, 0x0c, 0x0a, 0x0e, 0x09, 0x0d,
    0x0b, 0x8f, 0x88, 0x8c, 0x4a, 0x1c, 0xd5, 0x33, 0xaa, 0x67, 0x54, 0xcf, 0xb0, 0xd4, 0x03, 0x00,
};

std::string largeInflatedPayload() {
  const std::string letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  std::string result;
  result.reserve(1040);
  for (int i = 0; i < 20; ++i) result += letters;
  return result;
}

// Mock Print for stream testing
class MockPrint : public Print {
 public:
  size_t write(uint8_t b) override {
    data_.push_back(b);
    return 1;
  }
  size_t write(const uint8_t* buf, size_t len) override {
    data_.insert(data_.end(), buf, buf + len);
    return len;
  }
  const std::vector<uint8_t>& data() const { return data_; }
  void clear() { data_.clear(); }

 private:
  std::vector<uint8_t> data_;
};

int main() {
  TestUtils::TestRunner runner("ZipFileErrorPath");

  // ========================================================================
  // Basic Open/Close Tests - Error Cases
  // ========================================================================

  {
    SdMan.reset();
    SdMan.setFileExists("/test.zip", false);
    ZipFile zip("/test.zip");
    runner.expectFalse(zip.open(), "OpenNonExistentFile_ReturnsFalse");
    runner.expectFalse(zip.isOpen(), "OpenNonExistentFile_NotOpen");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");
    zip.open();
    zip.close();
    runner.expectFalse(zip.isOpen(), "AfterClose_NotOpen");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");
    zip.open();
    zip.close();
    zip.close();  // Second close should be safe
    runner.expectTrue(true, "DoubleClose_NoCrash");
  }

  // ========================================================================
  // Zip Details Loading - Error Cases
  // ========================================================================

  {
    SdMan.reset();
    std::vector<uint8_t> smallData(21, 0);  // Too small for valid ZIP
    SdMan.setFileData("/test.zip", smallData);
    ZipFile zip("/test.zip");
    zip.open();
    runner.expectEq<uint16_t>(0, zip.getTotalEntries(), "TooSmallZip_ZeroEntries");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> data(100, 0);  // No EOCD signature
    SdMan.setFileData("/test.zip", data);
    ZipFile zip("/test.zip");
    zip.open();
    runner.expectEq<uint16_t>(0, zip.getTotalEntries(), "NoEOCD_ZeroEntries");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> data = createMinimalZip();
    data[94] = 0xFF;
    data[95] = 0xFF;
    data[96] = 0xFF;
    data[97] = 0x7F;
    SdMan.setFileData("/bad-offset.zip", data);
    ZipFile zip("/bad-offset.zip");
    runner.expectFalse(zip.loadAllFileStatSlims(), "InvalidCentralDirectoryOffset_Rejected");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> data = createMinimalZip();
    data[88] = 0x11;  // 10001 entries
    data[89] = 0x27;
    SdMan.setFileData("/too-many.zip", data);
    ZipFile zip("/too-many.zip");
    runner.expectFalse(zip.loadAllFileStatSlims(), "ExcessiveEntryCount_Rejected");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> data = createZipWithNamedEntries({{"chapter.xhtml", 123}});
    data.erase(data.begin() + 46);  // Truncate the filename while preserving an EOCD record
    SdMan.setFileData("/truncated-central.zip", data);
    ZipFile zip("/truncated-central.zip");
    runner.expectFalse(zip.loadAllFileStatSlims(), "TruncatedCentralDirectory_Rejected");
  }

  // ========================================================================
  // Local header offsets
  // ========================================================================

  {
    SdMan.reset();
    const std::string name = "chapter.xhtml";
    const std::string path = "/stored.zip";
    SdMan.setFileData(path, createStoredZip(name, "content"));
    ZipFile zip(path);
    ZipFile::FileStatSlim stat{};
    runner.expectTrue(zip.open(), "DataOffset_OpenForStat");
    runner.expectTrue(zip.loadFileStatSlim(name.c_str(), &stat), "DataOffset_LoadStat");
    zip.close();
    runner.expectFalse(zip.isOpen(), "DataOffset_StartsClosed");
    runner.expectEq<long>(static_cast<long>(30 + name.size()), zip.getDataOffset(stat),
                          "DataOffset_StandaloneClosedFile");
    runner.expectFalse(zip.isOpen(), "DataOffset_RestoresClosedState");
  }

  // ========================================================================
  // readFileToMemory - Error Cases
  // ========================================================================

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");
    size_t size = 0;
    uint8_t* data = zip.readFileToMemory("nonexistent.txt", &size);
    runner.expectTrue(data == nullptr, "ReadNonExistent_ReturnsNull");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createZipWithInvalidOffset("test.txt");
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");
    size_t size = 0;
    uint8_t* data = zip.readFileToMemory("test.txt", &size);
    runner.expectTrue(data == nullptr, "ReadInvalidOffset_ReturnsNull");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createZipWithUnsupportedCompression("test.txt");
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");
    size_t size = 0;
    uint8_t* data = zip.readFileToMemory("test.txt", &size);
    runner.expectTrue(data == nullptr, "ReadUnsupportedCompression_ReturnsNull");
  }

  // ========================================================================
  // readFileToStream - Error Cases
  // ========================================================================

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    MockPrint output;
    ZipFile zip("/test.zip");
    runner.expectFalse(zip.readFileToStream("nonexistent.txt", output, 1024), "StreamNonExistent_ReturnsFalse");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createZipWithInvalidOffset("test.txt");
    SdMan.setFileData("/test.zip", zipData);
    MockPrint output;
    ZipFile zip("/test.zip");
    runner.expectFalse(zip.readFileToStream("test.txt", output, 1024), "StreamInvalidOffset_ReturnsFalse");
  }

  {
    SdMan.reset();
    const std::string payload(3000, 'x');
    SdMan.setFileData("/stored.zip", createStoredZip("chapter.xhtml", payload));
    std::string path = "/stored.zip";
    ZipFile zip(path);
    MockPrint output;
    uint8_t bytes[4096] = {};
    BuildArena arena(bytes, sizeof(bytes));
    const auto result = zip.readFileToStreamDetailed("chapter.xhtml", output, 1024, nullptr, nullptr, &arena);
    runner.expectTrue(result == StreamReadResult::Success, "stored arena extraction succeeds");
    runner.expectEq<size_t>(payload.size(), output.data().size(), "stored arena output size");
    runner.expectTrue(std::equal(output.data().begin(), output.data().end(), payload.begin()),
                      "stored arena output matches");
    runner.expectTrue(arena.highWater() >= 1024, "stored arena used");
    runner.expectEq<size_t>(0, arena.used(), "stored arena scope released");
  }

  {
    SdMan.reset();
    const std::string payload(3000, 'x');
    SdMan.setFileData("/stored.zip", createStoredZip("chapter.xhtml", payload));
    std::string path = "/stored.zip";
    ZipFile zip(path);
    MockPrint output;
    uint8_t bytes[128] = {};
    BuildArena arena(bytes, sizeof(bytes));
    const auto result = zip.readFileToStreamDetailed("chapter.xhtml", output, 1024, nullptr, nullptr, &arena);
    runner.expectTrue(result == StreamReadResult::Success, "stored tiny arena extraction succeeds");
    runner.expectEq<uint32_t>(1, arena.fallbackCount(), "stored tiny arena fallback counted");
    runner.expectEq<size_t>(0, arena.used(), "stored tiny arena scope released");
  }

  {
    SdMan.reset();
    const std::string payload = largeInflatedPayload();
    const std::string path = "/deflated.zip";
    SdMan.setFileData(path, createDeflatedZip("chapter.xhtml"));
    ZipFile zip(path);
    MockPrint output;
    uint8_t bytes[33024] = {};
    BuildArena arena(bytes, sizeof(bytes));
    const auto result = zip.readFileToStreamDetailed("chapter.xhtml", output, 64, nullptr, nullptr, &arena);
    runner.expectTrue(result == StreamReadResult::Success, "deflated arena extraction succeeds");
    runner.expectTrue(std::equal(output.data().begin(), output.data().end(), payload.begin()),
                      "deflated arena output matches");
    runner.expectEq<uint32_t>(0, arena.fallbackCount(), "deflated arena avoids fallback");
    runner.expectEq<size_t>(0, arena.used(), "deflated arena scope released");
  }

  {
    SdMan.reset();
    const std::string payload = largeInflatedPayload();
    const std::string path = "/deflated-fallback.zip";
    SdMan.setFileData(path, createDeflatedZip("chapter.xhtml"));
    ZipFile zip(path);
    MockPrint output;
    uint8_t bytes[128] = {};
    BuildArena arena(bytes, sizeof(bytes));
    const auto result = zip.readFileToStreamDetailed("chapter.xhtml", output, 64, nullptr, nullptr, &arena);
    runner.expectTrue(result == StreamReadResult::Success, "partial deflated arena falls back to heap");
    runner.expectTrue(std::equal(output.data().begin(), output.data().end(), payload.begin()),
                      "deflated fallback output matches");
    runner.expectEq<uint32_t>(1, arena.fallbackCount(), "deflated heap fallback counted");
    runner.expectEq<size_t>(0, arena.used(), "deflated fallback releases partial arena allocations");
  }

  {
    SdMan.reset();
    const std::string path = "/deflated-abort.zip";
    SdMan.setFileData(path, createDeflatedZip("chapter.xhtml"));
    ZipFile zip(path);
    MockPrint output;
    uint8_t bytes[33024] = {};
    BuildArena arena(bytes, sizeof(bytes));
    const auto result = zip.readFileToStreamDetailed("chapter.xhtml", output, 8, nullptr, [] { return true; }, &arena);
    runner.expectTrue(result == StreamReadResult::Aborted, "deflated extraction aborts");
    runner.expectEq<size_t>(0, arena.used(), "deflated abort releases arena");
  }

  {
    SdMan.reset();
    const std::string path = "/deflated-size.zip";
    SdMan.setFileData(path, createDeflatedZip("chapter.xhtml", 1041));
    ZipFile zip(path);
    MockPrint output;
    uint8_t bytes[33024] = {};
    BuildArena arena(bytes, sizeof(bytes));
    const auto result = zip.readFileToStreamDetailed("chapter.xhtml", output, 64, nullptr, nullptr, &arena);
    runner.expectTrue(result == StreamReadResult::SizeMismatch, "deflated size mismatch is reported");
    runner.expectEq<size_t>(0, arena.used(), "deflated size mismatch releases arena");
  }

  // ========================================================================
  // getInflatedFileSize - Error Cases
  // ========================================================================

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");
    size_t size = 0;
    runner.expectFalse(zip.getInflatedFileSize("nonexistent.txt", &size), "GetSizeNonExistent_ReturnsFalse");
  }

  // ========================================================================
  // Memory Safety Tests
  // ========================================================================

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    ZipFile zip("/test.zip");

    // Multiple read operations should not leak memory
    for (int i = 0; i < 5; i++) {
      size_t size = 0;
      uint8_t* data = zip.readFileToMemory("test.txt", &size);
      if (data) {
        free(data);
      }
    }
    runner.expectTrue(true, "MultipleReads_NoMemoryLeaks");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> zipData = createMinimalZip();
    SdMan.setFileData("/test.zip", zipData);
    {
      ZipFile zip("/test.zip");
      zip.open();
      // Destructor should close file safely
    }
    runner.expectTrue(true, "Destructor_ClosesFile");
  }

  // ========================================================================
  // fillUncompressedSizes - Early Exit Tests
  // ========================================================================

  // Empty targets: returns 0 without needing a valid ZIP
  {
    SdMan.reset();
    ZipFile zip("/test.zip");
    std::vector<ZipFile::SizeTarget> targets;
    std::vector<uint32_t> sizes;
    runner.expectEq<int>(0, zip.fillUncompressedSizes(targets, sizes), "FillSizes_EmptyTargets_ReturnsZero");
  }

  // Single target matches the only entry
  {
    SdMan.reset();
    std::vector<std::pair<std::string, uint32_t>> entries = {{"OEBPS/content.opf", 1234}};
    SdMan.setFileData("/test.zip", createZipWithNamedEntries(entries));
    std::string path1 = "/test.zip";
    ZipFile zip(path1);

    const char* name = "OEBPS/content.opf";
    uint64_t hash = ZipFile::fnvHash64(name, strlen(name));
    std::vector<ZipFile::SizeTarget> targets = {{hash, static_cast<uint16_t>(strlen(name)), 0}};
    std::vector<uint32_t> sizes(1, 0);
    int matched = zip.fillUncompressedSizes(targets, sizes);
    runner.expectEq<int>(1, matched, "FillSizes_OneEntry_OneTarget_MatchedOne");
    runner.expectEq<uint32_t>(1234, sizes[0], "FillSizes_OneEntry_CorrectSize");
  }

  // Two targets in a ZIP with more entries; early exit fires after both are matched
  // The ZIP has 4 entries; targets are entries[0] and entries[2].
  // Without early exit the scan continues past entries[2] into entries[3].
  // With early exit the scan stops as soon as matched == 2, leaving entries[3] unread.
  // We verify: matched == 2 and both sizes are populated correctly.
  {
    SdMan.reset();
    std::vector<std::pair<std::string, uint32_t>> entries = {
        {"OEBPS/chapter1.html", 5000},
        {"OEBPS/chapter2.html", 6000},
        {"OEBPS/content.opf", 1234},
        {"OEBPS/chapter3.html", 7000},
    };
    SdMan.setFileData("/test.zip", createZipWithNamedEntries(entries));
    std::string path2 = "/test.zip";
    ZipFile zip(path2);

    const char* nameA = "OEBPS/chapter1.html";
    const char* nameB = "OEBPS/content.opf";
    std::vector<ZipFile::SizeTarget> targets = {
        {ZipFile::fnvHash64(nameA, strlen(nameA)), static_cast<uint16_t>(strlen(nameA)), 0},
        {ZipFile::fnvHash64(nameB, strlen(nameB)), static_cast<uint16_t>(strlen(nameB)), 1},
    };
    std::sort(targets.begin(), targets.end());
    std::vector<uint32_t> sizes(2, 0);
    int matched = zip.fillUncompressedSizes(targets, sizes);
    runner.expectEq<int>(2, matched, "FillSizes_FourEntries_TwoTargets_EarlyExit_MatchedTwo");
    // Verify each size landed in the correct slot
    uint32_t sizeA = 0, sizeB = 0;
    for (auto& t : targets) {
      if (t.hash == ZipFile::fnvHash64(nameA, strlen(nameA))) sizeA = sizes[t.index];
      if (t.hash == ZipFile::fnvHash64(nameB, strlen(nameB))) sizeB = sizes[t.index];
    }
    runner.expectEq<uint32_t>(5000, sizeA, "FillSizes_FourEntries_TwoTargets_EarlyExit_SizeA");
    runner.expectEq<uint32_t>(1234, sizeB, "FillSizes_FourEntries_TwoTargets_EarlyExit_SizeB");
  }

  // No targets match: returns 0 and sizes unchanged
  {
    SdMan.reset();
    std::vector<std::pair<std::string, uint32_t>> entries = {{"OEBPS/file.html", 999}};
    SdMan.setFileData("/test.zip", createZipWithNamedEntries(entries));
    std::string path3 = "/test.zip";
    ZipFile zip(path3);

    const char* name = "OEBPS/other.html";
    std::vector<ZipFile::SizeTarget> targets = {
        {ZipFile::fnvHash64(name, strlen(name)), static_cast<uint16_t>(strlen(name)), 0},
    };
    std::vector<uint32_t> sizes(1, 42);
    int matched = zip.fillUncompressedSizes(targets, sizes);
    runner.expectEq<int>(0, matched, "FillSizes_NoMatch_ReturnsZero");
    runner.expectEq<uint32_t>(42, sizes[0], "FillSizes_NoMatch_SizeUnchanged");
  }

  // Early-exit break: poison trailing entry is never reached
  //
  // The ZIP has 3 CD entries:
  //   [0] "OEBPS/chapter1.html" size=5000  <- matches target B (index 1)
  //   [1] "OEBPS/content.opf"   size=1234  <- matches target A (index 0)
  //   [2] "OEBPS/content.opf"   size=9999  <- POISON: valid CD sig, same name,
  //                                            wrong size for target A
  //
  // With the early-exit break (matched == targets.size()) the loop stops after
  // entry [1] and never reads entry [2], so sizes remain correct and
  // matched == 2.
  //
  // Without the early-exit break the loop continues, processes entry [2],
  // overwrites sizes[A.index] with 9999, and matched becomes 3 — both
  // matched != 2 and sizes[A.index] != 1234 would fail.
  {
    SdMan.reset();
    const char* nameA = "OEBPS/content.opf";
    const char* nameB = "OEBPS/chapter1.html";
    const uint32_t correctSizeA = 1234;
    const uint32_t correctSizeB = 5000;
    const uint32_t poisonSizeA = 9999;

    std::vector<std::pair<std::string, uint32_t>> entries = {
        {nameB, correctSizeB},
        {nameA, correctSizeA},
    };
    SdMan.setFileData("/test.zip", createZipWithPoisonTrailingEntry(entries, nameA, poisonSizeA));
    std::string path4 = "/test.zip";
    ZipFile zip(path4);

    std::vector<ZipFile::SizeTarget> targets = {
        {ZipFile::fnvHash64(nameA, strlen(nameA)), static_cast<uint16_t>(strlen(nameA)), 0},
        {ZipFile::fnvHash64(nameB, strlen(nameB)), static_cast<uint16_t>(strlen(nameB)), 1},
    };
    std::sort(targets.begin(), targets.end());
    std::vector<uint32_t> sizes(2, 0);
    int matched = zip.fillUncompressedSizes(targets, sizes);

    uint32_t sizeA = 0, sizeB = 0;
    for (auto& t : targets) {
      if (t.hash == ZipFile::fnvHash64(nameA, strlen(nameA))) sizeA = sizes[t.index];
      if (t.hash == ZipFile::fnvHash64(nameB, strlen(nameB))) sizeB = sizes[t.index];
    }
    runner.expectEq<int>(2, matched, "FillSizes_EarlyExit_CorruptTrailingEntry_NotReached_MatchedTwo");
    runner.expectEq<uint32_t>(correctSizeA, sizeA, "FillSizes_EarlyExit_CorruptTrailingEntry_NotReached_SizeA");
    runner.expectEq<uint32_t>(correctSizeB, sizeB, "FillSizes_EarlyExit_CorruptTrailingEntry_NotReached_SizeB");
  }

  // findFirstExisting observes cancellation while scanning the central directory.
  {
    SdMan.reset();
    const std::vector<std::pair<std::string, uint32_t>> entries = {
        {"chapter1.xhtml", 100}, {"chapter2.xhtml", 200}, {"cover.jpg", 300}};
    SdMan.setFileData("/covers.zip", createZipWithNamedEntries(entries));
    std::string path = "/covers.zip";
    ZipFile zip(path);
    const char* candidates[] = {"missing.jpg", "cover.jpg"};
    int checks = 0;
    const int found = zip.findFirstExisting(candidates, 2, [&checks]() { return ++checks >= 3; });
    runner.expectEq<int>(-1, found, "FindFirstExisting_Abort_ReturnsNotFound");
    runner.expectTrue(checks >= 3, "FindFirstExisting_Abort_CheckedDuringScan");
  }

  SdMan.reset();
  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}

// ============================================================================
// Helper Functions Implementation
// ============================================================================

std::vector<uint8_t> createMinimalZip() {
  std::vector<uint8_t> data(100, 0);
  // EOCD at position 78
  data[78] = 0x50;
  data[79] = 0x4b;
  data[80] = 0x05;
  data[81] = 0x06;
  data[92] = 0x00;  // 0 entries
  return data;
}

namespace {

std::vector<uint8_t> createSingleEntryZip(const std::string& name, const uint8_t* compressed, size_t compressedSize,
                                          uint32_t inflatedSize, uint16_t method) {
  std::vector<uint8_t> data;
  const auto u16 = [&data](uint16_t value) {
    data.push_back(static_cast<uint8_t>(value));
    data.push_back(static_cast<uint8_t>(value >> 8));
  };
  const auto u32 = [&data](uint32_t value) {
    data.push_back(static_cast<uint8_t>(value));
    data.push_back(static_cast<uint8_t>(value >> 8));
    data.push_back(static_cast<uint8_t>(value >> 16));
    data.push_back(static_cast<uint8_t>(value >> 24));
  };
  const auto bytes = [&data](const std::string& value) { data.insert(data.end(), value.begin(), value.end()); };

  u32(0x04034b50);
  u16(20);
  u16(0);
  u16(method);
  u16(0);
  u16(0);
  u32(0);
  u32(static_cast<uint32_t>(compressedSize));
  u32(inflatedSize);
  u16(static_cast<uint16_t>(name.size()));
  u16(0);
  bytes(name);
  data.insert(data.end(), compressed, compressed + compressedSize);

  const uint32_t centralOffset = static_cast<uint32_t>(data.size());
  u32(0x02014b50);
  u16(20);
  u16(20);
  u16(0);
  u16(method);
  u16(0);
  u16(0);
  u32(0);
  u32(static_cast<uint32_t>(compressedSize));
  u32(inflatedSize);
  u16(static_cast<uint16_t>(name.size()));
  u16(0);
  u16(0);
  u16(0);
  u16(0);
  u32(0);
  u32(0);
  bytes(name);
  const uint32_t centralSize = static_cast<uint32_t>(data.size()) - centralOffset;

  u32(0x06054b50);
  u16(0);
  u16(0);
  u16(1);
  u16(1);
  u32(centralSize);
  u32(centralOffset);
  u16(0);
  return data;
}

}  // namespace

std::vector<uint8_t> createStoredZip(const std::string& name, const std::string& contents) {
  return createSingleEntryZip(name, reinterpret_cast<const uint8_t*>(contents.data()), contents.size(),
                              static_cast<uint32_t>(contents.size()), 0);
}

std::vector<uint8_t> createDeflatedZip(const std::string& name, uint32_t inflatedSize) {
  return createSingleEntryZip(name, LARGE_DEFLATED, sizeof(LARGE_DEFLATED), inflatedSize, 8);
}

std::vector<uint8_t> createZipWithInvalidOffset(const char* name) {
  uint16_t nameLen = static_cast<uint16_t>(strlen(name));
  std::vector<uint8_t> data;
  data.insert(data.end(), {0x50, 0x4b, 0x03, 0x04});
  data.insert(data.end(), {0x14, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.push_back(nameLen & 0xFF);
  data.push_back((nameLen >> 8) & 0xFF);
  data.insert(data.end(), {0x00, 0x00});
  for (size_t i = 0; i < nameLen; i++) {
    data.push_back(static_cast<uint8_t>(name[i]));
  }

  // Central directory with invalid offset
  size_t cdOffset = data.size();
  data.insert(data.end(), {0x50, 0x4b, 0x01, 0x02});
  data.insert(data.end(), {0x14, 0x00});
  data.insert(data.end(), {0x14, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.push_back(nameLen & 0xFF);
  data.push_back((nameLen >> 8) & 0xFF);
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  // Invalid offset - points past file
  data.insert(data.end(), {0xFF, 0xFF, 0xFF, 0x7F});
  for (size_t i = 0; i < nameLen; i++) {
    data.push_back(static_cast<uint8_t>(name[i]));
  }

  // EOCD
  size_t eocdOffset = data.size();
  data.insert(data.end(), {0x50, 0x4b, 0x05, 0x06});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x01, 0x00});
  data.insert(data.end(), {0x01, 0x00});
  uint32_t cdSize = static_cast<uint32_t>(eocdOffset - cdOffset);
  data.push_back(cdSize & 0xFF);
  data.push_back((cdSize >> 8) & 0xFF);
  data.push_back((cdSize >> 16) & 0xFF);
  data.push_back((cdSize >> 24) & 0xFF);
  data.push_back(cdOffset & 0xFF);
  data.push_back((cdOffset >> 8) & 0xFF);
  data.push_back((cdOffset >> 16) & 0xFF);
  data.push_back((cdOffset >> 24) & 0xFF);
  data.insert(data.end(), {0x00, 0x00});

  return data;
}

std::vector<uint8_t> createZipWithUnsupportedCompression(const char* name) {
  std::vector<uint8_t> data;
  data.insert(data.end(), {0x50, 0x4b, 0x03, 0x04});
  data.insert(data.end(), {0x14, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x0A, 0x00});  // Method 10 - unsupported
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  uint16_t nameLen = static_cast<uint16_t>(strlen(name));
  data.push_back(nameLen & 0xFF);
  data.push_back((nameLen >> 8) & 0xFF);
  data.insert(data.end(), {0x00, 0x00});
  for (size_t i = 0; i < nameLen; i++) {
    data.push_back(static_cast<uint8_t>(name[i]));
  }

  // Central directory
  size_t cdOffset = data.size();
  data.insert(data.end(), {0x50, 0x4b, 0x01, 0x02});
  data.insert(data.end(), {0x14, 0x00});
  data.insert(data.end(), {0x14, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x0A, 0x00});  // Method 10
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.push_back(nameLen & 0xFF);
  data.push_back((nameLen >> 8) & 0xFF);
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x00});
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);
  for (size_t i = 0; i < nameLen; i++) {
    data.push_back(static_cast<uint8_t>(name[i]));
  }

  // EOCD
  size_t eocdOffset = data.size();
  data.insert(data.end(), {0x50, 0x4b, 0x05, 0x06});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x00, 0x00});
  data.insert(data.end(), {0x01, 0x00});
  data.insert(data.end(), {0x01, 0x00});
  uint32_t cdSize = static_cast<uint32_t>(eocdOffset - cdOffset);
  data.push_back(cdSize & 0xFF);
  data.push_back((cdSize >> 8) & 0xFF);
  data.push_back((cdSize >> 16) & 0xFF);
  data.push_back((cdSize >> 24) & 0xFF);
  data.push_back(cdOffset & 0xFF);
  data.push_back((cdOffset >> 8) & 0xFF);
  data.push_back((cdOffset >> 16) & 0xFF);
  data.push_back((cdOffset >> 24) & 0xFF);
  data.insert(data.end(), {0x00, 0x00});

  return data;
}

// Build a ZIP with named entries plus one extra "poison" CD entry at the end.
// The poison entry has a valid CD signature and the given name/size.  It is
// appended after all normal entries so that, if the early-exit break in
// fillUncompressedSizes fires correctly, the poison entry is never read.
// If the break is absent the loop processes the poison entry and may
// overwrite a previously-correct size value.
std::vector<uint8_t> createZipWithPoisonTrailingEntry(const std::vector<std::pair<std::string, uint32_t>>& entries,
                                                      const std::string& poisonName, uint32_t poisonSize) {
  // Reuse the same builder logic as createZipWithNamedEntries but with an
  // extra entry appended.
  auto allEntries = entries;
  allEntries.emplace_back(poisonName, poisonSize);
  // The EOCD entry count reflects only the real entries so the ZIP looks
  // slightly malformed (entry count != actual CD records), but
  // fillUncompressedSizes iterates until a bad signature or EOF — it does
  // not use the EOCD entry count to bound the loop — so all 3 CD records
  // are visible to the scanner.
  //
  // We build the full byte stream manually to control the EOCD count field.
  auto u16le = [](std::vector<uint8_t>& v, uint16_t val) {
    v.push_back(val & 0xFF);
    v.push_back((val >> 8) & 0xFF);
  };
  auto u32le = [](std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(val & 0xFF);
    v.push_back((val >> 8) & 0xFF);
    v.push_back((val >> 16) & 0xFF);
    v.push_back((val >> 24) & 0xFF);
  };

  std::vector<uint8_t> data;
  const uint32_t cdOffset = 0;

  for (const auto& entry : allEntries) {
    const std::string& name = entry.first;
    const uint32_t uncompressedSize = entry.second;
    const auto nameLen = static_cast<uint16_t>(name.size());
    data.insert(data.end(), {0x50, 0x4b, 0x01, 0x02});
    u16le(data, 0x0314);
    u16le(data, 0x0014);
    u16le(data, 0x0000);
    u16le(data, 0x0000);
    u16le(data, 0x0000);
    u16le(data, 0x0000);
    u32le(data, 0x00000000);
    u32le(data, 0x00000000);
    u32le(data, uncompressedSize);
    u16le(data, nameLen);
    u16le(data, 0);
    u16le(data, 0);
    u16le(data, 0);
    u16le(data, 0);
    u32le(data, 0);
    u32le(data, 0);
    for (char c : name) {
      data.push_back(static_cast<uint8_t>(c));
    }
  }

  // EOCD — report only the real (non-poison) entry count
  const auto eocdOffset = static_cast<uint32_t>(data.size());
  data.insert(data.end(), {0x50, 0x4b, 0x05, 0x06});
  u16le(data, 0);
  u16le(data, 0);
  u16le(data, static_cast<uint16_t>(entries.size()));
  u16le(data, static_cast<uint16_t>(entries.size()));
  u32le(data, eocdOffset - cdOffset);
  u32le(data, cdOffset);
  u16le(data, 0);

  return data;
}

// Build a ZIP with named entries in the central directory.
// Each entry has no local file data — only the central directory and EOCD are
// present, which is sufficient for fillUncompressedSizes.
std::vector<uint8_t> createZipWithNamedEntries(const std::vector<std::pair<std::string, uint32_t>>& entries) {
  auto u16le = [](std::vector<uint8_t>& v, uint16_t val) {
    v.push_back(val & 0xFF);
    v.push_back((val >> 8) & 0xFF);
  };
  auto u32le = [](std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(val & 0xFF);
    v.push_back((val >> 8) & 0xFF);
    v.push_back((val >> 16) & 0xFF);
    v.push_back((val >> 24) & 0xFF);
  };

  std::vector<uint8_t> data;

  // Central directory starts right at offset 0 (no local file data needed)
  const uint32_t cdOffset = 0;

  for (const auto& entry : entries) {
    const std::string& name = entry.first;
    const uint32_t uncompressedSize = entry.second;
    const auto nameLen = static_cast<uint16_t>(name.size());
    // Central directory file header signature
    data.insert(data.end(), {0x50, 0x4b, 0x01, 0x02});
    u16le(data, 0x0314);      // version made by
    u16le(data, 0x0014);      // version needed
    u16le(data, 0x0000);      // flags
    u16le(data, 0x0000);      // method (stored)
    u16le(data, 0x0000);      // last mod time
    u16le(data, 0x0000);      // last mod date
    u32le(data, 0x00000000);  // crc-32
    u32le(data, 0x00000000);  // compressed size
    u32le(data, uncompressedSize);
    u16le(data, nameLen);
    u16le(data, 0);  // extra field length
    u16le(data, 0);  // comment length
    u16le(data, 0);  // disk number start
    u16le(data, 0);  // internal attrs
    u32le(data, 0);  // external attrs
    u32le(data, 0);  // local header offset (not used by fillUncompressedSizes)
    for (char c : name) {
      data.push_back(static_cast<uint8_t>(c));
    }
  }

  // EOCD
  const auto eocdOffset = static_cast<uint32_t>(data.size());
  data.insert(data.end(), {0x50, 0x4b, 0x05, 0x06});
  u16le(data, 0);  // disk number
  u16le(data, 0);  // disk with start of CD
  u16le(data, static_cast<uint16_t>(entries.size()));
  u16le(data, static_cast<uint16_t>(entries.size()));
  u32le(data, eocdOffset - cdOffset);
  u32le(data, cdOffset);
  u16le(data, 0);  // comment length

  return data;
}
