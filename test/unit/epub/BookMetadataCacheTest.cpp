// Tests for BookMetadataCache: bulk TOC read (readTocEntries), load, and
// spine/TOC entry retrieval. Mirrors the book.bin binary format to exercise
// the read paths without needing ZIP decompression.

#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "HardwareSerial.h"
#include "SDCardManager.h"
#include "SdFat.h"
#include "Serialization.h"

#include "BookMetadataCache.h"

namespace {

constexpr uint8_t BOOK_CACHE_VERSION = 7;

uint32_t readU32(const std::string& data, size_t offset) {
  uint32_t value = 0;
  memcpy(&value, data.data() + offset, sizeof(value));
  return value;
}

template <typename T>
void writeAt(std::string& data, size_t offset, const T& value) {
  memcpy(data.data() + offset, &value, sizeof(value));
}

struct TestBookData {
  BookMetadataCache::BookMetadata metadata;
  std::vector<BookMetadataCache::SpineEntry> spine;
  std::vector<BookMetadataCache::TocEntry> toc;
};

void writeSpineEntry(FsFile& file, const std::string& href, int16_t tocIndex) {
  serialization::writeString(file, href);
  serialization::writePod(file, tocIndex);
}

void writeTocEntry(FsFile& file, const std::string& title, const std::string& href, const std::string& anchor,
                   uint8_t level, int16_t spineIndex) {
  serialization::writeString(file, title);
  serialization::writeString(file, href);
  serialization::writeString(file, anchor);
  serialization::writePod(file, level);
  serialization::writePod(file, spineIndex);
}

uint32_t spineEntrySize(const std::string& href) {
  return static_cast<uint32_t>(sizeof(uint32_t) + href.size() + sizeof(int16_t));
}

uint32_t tocEntrySize(const std::string& title, const std::string& href, const std::string& anchor) {
  return static_cast<uint32_t>(sizeof(uint32_t) + title.size() + sizeof(uint32_t) + href.size() + sizeof(uint32_t) +
                               anchor.size() + sizeof(uint8_t) + sizeof(int16_t));
}

// Build a complete book.bin file in memory matching BookMetadataCache::buildBookBin format
std::string buildBookBin(const TestBookData& data) {
  const uint16_t spineCount = static_cast<uint16_t>(data.spine.size());
  const uint16_t tocCount = static_cast<uint16_t>(data.toc.size());

  // Calculate sizes
  constexpr uint32_t headerASize = sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
  const uint32_t metadataSize =
      static_cast<uint32_t>(data.metadata.title.size() + data.metadata.author.size() + data.metadata.language.size() +
                            data.metadata.coverItemHref.size() + data.metadata.textReferenceHref.size() +
                            sizeof(uint32_t) * 5);
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  // Calculate spine data size for TOC position offsets
  uint32_t spineDataSize = 0;
  for (const auto& s : data.spine) {
    spineDataSize += spineEntrySize(s.href);
  }

  FsFile file;
  file.setBuffer("");

  // Header
  serialization::writePod(file, BOOK_CACHE_VERSION);
  serialization::writePod(file, lutOffset);
  serialization::writePod(file, spineCount);
  serialization::writePod(file, tocCount);

  // Metadata
  serialization::writeString(file, data.metadata.title);
  serialization::writeString(file, data.metadata.author);
  serialization::writeString(file, data.metadata.language);
  serialization::writeString(file, data.metadata.coverItemHref);
  serialization::writeString(file, data.metadata.textReferenceHref);

  // Spine LUT
  uint32_t spinePos = 0;
  for (const auto& s : data.spine) {
    serialization::writePod(file, spinePos + lutOffset + lutSize);
    spinePos += spineEntrySize(s.href);
  }

  // TOC LUT
  uint32_t tocPos = 0;
  for (const auto& t : data.toc) {
    serialization::writePod(file, tocPos + lutOffset + lutSize + spineDataSize);
    tocPos += tocEntrySize(t.title, t.href, t.anchor);
  }

  // Spine entries
  for (const auto& s : data.spine) {
    writeSpineEntry(file, s.href, s.tocIndex);
  }

  // TOC entries
  for (const auto& t : data.toc) {
    writeTocEntry(file, t.title, t.href, t.anchor, t.level, t.spineIndex);
  }

  return file.getBuffer();
}

TestBookData makeTestBook() {
  TestBookData data;
  data.metadata.title = "Test Book";
  data.metadata.author = "Test Author";
  data.metadata.language = "en";

  data.spine.emplace_back("chapter1.xhtml", 0);
  data.spine.emplace_back("chapter2.xhtml", 1);
  data.spine.emplace_back("chapter3.xhtml", 2);

  data.toc.emplace_back("Introduction", "chapter1.xhtml", "", uint8_t(0), int16_t(0));
  data.toc.emplace_back("Main Content", "chapter2.xhtml", "", uint8_t(0), int16_t(1));
  data.toc.emplace_back("Section 2.1", "chapter2.xhtml", "s21", uint8_t(1), int16_t(1));
  data.toc.emplace_back("Conclusion", "chapter3.xhtml", "", uint8_t(0), int16_t(2));

  return data;
}

TestBookData makeLargeTestBook(int spineCount, int tocCount) {
  TestBookData data;
  data.metadata.title = "Large Book";
  data.metadata.author = "Author";
  data.metadata.language = "en";

  for (int i = 0; i < spineCount; i++) {
    data.spine.emplace_back("ch" + std::to_string(i) + ".xhtml", static_cast<int16_t>(i < tocCount ? i : -1));
  }

  for (int i = 0; i < tocCount; i++) {
    data.toc.emplace_back("Chapter " + std::to_string(i), "ch" + std::to_string(i) + ".xhtml", "",
                          static_cast<uint8_t>(i % 3), static_cast<int16_t>(i));
  }

  return data;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("BookMetadataCache");

  // ======================== load() ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    auto bin = buildBookBin(data);
    SdMan.registerFile("/cache/book.bin", bin);

    BookMetadataCache cache("/cache");
    runner.expectTrue(cache.load(), "Load_Success");
    runner.expectEq(3, cache.getSpineCount(), "Load_SpineCount");
    runner.expectEq(4, cache.getTocCount(), "Load_TocCount");
  }

