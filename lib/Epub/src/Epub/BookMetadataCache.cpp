#include "BookMetadataCache.h"

#include <Logging.h>
#include <Serialization.h>

#include <vector>

#define TAG "META_CACHE"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 6;
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  buildMode = true;
  spineCount = 0;
  tocCount = 0;
  LOG_DBG(TAG, "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG(TAG, "Beginning content opf pass");

  // Open spine file for writing
  return SdMan.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile);
}

bool BookMetadataCache::endContentOpfPass() {
  spineFile.close();
  return true;
}

bool BookMetadataCache::beginTocPass() {
  LOG_DBG(TAG, "Beginning toc pass");

  // Open spine file for reading
  if (!SdMan.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    return false;
  }
  if (!SdMan.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    spineFile.close();
    return false;
  }

  // Pre-load spine hrefs for O(1) lookup during TOC entry creation
  spineHrefIndex.clear();
  spineHrefIndex.reserve(spineCount);
  spineFile.seek(0);
  for (int i = 0; i < spineCount; i++) {
    auto entry = readSpineEntry(spineFile);
    spineHrefIndex[entry.href] = i;
  }
  LOG_DBG(TAG, "Cached %d spine hrefs for fast lookup", spineCount);

  return true;
}

bool BookMetadataCache::endTocPass() {
  tocFile.close();
  spineFile.close();

  // Free cached spine hrefs memory - swap idiom to release bucket memory
  std::unordered_map<std::string, int>().swap(spineHrefIndex);

  return true;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_ERR(TAG, "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  LOG_INF(TAG, "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  // Open all three files, writing to meta, reading from spine and toc
  if (!SdMan.openFileForWrite("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  if (!SdMan.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    bookFile.close();
    return false;
  }

  if (!SdMan.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    bookFile.close();
    spineFile.close();
    return false;
  }

  constexpr uint32_t headerASize =
      sizeof(BOOK_CACHE_VERSION) + /* LUT Offset */ sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const uint32_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                                metadata.coverItemHref.size() + metadata.textReferenceHref.size() +
                                sizeof(uint32_t) * 5;
  const uint32_t lutSize = sizeof(uint32_t) * spineCount + sizeof(uint32_t) * tocCount;
  const uint32_t lutOffset = headerASize + metadataSize;

  // Header A
  serialization::writePod(bookFile, BOOK_CACHE_VERSION);
  serialization::writePod(bookFile, lutOffset);
  serialization::writePod(bookFile, spineCount);
  serialization::writePod(bookFile, tocCount);
  // Metadata
  serialization::writeString(bookFile, metadata.title);
  serialization::writeString(bookFile, metadata.author);
  serialization::writeString(bookFile, metadata.language);
  serialization::writeString(bookFile, metadata.coverItemHref);
  serialization::writeString(bookFile, metadata.textReferenceHref);

  // Loop through spine entries, writing LUT positions
  spineFile.seek(0);
  for (int i = 0; i < spineCount; i++) {
    uint32_t pos = spineFile.position();
    auto spineEntry = readSpineEntry(spineFile);
    serialization::writePod(bookFile, pos + lutOffset + lutSize);
  }

  // Loop through toc entries, writing LUT positions
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    uint32_t pos = tocFile.position();
    auto tocEntry = readTocEntry(tocFile);
    serialization::writePod(bookFile, pos + lutOffset + lutSize + static_cast<uint32_t>(spineFile.position()));
  }

  // LUTs complete
  // Loop through spines from spine file matching up TOC indexes and writing to book.bin

  // Build spineIndex->tocIndex mapping in one pass (O(n) instead of O(n*m))
  std::vector<int16_t> spineToTocIndex(spineCount, -1);
  tocFile.seek(0);
  for (int j = 0; j < tocCount; j++) {
    auto tocEntry = readTocEntry(tocFile);
    if (tocEntry.spineIndex >= 0 && static_cast<uint16_t>(tocEntry.spineIndex) < spineCount) {
      if (spineToTocIndex[tocEntry.spineIndex] == -1) {
        spineToTocIndex[tocEntry.spineIndex] = static_cast<int16_t>(j);
      }
    }
  }

  // Write spine entries with TOC mapping (cumulativeSize is no longer used - progress is page-based)
  spineFile.seek(0);
  int lastSpineTocIndex = -1;
  for (uint16_t i = 0; i < spineCount; i++) {
    auto spineEntry = readSpineEntry(spineFile);

    // O(1) lookup using prebuilt mapping
    spineEntry.tocIndex = spineToTocIndex[i];

    // Not a huge deal if we don't find a TOC entry for the spine entry, this is expected behaviour for EPUBs
    if (spineEntry.tocIndex == -1) {
      LOG_DBG(TAG, "Warning: Could not find TOC entry for spine item %d: %s, using title from last section", i,
              spineEntry.href.c_str());
      spineEntry.tocIndex = lastSpineTocIndex;
    }
    lastSpineTocIndex = spineEntry.tocIndex;

    writeSpineEntry(bookFile, spineEntry);
  }

  // Loop through toc entries from toc file writing to book.bin
  tocFile.seek(0);
  for (int i = 0; i < tocCount; i++) {
    auto tocEntry = readTocEntry(tocFile);
    writeTocEntry(bookFile, tocEntry);
  }

  bookFile.close();
  spineFile.close();
  tocFile.close();

  LOG_INF(TAG, "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  if (SdMan.exists((cachePath + tmpSpineBinFile).c_str())) {
    SdMan.remove((cachePath + tmpSpineBinFile).c_str());
  }
  if (SdMan.exists((cachePath + tmpTocBinFile).c_str())) {
    SdMan.remove((cachePath + tmpTocBinFile).c_str());
  }
  return true;
}

uint32_t BookMetadataCache::writeSpineEntry(FsFile& file, const SpineEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.href);
  serialization::writePod(file, entry.tocIndex);
  return pos;
}

