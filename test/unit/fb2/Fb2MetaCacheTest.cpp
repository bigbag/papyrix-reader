// Fb2 metadata cache serialization unit tests
//
// Tests the binary format used by Fb2::saveMetaCache() / loadMetaCache()
// by reimplementing the serialization protocol in a test-friendly way,
// without needing SD card or Serial dependencies.

#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Include mocks
#include "HardwareSerial.h"
#include "SdFat.h"

// Include serialization library
#include "Serialization.h"

namespace {
constexpr uint8_t kMetaCacheVersion = 2;
}

struct TocItem {
  std::string title;
  int sectionIndex;
};

// Write meta cache in the same format as Fb2::saveMetaCache()
static void writeMetaCache(FsFile& file, const std::string& title, const std::string& author,
                           const std::string& coverPath, uint32_t fileSize, uint16_t sectionCount,
                           const std::vector<TocItem>& tocItems) {
  serialization::writePod(file, kMetaCacheVersion);
  serialization::writeString(file, title);
  serialization::writeString(file, author);
  serialization::writeString(file, coverPath);
  serialization::writePod(file, fileSize);
  serialization::writePod(file, sectionCount);

  const uint16_t tocItemCount = static_cast<uint16_t>(tocItems.size());
  serialization::writePod(file, tocItemCount);

  for (const auto& item : tocItems) {
    serialization::writeString(file, item.title);
    const int16_t idx = static_cast<int16_t>(item.sectionIndex);
    serialization::writePod(file, idx);
  }
}

// Read meta cache in the same format as Fb2::loadMetaCache()
struct MetaCacheData {
  std::string title;
  std::string author;
  std::string coverPath;
  uint32_t fileSize = 0;
  uint16_t sectionCount = 0;
  std::vector<TocItem> tocItems;
};

static bool readMetaCache(FsFile& file, MetaCacheData& data) {
  uint8_t version;
  if (!serialization::readPodChecked(file, version) || version != kMetaCacheVersion) {
    return false;
  }

  if (!serialization::readString(file, data.title) || !serialization::readString(file, data.author) ||
      !serialization::readString(file, data.coverPath)) {
    return false;
  }

  if (!serialization::readPodChecked(file, data.fileSize)) {
    return false;
  }

  if (!serialization::readPodChecked(file, data.sectionCount)) {
    return false;
  }

  uint16_t tocItemCount;
  if (!serialization::readPodChecked(file, tocItemCount)) {
    return false;
  }

  data.tocItems.clear();
  data.tocItems.reserve(tocItemCount);
  for (uint16_t i = 0; i < tocItemCount; i++) {
    TocItem item;
    if (!serialization::readString(file, item.title)) {
      return false;
    }
    int16_t idx;
    if (!serialization::readPodChecked(file, idx)) {
      return false;
    }
    item.sectionIndex = idx;
    data.tocItems.push_back(std::move(item));
  }

  return true;
}