  // ======================== getSpineEntry() ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    auto e0 = cache.getSpineEntry(0);
    runner.expectEqual("chapter1.xhtml", e0.href, "SpineEntry_0_Href");
    runner.expectEq(int16_t(0), e0.tocIndex, "SpineEntry_0_TocIndex");

    auto e2 = cache.getSpineEntry(2);
    runner.expectEqual("chapter3.xhtml", e2.href, "SpineEntry_2_Href");
  }

  // ======================== getTocEntry() ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    auto t0 = cache.getTocEntry(0);
    runner.expectEqual("Introduction", t0.title, "TocEntry_0_Title");
    runner.expectEq(int16_t(0), t0.spineIndex, "TocEntry_0_SpineIndex");
    runner.expectEq(uint8_t(0), t0.level, "TocEntry_0_Level");

    auto t2 = cache.getTocEntry(2);
    runner.expectEqual("Section 2.1", t2.title, "TocEntry_2_Title");
    runner.expectEqual("s21", t2.anchor, "TocEntry_2_Anchor");
    runner.expectEq(uint8_t(1), t2.level, "TocEntry_2_Level");
    runner.expectEq(int16_t(1), t2.spineIndex, "TocEntry_2_SpineIndex");
  }

  // ======================== readTocEntries() — bulk read ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    std::vector<BookMetadataCache::TocEntry> entries;
    runner.expectTrue(cache.readTocEntries(entries, 256), "BulkToc_Success");
    runner.expectEq(size_t(4), entries.size(), "BulkToc_Count");

    runner.expectEqual("Introduction", entries[0].title, "BulkToc_0_Title");
    runner.expectEq(int16_t(0), entries[0].spineIndex, "BulkToc_0_SpineIndex");

    runner.expectEqual("Main Content", entries[1].title, "BulkToc_1_Title");
    runner.expectEq(int16_t(1), entries[1].spineIndex, "BulkToc_1_SpineIndex");

    runner.expectEqual("Section 2.1", entries[2].title, "BulkToc_2_Title");
    runner.expectEqual("s21", entries[2].anchor, "BulkToc_2_Anchor");
    runner.expectEq(uint8_t(1), entries[2].level, "BulkToc_2_Level");

    runner.expectEqual("Conclusion", entries[3].title, "BulkToc_3_Title");
    runner.expectEq(int16_t(2), entries[3].spineIndex, "BulkToc_3_SpineIndex");
  }

  // ======================== readTocEntries() — maxCount limit ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    std::vector<BookMetadataCache::TocEntry> entries;
    runner.expectTrue(cache.readTocEntries(entries, 2), "BulkToc_MaxCount_Success");
    runner.expectEq(size_t(2), entries.size(), "BulkToc_MaxCount_Count");
    runner.expectEqual("Introduction", entries[0].title, "BulkToc_MaxCount_0");
    runner.expectEqual("Main Content", entries[1].title, "BulkToc_MaxCount_1");
  }

  // ======================== readTocEntries() — large book ========================

  {
    SdMan.reset();
    auto data = makeLargeTestBook(300, 256);
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    std::vector<BookMetadataCache::TocEntry> entries;
    runner.expectTrue(cache.readTocEntries(entries, 256), "BulkToc_Large_Success");
    runner.expectEq(size_t(256), entries.size(), "BulkToc_Large_Count");

    runner.expectEqual("Chapter 0", entries[0].title, "BulkToc_Large_First");
    runner.expectEqual("Chapter 255", entries[255].title, "BulkToc_Large_Last");
    runner.expectEq(int16_t(255), entries[255].spineIndex, "BulkToc_Large_LastSpine");
  }

  // ======================== readTocEntries() — empty TOC ========================

  {
    SdMan.reset();
    TestBookData data;
    data.metadata.title = "No TOC";
    data.metadata.author = "Author";
    data.metadata.language = "en";
    data.spine.emplace_back("ch1.xhtml", int16_t(-1));
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    std::vector<BookMetadataCache::TocEntry> entries;
    runner.expectFalse(cache.readTocEntries(entries, 256), "BulkToc_Empty_ReturnsFalse");
    runner.expectEq(size_t(0), entries.size(), "BulkToc_Empty_Count");
  }

  // ======================== readTocEntries() matches individual getTocEntry() ========================

  {
    SdMan.reset();
    auto data = makeLargeTestBook(50, 30);
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    std::vector<BookMetadataCache::TocEntry> bulk;
    cache.readTocEntries(bulk, 30);

    bool allMatch = true;
    for (int i = 0; i < 30; i++) {
      auto single = cache.getTocEntry(i);
      if (bulk[i].title != single.title || bulk[i].spineIndex != single.spineIndex ||
          bulk[i].level != single.level || bulk[i].anchor != single.anchor) {
        allMatch = false;
        break;
      }
    }
    runner.expectTrue(allMatch, "BulkToc_MatchesIndividual");
  }

  // ======================== metadata ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    SdMan.registerFile("/cache/book.bin", buildBookBin(data));

    BookMetadataCache cache("/cache");
    cache.load();

    runner.expectEqual("Test Book", cache.coreMetadata.title, "Metadata_Title");
    runner.expectEqual("Test Author", cache.coreMetadata.author, "Metadata_Author");
    runner.expectEqual("en", cache.coreMetadata.language, "Metadata_Language");
  }

  // ======================== malformed files ========================

  {
    SdMan.reset();
    auto data = makeTestBook();
    auto bin = buildBookBin(data);
    bin[0] = 99;  // corrupt version byte
    SdMan.registerFile("/cache/book.bin", bin);

    BookMetadataCache cache("/cache");
    runner.expectFalse(cache.load(), "Load_VersionMismatch");
  }

  for (size_t length = 0; length < 9; length++) {
    SdMan.reset();
    SdMan.registerFile("/cache/book.bin", std::string(length, '\0'));
    BookMetadataCache cache("/cache");
    runner.expectFalse(cache.load(), "Load_TruncatedHeader_" + std::to_string(length));
  }

  {
    SdMan.reset();
    auto bin = buildBookBin(makeTestBook());
    const uint32_t invalidOffset = static_cast<uint32_t>(bin.size() + 1);
    writeAt(bin, 1, invalidOffset);
    SdMan.registerFile("/cache/book.bin", bin);
    BookMetadataCache cache("/cache");
    runner.expectFalse(cache.load(), "Load_LutOffsetOutOfRange");
  }

  {
    SdMan.reset();
    TestBookData data;
    auto bin = buildBookBin(data);
    const uint16_t oneSpine = 1;
    writeAt(bin, 5, oneSpine);
    SdMan.registerFile("/cache/book.bin", bin);
    BookMetadataCache cache("/cache");
    runner.expectFalse(cache.load(), "Load_TruncatedLut");
  }

  {
    SdMan.reset();
    auto bin = buildBookBin(makeTestBook());
    const uint32_t lutOffset = readU32(bin, 1);
    writeAt(bin, lutOffset, lutOffset);
    SdMan.registerFile("/cache/book.bin", bin);
    BookMetadataCache cache("/cache");
    runner.expectTrue(cache.load(), "SpinePositionBeforeData_LoadsHeader");
    runner.expectTrue(cache.getSpineEntry(0).href.empty(), "SpinePositionBeforeData_Rejected");
  }

  {
    SdMan.reset();
    auto data = makeTestBook();
    auto bin = buildBookBin(data);
    const uint32_t lutOffset = readU32(bin, 1);
    const size_t firstTocLut = lutOffset + data.spine.size() * sizeof(uint32_t);
    const uint32_t invalidPosition = static_cast<uint32_t>(bin.size());
    writeAt(bin, firstTocLut, invalidPosition);
    SdMan.registerFile("/cache/book.bin", bin);
    BookMetadataCache cache("/cache");
    runner.expectTrue(cache.load(), "TocPositionOutOfRange_LoadsHeader");
    runner.expectTrue(cache.getTocEntry(0).title.empty(), "TocPositionOutOfRange_Rejected");
  }

  {
    SdMan.reset();
    auto data = makeTestBook();
    auto bin = buildBookBin(data);
    const uint32_t lutOffset = readU32(bin, 1);
    const size_t firstTocLut = lutOffset + data.spine.size() * sizeof(uint32_t);
    const uint32_t firstTocPosition = readU32(bin, firstTocLut);
    bin.resize(firstTocPosition + 2);
    SdMan.registerFile("/cache/book.bin", bin);
    BookMetadataCache cache("/cache");
    runner.expectTrue(cache.load(), "TruncatedTocEntry_LoadsHeader");
    std::vector<BookMetadataCache::TocEntry> entries;
    runner.expectFalse(cache.readTocEntries(entries, 4), "TruncatedTocEntry_Rejected");
    runner.expectEq<size_t>(0, entries.size(), "TruncatedTocEntry_ClearsOutput");
  }

  // ======================== checked writes ========================

  {
    SdMan.reset();
    const auto data = makeTestBook();
    BookMetadataCache cache("/cache");
    runner.expectTrue(cache.rebuildFromMemory(data.metadata, data.spine, data.toc), "Rebuild_Success");
    runner.expectTrue(SdMan.exists("/cache/book.bin"), "Rebuild_PublishesFinalFile");
    runner.expectFalse(SdMan.exists("/cache/book.bin.new"), "Rebuild_RemovesTemporaryFile");
    BookMetadataCache loaded("/cache");
    runner.expectTrue(loaded.load(), "Rebuild_ResultLoads");
  }

  {
    SdMan.reset();
    SdMan.setWriteLimit(1);
    const auto data = makeTestBook();
    BookMetadataCache cache("/cache");
    runner.expectFalse(cache.rebuildFromMemory(data.metadata, data.spine, data.toc),
                       "Rebuild_ShortWriteRejected");
    runner.expectFalse(SdMan.exists("/cache/book.bin"), "Rebuild_ShortWriteNoFinalFile");
    runner.expectFalse(SdMan.exists("/cache/book.bin.new"), "Rebuild_ShortWriteRemovesTemporaryFile");
  }

  {
    SdMan.reset();
    SdMan.setSyncResult(false);
    const auto data = makeTestBook();
    BookMetadataCache cache("/cache");
    runner.expectFalse(cache.rebuildFromMemory(data.metadata, data.spine, data.toc),
                       "Rebuild_SyncFailureRejected");
    runner.expectFalse(SdMan.exists("/cache/book.bin"), "Rebuild_SyncFailureNoFinalFile");
    runner.expectFalse(SdMan.exists("/cache/book.bin.new"), "Rebuild_SyncFailureRemovesTemporaryFile");
  }

  {
    SdMan.reset();
    const auto data = makeTestBook();
    BookMetadataCache cache("/cache");
    runner.expectTrue(cache.beginWrite(), "Build_Begin");
    runner.expectTrue(cache.beginContentOpfPass(), "Build_BeginSpine");
    for (const auto& entry : data.spine) cache.createSpineEntry(entry.href);
    runner.expectTrue(cache.endContentOpfPass(), "Build_EndSpine");
    runner.expectTrue(cache.beginTocPass(), "Build_BeginToc");
    for (const auto& entry : data.toc) {
      cache.createTocEntry(entry.title, entry.href, entry.anchor, entry.level);
    }
    runner.expectTrue(cache.endTocPass(), "Build_EndToc");
    runner.expectTrue(cache.endWrite(), "Build_End");
    runner.expectTrue(cache.buildBookBin("/book.epub", data.metadata), "Build_CheckedOutputSuccess");
    runner.expectTrue(SdMan.exists("/cache/book.bin"), "Build_PublishesFinalFile");
  }

  return runner.allPassed() ? 0 : 1;
}