uint32_t BookMetadataCache::writeTocEntry(FsFile& file, const TocEntry& entry) const {
  const uint32_t pos = file.position();
  serialization::writeString(file, entry.title);
  serialization::writeString(file, entry.href);
  serialization::writeString(file, entry.anchor);
  serialization::writePod(file, entry.level);
  serialization::writePod(file, entry.spineIndex);
  return pos;
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_ERR(TAG, "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, -1);
  writeSpineEntry(spineFile, entry);
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile) {
    LOG_ERR(TAG, "createTocEntry called but not in build mode");
    return;
  }

  // O(1) lookup using cached spine href index
  int spineIndex = -1;
  auto it = spineHrefIndex.find(href);
  if (it != spineHrefIndex.end()) {
    spineIndex = it->second;
  }

  if (spineIndex == -1) {
    LOG_DBG(TAG, "addTocEntry: Could not find spine item for TOC href %s", href.c_str());
  }

  const TocEntry entry(title, href, anchor, level, spineIndex);
  writeTocEntry(tocFile, entry);
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  if (!SdMan.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(bookFile, version);
  if (version != BOOK_CACHE_VERSION) {
    LOG_ERR(TAG, "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, version);
    bookFile.close();
    return false;
  }

  serialization::readPod(bookFile, lutOffset);
  serialization::readPod(bookFile, spineCount);
  serialization::readPod(bookFile, tocCount);

  if (!serialization::readString(bookFile, coreMetadata.title) ||
      !serialization::readString(bookFile, coreMetadata.author) ||
      !serialization::readString(bookFile, coreMetadata.language) ||
      !serialization::readString(bookFile, coreMetadata.coverItemHref) ||
      !serialization::readString(bookFile, coreMetadata.textReferenceHref)) {
    LOG_ERR(TAG, "Failed to read metadata strings");
    return false;
  }

  loaded = true;
  LOG_INF(TAG, "Loaded cache data: %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

BookMetadataCache::SpineEntry BookMetadataCache::getSpineEntry(const int index) {
  if (!loaded) {
    LOG_ERR(TAG, "getSpineEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(spineCount)) {
    LOG_ERR(TAG, "getSpineEntry index %d out of range", index);
    return {};
  }

  // Seek to spine LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * index);
  uint32_t spineEntryPos;
  serialization::readPod(bookFile, spineEntryPos);
  bookFile.seek(spineEntryPos);
  return readSpineEntry(bookFile);
}

BookMetadataCache::TocEntry BookMetadataCache::getTocEntry(const int index) {
  if (!loaded) {
    LOG_ERR(TAG, "getTocEntry called but cache not loaded");
    return {};
  }

  if (index < 0 || index >= static_cast<int>(tocCount)) {
    LOG_ERR(TAG, "getTocEntry index %d out of range", index);
    return {};
  }

  // Seek to TOC LUT item, read from LUT and get out data
  bookFile.seek(lutOffset + sizeof(uint32_t) * spineCount + sizeof(uint32_t) * index);
  uint32_t tocEntryPos;
  serialization::readPod(bookFile, tocEntryPos);
  bookFile.seek(tocEntryPos);
  return readTocEntry(bookFile);
}

BookMetadataCache::SpineEntry BookMetadataCache::readSpineEntry(FsFile& file) const {
  SpineEntry entry;
  if (!serialization::readString(file, entry.href) || !serialization::readPodChecked(file, entry.tocIndex)) {
    return {};
  }
  return entry;
}

BookMetadataCache::TocEntry BookMetadataCache::readTocEntry(FsFile& file) const {
  TocEntry entry;
  if (!serialization::readString(file, entry.title) || !serialization::readString(file, entry.href) ||
      !serialization::readString(file, entry.anchor) || !serialization::readPodChecked(file, entry.level) ||
      !serialization::readPodChecked(file, entry.spineIndex)) {
    return {};
  }
  return entry;
}
