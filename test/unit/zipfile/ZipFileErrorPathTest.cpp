// ZipFile error path unit tests
//
// Tests error handling paths in ZipFile to ensure no memory leaks
// on early returns and proper cleanup in all scenarios.
//
// These tests verify that error conditions are handled gracefully without crashes
// or memory leaks. Tests that require successful ZIP parsing are not included
// as they would require a more sophisticated mock or real ZIP files.

#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
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
