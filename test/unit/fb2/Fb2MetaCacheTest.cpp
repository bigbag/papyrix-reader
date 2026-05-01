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
constexpr uint8_t kMetaCacheVersion = 5;
}

struct SectionOffset {
  uint32_t startOffset = 0;
  uint32_t endOffset = 0;
};

struct TocItem {
  std::string title;
  int sectionIndex;
};

// Write meta cache in the same format as Fb2::saveMetaCache()
static void writeMetaCache(FsFile& file, const std::string& title, const std::string& author,
                           const std::string& coverRef, const std::string& language,
                           const std::string& coverContentType, int64_t coverBinaryOffset, uint32_t fileSize,
                           uint16_t sectionCount, const std::vector<TocItem>& tocItems,
                           const std::vector<SectionOffset>& sectionOffsets = {}) {
  serialization::writePod(file, kMetaCacheVersion);
  serialization::writeString(file, title);
  serialization::writeString(file, author);
  serialization::writeString(file, coverRef);
  serialization::writeString(file, language);
  serialization::writeString(file, coverContentType);
  serialization::writePod(file, coverBinaryOffset);
  serialization::writePod(file, fileSize);
  serialization::writePod(file, sectionCount);

  const uint16_t tocItemCount = static_cast<uint16_t>(tocItems.size());
  serialization::writePod(file, tocItemCount);

  for (const auto& item : tocItems) {
    serialization::writeString(file, item.title);
    const int16_t idx = static_cast<int16_t>(item.sectionIndex);
    serialization::writePod(file, idx);
  }

  const uint16_t sectionOffsetCount = static_cast<uint16_t>(sectionOffsets.size());
  serialization::writePod(file, sectionOffsetCount);
  for (const auto& off : sectionOffsets) {
    serialization::writePod(file, off.startOffset);
    serialization::writePod(file, off.endOffset);
  }
}

// Read meta cache in the same format as Fb2::loadMetaCache()
struct MetaCacheData {
  std::string title;
  std::string author;
  std::string coverRef;
  std::string language;
  std::string coverContentType;
  int64_t coverBinaryOffset = -1;
  uint32_t fileSize = 0;
  uint16_t sectionCount = 0;
  std::vector<TocItem> tocItems;
  std::vector<SectionOffset> sectionOffsets;
};

