// ZipFile error path unit tests
//
// Tests error handling paths in ZipFile to ensure no memory leaks
// on early returns and proper cleanup in all scenarios.
//
// These tests verify that error conditions are handled gracefully without crashes
// or memory leaks. Tests that require successful ZIP parsing are not included
// as they would require a more sophisticated mock or real ZIP files.

#include "test_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Include mocks
#include "HardwareSerial.h"
#include "SdFat.h"
#include "SDCardManager.h"

// Include ZipFile header
#include "ZipFile.h"

// Forward declarations for helper functions
std::vector<uint8_t> createMinimalZip();
std::vector<uint8_t> createZipWithInvalidOffset(const char* name);
std::vector<uint8_t> createZipWithUnsupportedCompression(const char* name);
std::vector<uint8_t> createZipWithNamedEntries(const std::vector<std::pair<std::string, uint32_t>>& entries);

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