int main() {
  TestUtils::TestRunner runner("Fb2 Meta Cache");

  // Test 1: Basic roundtrip with all fields
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc = {{"Chapter 1", 0}, {"Chapter 2", 1}, {"Chapter 3", 2}};
    writeMetaCache(file, "Test Book", "John Doe", "/cover.jpg", 123456, 3, toc);

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "roundtrip: reads successfully");
    runner.expectEqual("Test Book", data.title, "roundtrip: title");
    runner.expectEqual("John Doe", data.author, "roundtrip: author");
    runner.expectEqual("/cover.jpg", data.coverPath, "roundtrip: coverPath");
    runner.expectEq(static_cast<uint32_t>(123456), data.fileSize, "roundtrip: fileSize");
    runner.expectEq(static_cast<uint16_t>(3), data.sectionCount, "roundtrip: sectionCount");
    runner.expectEq(static_cast<size_t>(3), data.tocItems.size(), "roundtrip: tocItems count");
    runner.expectEqual("Chapter 1", data.tocItems[0].title, "roundtrip: toc[0] title");
    runner.expectEq(0, data.tocItems[0].sectionIndex, "roundtrip: toc[0] index");
    runner.expectEqual("Chapter 3", data.tocItems[2].title, "roundtrip: toc[2] title");
    runner.expectEq(2, data.tocItems[2].sectionIndex, "roundtrip: toc[2] index");
  }

  // Test 2: Empty TOC
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "No Chapters", "Author", "", 5000, 0, {});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "empty_toc: reads successfully");
    runner.expectEqual("No Chapters", data.title, "empty_toc: title");
    runner.expectEq(static_cast<size_t>(0), data.tocItems.size(), "empty_toc: no items");
  }

  // Test 3: Empty strings (no title, no author, no cover)
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "", "", "", 0, 0, {});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "empty_strings: reads successfully");
    runner.expectEqual("", data.title, "empty_strings: empty title");
    runner.expectEqual("", data.author, "empty_strings: empty author");
    runner.expectEqual("", data.coverPath, "empty_strings: empty coverPath");
    runner.expectEq(static_cast<uint32_t>(0), data.fileSize, "empty_strings: zero fileSize");
  }

  // Test 4: UTF-8 metadata
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc = {
        {"\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1", 0},  // "Глава 1"
        {"\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 2", 1},  // "Глава 2"
    };
    writeMetaCache(file,
                   "\xD0\x92\xD0\xBE\xD0\xB9\xD0\xBD\xD0\xB0 \xD0\xB8 \xD0\xBC\xD0\xB8\xD1\x80",  // "Война и мир"
                   "\xD0\x9B\xD0\xB5\xD0\xB2 \xD0\xA2\xD0\xBE\xD0\xBB\xD1\x81\xD1\x82\xD0\xBE\xD0\xB9",  // "Лев Толстой"
                   "", 999999, 2, toc);

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "utf8: reads successfully");
    runner.expectEqual("\xD0\x92\xD0\xBE\xD0\xB9\xD0\xBD\xD0\xB0 \xD0\xB8 \xD0\xBC\xD0\xB8\xD1\x80", data.title,
                       "utf8: title preserved");
    runner.expectEqual(
        "\xD0\x9B\xD0\xB5\xD0\xB2 \xD0\xA2\xD0\xBE\xD0\xBB\xD1\x81\xD1\x82\xD0\xBE\xD0\xB9", data.author,
        "utf8: author preserved");
    runner.expectEqual("\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1", data.tocItems[0].title,
                       "utf8: toc title preserved");
  }

  // Test 5: Version mismatch rejects cache
  {
    FsFile file;
    file.setBuffer("");

    // Write with a different version
    uint8_t badVersion = 99;
    serialization::writePod(file, badVersion);
    serialization::writeString(file, std::string("Title"));

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectFalse(ok, "version_mismatch: rejected");
  }

  // Test 6: Empty file rejects
  {
    FsFile file;
    file.setBuffer("");

    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectFalse(ok, "empty_file: rejected");
  }

  // Test 7: Truncated after version byte
  {
    FsFile file;
    file.setBuffer("");
    serialization::writePod(file, kMetaCacheVersion);
    // No more data - title string read should fail

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectFalse(ok, "truncated_after_version: rejected");
  }

  // Test 8: Truncated in TOC items
  {
    FsFile file;
    file.setBuffer("");

    serialization::writePod(file, kMetaCacheVersion);
    serialization::writeString(file, std::string("Title"));
    serialization::writeString(file, std::string("Author"));
    serialization::writeString(file, std::string(""));
    uint32_t fs = 1000;
    serialization::writePod(file, fs);
    uint16_t sc = 5;
    serialization::writePod(file, sc);
    uint16_t tocCount = 3;  // Claim 3 items
    serialization::writePod(file, tocCount);
    // Only write 1 item
    serialization::writeString(file, std::string("Chapter 1"));
    int16_t idx = 0;
    serialization::writePod(file, idx);
    // Missing items 2 and 3

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectFalse(ok, "truncated_toc: rejected");
  }

  // Test 9: Many TOC items
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc;
    for (int i = 0; i < 100; i++) {
      toc.push_back({"Section " + std::to_string(i + 1), i});
    }
    writeMetaCache(file, "Big Book", "Author", "", 5000000, 100, toc);

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "many_toc: reads successfully");
    runner.expectEq(static_cast<size_t>(100), data.tocItems.size(), "many_toc: 100 items");
    runner.expectEqual("Section 1", data.tocItems[0].title, "many_toc: first item");
    runner.expectEqual("Section 100", data.tocItems[99].title, "many_toc: last item");
    runner.expectEq(99, data.tocItems[99].sectionIndex, "many_toc: last index");
  }

  // Test 10: Large file size value
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "Large", "Author", "", 0xFFFFFFFE, 1, {{"Ch1", 0}});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "large_filesize: reads successfully");
    runner.expectEq(static_cast<uint32_t>(0xFFFFFFFE), data.fileSize, "large_filesize: max-1 value preserved");
  }

  return runner.allPassed() ? 0 : 1;
}