static bool readMetaCache(FsFile& file, MetaCacheData& data) {
  uint8_t version;
  if (!serialization::readPodChecked(file, version) || version != kMetaCacheVersion) {
    return false;
  }

  if (!serialization::readString(file, data.title) || !serialization::readString(file, data.author) ||
      !serialization::readString(file, data.coverRef) || !serialization::readString(file, data.language) ||
      !serialization::readString(file, data.coverContentType)) {
    return false;
  }

  if (!serialization::readPodChecked(file, data.coverBinaryOffset)) {
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

  // Load section offsets (v5+) — graceful: missing is OK
  uint16_t sectionOffsetCount;
  if (serialization::readPodChecked(file, sectionOffsetCount)) {
    data.sectionOffsets.clear();
    data.sectionOffsets.reserve(sectionOffsetCount);
    for (uint16_t i = 0; i < sectionOffsetCount; i++) {
      SectionOffset off;
      if (!serialization::readPodChecked(file, off.startOffset) ||
          !serialization::readPodChecked(file, off.endOffset)) {
        data.sectionOffsets.clear();
        break;
      }
      data.sectionOffsets.push_back(off);
    }
  }

  return true;
}

// Build a compact LUT of file offsets (mirrors Fb2::loadMetaCache LUT logic)
struct MetaCacheLut {
  std::string title;
  std::string author;
  std::string coverRef;
  std::string language;
  std::string coverContentType;
  int64_t coverBinaryOffset = -1;
  uint32_t fileSize = 0;
  uint16_t sectionCount = 0;
  uint16_t tocItemCount = 0;
  std::vector<uint32_t> tocLut;
};

static bool buildMetaCacheLut(FsFile& file, MetaCacheLut& lut) {
  uint8_t version;
  if (!serialization::readPodChecked(file, version) || version != kMetaCacheVersion) {
    return false;
  }

  if (!serialization::readString(file, lut.title) || !serialization::readString(file, lut.author) ||
      !serialization::readString(file, lut.coverRef) || !serialization::readString(file, lut.language) ||
      !serialization::readString(file, lut.coverContentType)) {
    return false;
  }

  if (!serialization::readPodChecked(file, lut.coverBinaryOffset)) return false;
  if (!serialization::readPodChecked(file, lut.fileSize)) return false;
  if (!serialization::readPodChecked(file, lut.sectionCount)) return false;

  uint16_t tocItemCount;
  if (!serialization::readPodChecked(file, tocItemCount)) return false;

  lut.tocItemCount = tocItemCount;
  lut.tocLut.clear();
  lut.tocLut.reserve(tocItemCount);
  for (uint16_t i = 0; i < tocItemCount; i++) {
    lut.tocLut.push_back(static_cast<uint32_t>(file.position()));
    int16_t dummyIdx;
    if (!serialization::skipString(file) || !serialization::readPodChecked(file, dummyIdx)) {
      lut.tocLut.clear();
      lut.tocItemCount = 0;
      return false;
    }
  }
  return true;
}

// Read a single TOC item by LUT offset (mirrors Fb2::getTocItem)
static TocItem readTocItemByLut(FsFile& file, const MetaCacheLut& lut, uint16_t index) {
  TocItem item;
  item.sectionIndex = -1;
  if (index >= lut.tocItemCount) return item;

  file.seek(lut.tocLut[index]);
  serialization::readString(file, item.title);
  int16_t idx;
  if (serialization::readPodChecked(file, idx)) {
    item.sectionIndex = idx;
  }
  return item;
}

int main() {
  TestUtils::TestRunner runner("Fb2 Meta Cache");

  // Test 1: Basic roundtrip with all fields
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc = {{"Chapter 1", 0}, {"Chapter 2", 1}, {"Chapter 3", 2}};
    writeMetaCache(file, "Test Book", "John Doe", "/cover.jpg", "en", "image/jpeg", 50000, 123456, 3, toc);

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "roundtrip: reads successfully");
    runner.expectEqual("Test Book", data.title, "roundtrip: title");
    runner.expectEqual("John Doe", data.author, "roundtrip: author");
    runner.expectEqual("/cover.jpg", data.coverRef, "roundtrip: coverRef");
    runner.expectEqual("en", data.language, "roundtrip: language");
    runner.expectEqual("image/jpeg", data.coverContentType, "roundtrip: coverContentType");
    runner.expectEq(static_cast<int64_t>(50000), data.coverBinaryOffset, "roundtrip: coverBinaryOffset");
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

    writeMetaCache(file, "No Chapters", "Author", "", "", "", -1, 5000, 0, {});

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

    writeMetaCache(file, "", "", "", "", "", -1, 0, 0, {});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "empty_strings: reads successfully");
    runner.expectEqual("", data.title, "empty_strings: empty title");
    runner.expectEqual("", data.author, "empty_strings: empty author");
    runner.expectEqual("", data.coverRef, "empty_strings: empty coverRef");
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
                   "", "ru", "", -1, 999999, 2, toc);

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
    serialization::writeString(file, std::string(""));
    serialization::writeString(file, std::string(""));
    int64_t offset = -1;
    serialization::writePod(file, offset);
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
    writeMetaCache(file, "Big Book", "Author", "", "", "", -1, 5000000, 100, toc);

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

    writeMetaCache(file, "Large", "Author", "", "", "", -1, 0xFFFFFFFE, 1, {{"Ch1", 0}});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "large_filesize: reads successfully");
    runner.expectEq(static_cast<uint32_t>(0xFFFFFFFE), data.fileSize, "large_filesize: max-1 value preserved");
  }

  // Test 11: LUT-based lazy loading roundtrip
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc = {{"Chapter 1", 0}, {"Chapter 2", 1}, {"Chapter 3", 2}};
    writeMetaCache(file, "Lazy Book", "Jane Doe", "/cover.png", "fr", "image/png", 12345, 54321, 3, toc);

    file.seek(0);
    MetaCacheLut lut;
    bool ok = buildMetaCacheLut(file, lut);
    runner.expectTrue(ok, "lut_roundtrip: builds LUT successfully");
    runner.expectEqual("Lazy Book", lut.title, "lut_roundtrip: title");
    runner.expectEqual("Jane Doe", lut.author, "lut_roundtrip: author");
    runner.expectEq(static_cast<uint16_t>(3), lut.tocItemCount, "lut_roundtrip: tocItemCount");
    runner.expectEq(static_cast<size_t>(3), lut.tocLut.size(), "lut_roundtrip: LUT size");

    // Read individual items via LUT
    TocItem item0 = readTocItemByLut(file, lut, 0);
    runner.expectEqual("Chapter 1", item0.title, "lut_roundtrip: item 0 title");
    runner.expectEq(0, item0.sectionIndex, "lut_roundtrip: item 0 index");

    TocItem item2 = readTocItemByLut(file, lut, 2);
    runner.expectEqual("Chapter 3", item2.title, "lut_roundtrip: item 2 title");
    runner.expectEq(2, item2.sectionIndex, "lut_roundtrip: item 2 index");

    // Read middle item
    TocItem item1 = readTocItemByLut(file, lut, 1);
    runner.expectEqual("Chapter 2", item1.title, "lut_roundtrip: item 1 title");
    runner.expectEq(1, item1.sectionIndex, "lut_roundtrip: item 1 index");
  }

  // Test 12: LUT with empty TOC
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "No TOC", "Author", "", "", "", -1, 1000, 0, {});

    file.seek(0);
    MetaCacheLut lut;
    bool ok = buildMetaCacheLut(file, lut);
    runner.expectTrue(ok, "lut_empty: builds LUT successfully");
    runner.expectEq(static_cast<uint16_t>(0), lut.tocItemCount, "lut_empty: zero items");
    runner.expectEq(static_cast<size_t>(0), lut.tocLut.size(), "lut_empty: empty LUT");

    // Out-of-range access returns default
    TocItem item = readTocItemByLut(file, lut, 0);
    runner.expectEq(-1, item.sectionIndex, "lut_empty: out-of-range returns default");
  }

  // Test 13: LUT with many items and random access
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc;
    for (int i = 0; i < 100; i++) {
      toc.push_back({"Section " + std::to_string(i + 1), i});
    }
    writeMetaCache(file, "Big Book", "Author", "", "", "", -1, 5000000, 100, toc);

    file.seek(0);
    MetaCacheLut lut;
    bool ok = buildMetaCacheLut(file, lut);
    runner.expectTrue(ok, "lut_many: builds LUT successfully");
    runner.expectEq(static_cast<uint16_t>(100), lut.tocItemCount, "lut_many: 100 items");

    // Random access pattern (not sequential)
    TocItem item99 = readTocItemByLut(file, lut, 99);
    runner.expectEqual("Section 100", item99.title, "lut_many: last item");
    runner.expectEq(99, item99.sectionIndex, "lut_many: last index");

    TocItem item0 = readTocItemByLut(file, lut, 0);
    runner.expectEqual("Section 1", item0.title, "lut_many: first item after last");
    runner.expectEq(0, item0.sectionIndex, "lut_many: first index");

    TocItem item50 = readTocItemByLut(file, lut, 50);
    runner.expectEqual("Section 51", item50.title, "lut_many: middle item");
    runner.expectEq(50, item50.sectionIndex, "lut_many: middle index");
  }

  // Test 14: LUT with UTF-8 titles
  {
    FsFile file;
    file.setBuffer("");

    std::vector<TocItem> toc = {
        {"\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1", 0},
        {"\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 2", 1},
    };
    writeMetaCache(file, "UTF8", "Author", "", "", "", -1, 1000, 2, toc);

    file.seek(0);
    MetaCacheLut lut;
    bool ok = buildMetaCacheLut(file, lut);
    runner.expectTrue(ok, "lut_utf8: builds LUT");

    TocItem item0 = readTocItemByLut(file, lut, 0);
    runner.expectEqual("\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1", item0.title, "lut_utf8: item 0 title");

    TocItem item1 = readTocItemByLut(file, lut, 1);
    runner.expectEqual("\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 2", item1.title, "lut_utf8: item 1 title");
  }

  // Test 15: LUT truncated cache rejects
  {
    FsFile file;
    file.setBuffer("");

    serialization::writePod(file, kMetaCacheVersion);
    serialization::writeString(file, std::string("Title"));
    serialization::writeString(file, std::string("Author"));
    serialization::writeString(file, std::string(""));
    serialization::writeString(file, std::string(""));
    serialization::writeString(file, std::string(""));
    int64_t offset2 = -1;
    serialization::writePod(file, offset2);
    uint32_t fs = 1000;
    serialization::writePod(file, fs);
    uint16_t sc = 5;
    serialization::writePod(file, sc);
    uint16_t tocCount = 3;
    serialization::writePod(file, tocCount);
    // Only write 1 item
    serialization::writeString(file, std::string("Chapter 1"));
    int16_t idx = 0;
    serialization::writePod(file, idx);

    file.seek(0);
    MetaCacheLut lut;
    bool ok = buildMetaCacheLut(file, lut);
    runner.expectFalse(ok, "lut_truncated: rejected");
    runner.expectEq(static_cast<uint16_t>(0), lut.tocItemCount, "lut_truncated: count reset to 0");
    runner.expectEq(static_cast<size_t>(0), lut.tocLut.size(), "lut_truncated: LUT cleared");
  }

  // Test 16: Language and coverContentType roundtrip
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "Russian Book", "Author", "cover.jpg", "ru", "image/jpeg", 75000, 10000, 1, {{"Ch1", 0}});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "lang_roundtrip: reads successfully");
    runner.expectEqual("ru", data.language, "lang_roundtrip: language preserved");
    runner.expectEqual("image/jpeg", data.coverContentType, "lang_roundtrip: coverContentType preserved");
    runner.expectEq(static_cast<int64_t>(75000), data.coverBinaryOffset, "lang_roundtrip: offset preserved");
  }

  // Test 17: Empty language and coverContentType
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "Book", "Author", "", "", "", -1, 5000, 0, {});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "empty_lang: reads successfully");
    runner.expectEqual("", data.language, "empty_lang: empty language");
    runner.expectEqual("", data.coverContentType, "empty_lang: empty coverContentType");
  }

  // Test 18: LUT with language and coverContentType
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "French Book", "Author", "img.png", "fr", "image/png", 6000, 8000, 2,
                   {{"Ch1", 0}, {"Ch2", 1}});

    file.seek(0);
    MetaCacheLut lut;
    bool ok = buildMetaCacheLut(file, lut);
    runner.expectTrue(ok, "lut_lang: builds LUT");
    runner.expectEqual("fr", lut.language, "lut_lang: language preserved");
    runner.expectEqual("image/png", lut.coverContentType, "lut_lang: coverContentType preserved");
    runner.expectEq(static_cast<int64_t>(6000), lut.coverBinaryOffset, "lut_lang: offset preserved");
    runner.expectEq(static_cast<uint16_t>(2), lut.tocItemCount, "lut_lang: 2 TOC items");
  }

  // Test 19: Section offsets round-trip
  {
    FsFile file;
    file.setBuffer("");

    std::vector<SectionOffset> offsets = {{100, 500}, {600, 2000}};
    writeMetaCache(file, "Book", "Author", "", "", "", -1, 5000, 2, {{"Ch1", 0}, {"Ch2", 1}}, offsets);

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "section_offsets_roundtrip: reads successfully");
    runner.expectEq(static_cast<size_t>(2), data.sectionOffsets.size(), "section_offsets_roundtrip: 2 offsets");
    runner.expectEq(static_cast<uint32_t>(100), data.sectionOffsets[0].startOffset, "section_offsets_roundtrip: [0] start");
    runner.expectEq(static_cast<uint32_t>(500), data.sectionOffsets[0].endOffset, "section_offsets_roundtrip: [0] end");
    runner.expectEq(static_cast<uint32_t>(600), data.sectionOffsets[1].startOffset, "section_offsets_roundtrip: [1] start");
    runner.expectEq(static_cast<uint32_t>(2000), data.sectionOffsets[1].endOffset, "section_offsets_roundtrip: [1] end");
  }

  // Test 20: Zero section offsets
  {
    FsFile file;
    file.setBuffer("");

    writeMetaCache(file, "Book", "Author", "", "", "", -1, 5000, 0, {});

    file.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file, data);
    runner.expectTrue(ok, "section_offsets_zero: reads successfully");
    runner.expectEq(static_cast<size_t>(0), data.sectionOffsets.size(), "section_offsets_zero: empty");
  }

  // Test 21: Truncated section offsets — succeeds but offsets cleared
  {
    FsFile file;
    file.setBuffer("");

    // Manually write a valid header + TOC + sectionOffsetCount=2 but only half of first offset
    writeMetaCache(file, "Book", "Author", "", "", "", -1, 5000, 0, {});
    // Overwrite: seek back and rewrite with truncated section offsets appended manually
    // Easier: write a fresh buffer from scratch
    FsFile file2;
    file2.setBuffer("");
    serialization::writePod(file2, kMetaCacheVersion);
    serialization::writeString(file2, std::string("Title"));
    serialization::writeString(file2, std::string("Author"));
    serialization::writeString(file2, std::string(""));
    serialization::writeString(file2, std::string(""));
    serialization::writeString(file2, std::string(""));
    int64_t binaryOffset = -1;
    serialization::writePod(file2, binaryOffset);
    uint32_t fs = 1000;
    serialization::writePod(file2, fs);
    uint16_t sc = 0;
    serialization::writePod(file2, sc);
    uint16_t tocCount = 0;
    serialization::writePod(file2, tocCount);
    // Write sectionOffsetCount=2 but only startOffset of first (no endOffset, no second)
    uint16_t sectionOffsetCount = 2;
    serialization::writePod(file2, sectionOffsetCount);
    uint32_t startOffset = 100;
    serialization::writePod(file2, startOffset);
    // endOffset missing — truncated here

    file2.seek(0);
    MetaCacheData data;
    bool ok = readMetaCache(file2, data);
    runner.expectTrue(ok, "section_offsets_truncated: still returns true (graceful)");
    runner.expectEq(static_cast<size_t>(0), data.sectionOffsets.size(), "section_offsets_truncated: offsets cleared");
  }

  return runner.allPassed() ? 0 : 1;
}
