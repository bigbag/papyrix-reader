#include "BookMetadataCache.h"

#include <Logging.h>
#include <Serialization.h>

#include <vector>

#define TAG "META_CACHE"

namespace {
constexpr uint8_t BOOK_CACHE_VERSION = 7;
constexpr char bookBinFile[] = "/book.bin";
constexpr char tmpSpineBinFile[] = "/spine.bin.tmp";
constexpr char tmpTocBinFile[] = "/toc.bin.tmp";
}  // namespace

/* ============= WRITING / BUILDING FUNCTIONS ================ */

bool BookMetadataCache::beginWrite() {
  buildMode = true;
  buildFailed = false;
  spineCount = 0;
  tocCount = 0;
  LOG_DBG(TAG, "Entering write mode");
  return true;
}

bool BookMetadataCache::beginContentOpfPass() {
  LOG_DBG(TAG, "Beginning content opf pass");

  // Open spine file for writing
  const bool success = SdMan.openFileForWrite("BMC", cachePath + tmpSpineBinFile, spineFile);
  if (!success) buildFailed = true;
  return success;
}

bool BookMetadataCache::endContentOpfPass() {
  const bool success = !buildFailed && spineFile.sync();
  spineFile.close();
  if (!success) buildFailed = true;
  return success;
}

bool BookMetadataCache::beginTocPass() {
  if (buildFailed) return false;
  LOG_DBG(TAG, "Beginning toc pass");

  // Open spine file for reading
  if (!SdMan.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    buildFailed = true;
    return false;
  }
  if (!SdMan.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    spineFile.close();
    buildFailed = true;
    return false;
  }

  return true;
}

bool BookMetadataCache::endTocPass() {
  const bool success = !buildFailed && tocFile.sync();
  tocFile.close();
  spineFile.close();
  if (!success) buildFailed = true;
  return success;
}

bool BookMetadataCache::skipTocPass() {
  LOG_INF(TAG, "Skipping TOC pass (heap too low)");
  if (!SdMan.openFileForWrite("BMC", cachePath + tmpTocBinFile, tocFile)) {
    buildFailed = true;
    return false;
  }
  const bool success = tocFile.sync();
  tocFile.close();
  if (!success) buildFailed = true;
  return success;
}

bool BookMetadataCache::endWrite() {
  if (!buildMode) {
    LOG_ERR(TAG, "endWrite called but not in build mode");
    return false;
  }

  buildMode = false;
  if (buildFailed) return false;
  LOG_INF(TAG, "Wrote %d spine, %d TOC entries", spineCount, tocCount);
  return true;
}

bool BookMetadataCache::buildBookBin(const std::string& epubPath, const BookMetadata& metadata) {
  (void)epubPath;
  const std::string tmpBookPath = cachePath + "/book.bin.new";
  if (!SdMan.openFileForWrite("BMC", tmpBookPath, bookFile)) return false;

  if (!SdMan.openFileForRead("BMC", cachePath + tmpSpineBinFile, spineFile)) {
    bookFile.close();
    SdMan.remove(tmpBookPath.c_str());
    return false;
  }
  if (!SdMan.openFileForRead("BMC", cachePath + tmpTocBinFile, tocFile)) {
    bookFile.close();
    spineFile.close();
    SdMan.remove(tmpBookPath.c_str());
    return false;
  }

  auto fail = [&]() {
    bookFile.close();
    spineFile.close();
    tocFile.close();
    SdMan.remove(tmpBookPath.c_str());
    return false;
  };

  constexpr uint32_t headerSize = sizeof(BOOK_CACHE_VERSION) + sizeof(uint32_t) + sizeof(spineCount) + sizeof(tocCount);
  const size_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                              metadata.coverItemHref.size() + metadata.textReferenceHref.size() + sizeof(uint32_t) * 5;
  const size_t lutSize = sizeof(uint32_t) * static_cast<size_t>(spineCount + tocCount);
  if (metadataSize > UINT32_MAX - headerSize || lutSize > UINT32_MAX - headerSize - metadataSize) return fail();
  const uint32_t lutOffset = static_cast<uint32_t>(headerSize + metadataSize);

  bool writeOk = serialization::writePodChecked(bookFile, BOOK_CACHE_VERSION) &&
                 serialization::writePodChecked(bookFile, lutOffset) &&
                 serialization::writePodChecked(bookFile, spineCount) &&
                 serialization::writePodChecked(bookFile, tocCount) &&
                 serialization::writeStringChecked(bookFile, metadata.title) &&
                 serialization::writeStringChecked(bookFile, metadata.author) &&
                 serialization::writeStringChecked(bookFile, metadata.language) &&
                 serialization::writeStringChecked(bookFile, metadata.coverItemHref) &&
                 serialization::writeStringChecked(bookFile, metadata.textReferenceHref);
  if (!writeOk || !spineFile.seek(0)) return fail();

  for (uint16_t i = 0; i < spineCount; i++) {
    const uint32_t pos = spineFile.position();
    if (readSpineEntry(spineFile).href.empty() ||
        !serialization::writePodChecked(bookFile, pos + lutOffset + static_cast<uint32_t>(lutSize))) {
      return fail();
    }
  }
  const uint32_t spineDataSize = spineFile.position();

  if (!tocFile.seek(0)) return fail();
  for (uint16_t i = 0; i < tocCount; i++) {
    const uint32_t pos = tocFile.position();
    if (readTocEntry(tocFile).href.empty() ||
        !serialization::writePodChecked(bookFile, pos + lutOffset + static_cast<uint32_t>(lutSize) + spineDataSize)) {
      return fail();
    }
  }

  if (!spineFile.seek(0)) return fail();
  if (tocCount > 0) {
    std::vector<int16_t> spineToTocIndex(spineCount, -1);
    if (!tocFile.seek(0)) return fail();
    for (uint16_t j = 0; j < tocCount; j++) {
      const auto tocEntry = readTocEntry(tocFile);
      if (tocEntry.href.empty()) return fail();
      if (tocEntry.spineIndex >= 0 && static_cast<uint16_t>(tocEntry.spineIndex) < spineCount &&
          spineToTocIndex[tocEntry.spineIndex] == -1) {
        spineToTocIndex[tocEntry.spineIndex] = static_cast<int16_t>(j);
      }
    }

    int16_t lastSpineTocIndex = -1;
    for (uint16_t i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href.empty()) return fail();
      spineEntry.tocIndex = spineToTocIndex[i];
      if (spineEntry.tocIndex == -1) spineEntry.tocIndex = lastSpineTocIndex;
      lastSpineTocIndex = spineEntry.tocIndex;
      if (!writeSpineEntry(bookFile, spineEntry)) return fail();
    }

    if (!tocFile.seek(0)) return fail();
    for (uint16_t i = 0; i < tocCount; i++) {
      const auto tocEntry = readTocEntry(tocFile);
      if (tocEntry.href.empty() || !writeTocEntry(bookFile, tocEntry)) return fail();
    }
  } else {
    for (uint16_t i = 0; i < spineCount; i++) {
      auto spineEntry = readSpineEntry(spineFile);
      if (spineEntry.href.empty()) return fail();
      spineEntry.tocIndex = -1;
      if (!writeSpineEntry(bookFile, spineEntry)) return fail();
    }
  }

  if (!bookFile.sync()) return fail();
  bookFile.close();
  spineFile.close();
  tocFile.close();

  const std::string finalPath = cachePath + bookBinFile;
  SdMan.remove(finalPath.c_str());
  if (!SdMan.rename(tmpBookPath.c_str(), finalPath.c_str())) {
    SdMan.remove(tmpBookPath.c_str());
    return false;
  }

  LOG_INF(TAG, "Successfully built book.bin");
  return true;
}

bool BookMetadataCache::rebuildFromMemory(const BookMetadata& metadata, const std::vector<SpineEntry>& spine,
                                          const std::vector<TocEntry>& toc) {
  if (spine.size() > 500 || toc.size() > 500) return false;
  const uint16_t newSpineCount = static_cast<uint16_t>(spine.size());
  const uint16_t newTocCount = static_cast<uint16_t>(toc.size());
  const std::string spineTmpPath = cachePath + tmpSpineBinFile;
  const std::string tocTmpPath = cachePath + tmpTocBinFile;
  const std::string tmpBookPath = cachePath + "/book.bin.new";

  auto cleanupTmps = [&]() {
    SdMan.remove(spineTmpPath.c_str());
    SdMan.remove(tocTmpPath.c_str());
    SdMan.remove(tmpBookPath.c_str());
  };

  {
    FsFile file;
    if (!SdMan.openFileForWrite("BMC", spineTmpPath, file)) return false;
    bool ok = true;
    for (const auto& entry : spine) ok = ok && writeSpineEntry(file, entry);
    ok = ok && file.sync();
    file.close();
    if (!ok) {
      cleanupTmps();
      return false;
    }
  }
  {
    FsFile file;
    if (!SdMan.openFileForWrite("BMC", tocTmpPath, file)) {
      cleanupTmps();
      return false;
    }
    bool ok = true;
    for (const auto& entry : toc) ok = ok && writeTocEntry(file, entry);
    ok = ok && file.sync();
    file.close();
    if (!ok) {
      cleanupTmps();
      return false;
    }
  }

  std::vector<int16_t> spineToTocIndex(newSpineCount, -1);
  for (uint16_t j = 0; j < newTocCount; j++) {
    if (toc[j].spineIndex >= 0 && static_cast<uint16_t>(toc[j].spineIndex) < newSpineCount &&
        spineToTocIndex[toc[j].spineIndex] == -1) {
      spineToTocIndex[toc[j].spineIndex] = static_cast<int16_t>(j);
    }
  }

  FsFile spineTmp;
  FsFile tocTmp;
  if (!SdMan.openFileForRead("BMC", spineTmpPath, spineTmp) || !SdMan.openFileForRead("BMC", tocTmpPath, tocTmp)) {
    spineTmp.close();
    tocTmp.close();
    cleanupTmps();
    return false;
  }

  constexpr uint32_t headerSize = sizeof(BOOK_CACHE_VERSION) + sizeof(uint32_t) + sizeof(uint16_t) * 2;
  const size_t metadataSize = metadata.title.size() + metadata.author.size() + metadata.language.size() +
                              metadata.coverItemHref.size() + metadata.textReferenceHref.size() + sizeof(uint32_t) * 5;
  const size_t lutSize = sizeof(uint32_t) * (spine.size() + toc.size());
  if (metadataSize > UINT32_MAX - headerSize || lutSize > UINT32_MAX - headerSize - metadataSize) {
    spineTmp.close();
    tocTmp.close();
    cleanupTmps();
    return false;
  }
  const uint32_t lutOffset = static_cast<uint32_t>(headerSize + metadataSize);

  FsFile output;
  if (!SdMan.openFileForWrite("BMC", tmpBookPath, output)) {
    spineTmp.close();
    tocTmp.close();
    cleanupTmps();
    return false;
  }
  auto fail = [&]() {
    output.close();
    spineTmp.close();
    tocTmp.close();
    cleanupTmps();
    return false;
  };

  bool ok =
      serialization::writePodChecked(output, BOOK_CACHE_VERSION) && serialization::writePodChecked(output, lutOffset) &&
      serialization::writePodChecked(output, newSpineCount) && serialization::writePodChecked(output, newTocCount) &&
      serialization::writeStringChecked(output, metadata.title) &&
      serialization::writeStringChecked(output, metadata.author) &&
      serialization::writeStringChecked(output, metadata.language) &&
      serialization::writeStringChecked(output, metadata.coverItemHref) &&
      serialization::writeStringChecked(output, metadata.textReferenceHref) && spineTmp.seek(0);
  if (!ok) return fail();

  for (uint16_t i = 0; i < newSpineCount; i++) {
    const uint32_t pos = spineTmp.position();
    if (readSpineEntry(spineTmp).href.empty() ||
        !serialization::writePodChecked(output, pos + lutOffset + static_cast<uint32_t>(lutSize))) {
      return fail();
    }
  }
  const uint32_t spineDataSize = spineTmp.position();

  if (!tocTmp.seek(0)) return fail();
  for (uint16_t i = 0; i < newTocCount; i++) {
    const uint32_t pos = tocTmp.position();
    if (readTocEntry(tocTmp).href.empty() ||
        !serialization::writePodChecked(output, pos + lutOffset + static_cast<uint32_t>(lutSize) + spineDataSize)) {
      return fail();
    }
  }

  if (!spineTmp.seek(0)) return fail();
  int16_t lastTocIndex = -1;
  for (uint16_t i = 0; i < newSpineCount; i++) {
    auto entry = readSpineEntry(spineTmp);
    if (entry.href.empty()) return fail();
    entry.tocIndex = spineToTocIndex[i];
    if (entry.tocIndex == -1) entry.tocIndex = lastTocIndex;
    lastTocIndex = entry.tocIndex;
    if (!writeSpineEntry(output, entry)) return fail();
  }
  for (const auto& entry : toc) {
    if (!writeTocEntry(output, entry)) return fail();
  }

  if (!output.sync()) return fail();
  output.close();
  spineTmp.close();
  tocTmp.close();
  SdMan.remove(spineTmpPath.c_str());
  SdMan.remove(tocTmpPath.c_str());

  const std::string finalPath = cachePath + bookBinFile;
  SdMan.remove(finalPath.c_str());
  if (!SdMan.rename(tmpBookPath.c_str(), finalPath.c_str())) {
    SdMan.remove(tmpBookPath.c_str());
    return false;
  }

  LOG_INF(TAG, "Rebuilt book.bin: %u spine, %u TOC entries", newSpineCount, newTocCount);
  return true;
}

bool BookMetadataCache::cleanupTmpFiles() const {
  const auto spinePath = cachePath + tmpSpineBinFile;
  if (SdMan.exists(spinePath.c_str())) {
    SdMan.remove(spinePath.c_str());
  }
  const auto tocPath = cachePath + tmpTocBinFile;
  if (SdMan.exists(tocPath.c_str())) {
    SdMan.remove(tocPath.c_str());
  }
  return true;
}

bool BookMetadataCache::writeSpineEntry(FsFile& file, const SpineEntry& entry) const {
  return serialization::writeStringChecked(file, entry.href) && serialization::writePodChecked(file, entry.tocIndex);
}

bool BookMetadataCache::writeTocEntry(FsFile& file, const TocEntry& entry) const {
  return serialization::writeStringChecked(file, entry.title) && serialization::writeStringChecked(file, entry.href) &&
         serialization::writeStringChecked(file, entry.anchor) && serialization::writePodChecked(file, entry.level) &&
         serialization::writePodChecked(file, entry.spineIndex);
}

// Note: for the LUT to be accurate, this **MUST** be called for all spine items before `addTocEntry` is ever called
// this is because in this function we're marking positions of the items
void BookMetadataCache::createSpineEntry(const std::string& href) {
  if (!buildMode || !spineFile) {
    LOG_ERR(TAG, "createSpineEntry called but not in build mode");
    return;
  }

  const SpineEntry entry(href, -1);
  if (!writeSpineEntry(spineFile, entry)) {
    buildFailed = true;
    return;
  }
  spineCount++;
}

void BookMetadataCache::createTocEntry(const std::string& title, const std::string& href, const std::string& anchor,
                                       const uint8_t level) {
  if (!buildMode || !tocFile) {
    LOG_ERR(TAG, "createTocEntry called but not in build mode");
    return;
  }

  int spineIndex = -1;
  if (!spineFile.seek(0)) {
    buildFailed = true;
    return;
  }
  for (int i = 0; i < spineCount; i++) {
    auto entry = readSpineEntry(spineFile);
    if (entry.href.empty()) {
      buildFailed = true;
      return;
    }
    if (entry.href == href) {
      spineIndex = i;
      break;
    }
  }

  if (spineIndex == -1) {
    LOG_DBG(TAG, "addTocEntry: Could not find spine item for TOC href %s", href.c_str());
  }

  const TocEntry entry(title, href, anchor, level, spineIndex);
  if (!writeTocEntry(tocFile, entry)) {
    buildFailed = true;
    return;
  }
  tocCount++;
}

/* ============= READING / LOADING FUNCTIONS ================ */

bool BookMetadataCache::load() {
  loaded = false;
  if (!SdMan.openFileForRead("BMC", cachePath + bookBinFile, bookFile)) {
    return false;
  }

  uint8_t fileVersion = 0;
  uint32_t fileLutOffset = 0;
  uint16_t fileSpineCount = 0;
  uint16_t fileTocCount = 0;
  if (!serialization::readPodChecked(bookFile, fileVersion) ||
      !serialization::readPodChecked(bookFile, fileLutOffset) ||
      !serialization::readPodChecked(bookFile, fileSpineCount) ||
      !serialization::readPodChecked(bookFile, fileTocCount)) {
    LOG_ERR(TAG, "Truncated cache header");
    bookFile.close();
    return false;
  }
  if (fileVersion != BOOK_CACHE_VERSION) {
    LOG_ERR(TAG, "Cache version mismatch: expected %d, got %d", BOOK_CACHE_VERSION, fileVersion);
    bookFile.close();
    return false;
  }

  constexpr uint16_t kMaxEntries = 500;
  if (fileSpineCount > kMaxEntries || fileTocCount > kMaxEntries) {
    LOG_ERR(TAG, "Cache entry count exceeds limit: spine=%u toc=%u", fileSpineCount, fileTocCount);
    bookFile.close();
    return false;
  }

  BookMetadata fileMetadata;
  if (!serialization::readString(bookFile, fileMetadata.title) ||
      !serialization::readString(bookFile, fileMetadata.author) ||
      !serialization::readString(bookFile, fileMetadata.language) ||
      !serialization::readString(bookFile, fileMetadata.coverItemHref) ||
      !serialization::readString(bookFile, fileMetadata.textReferenceHref)) {
    LOG_ERR(TAG, "Failed to read metadata strings");
    bookFile.close();
    return false;
  }

  const size_t fileSize = bookFile.size();
  const size_t lutEntryCount = static_cast<size_t>(fileSpineCount) + fileTocCount;
  const size_t lutSize = lutEntryCount * sizeof(uint32_t);
  if (fileLutOffset != bookFile.position() || fileLutOffset > fileSize || lutSize > fileSize - fileLutOffset) {
    LOG_ERR(TAG, "Invalid cache LUT span");
    bookFile.close();
    return false;
  }

  lutOffset = fileLutOffset;
  spineCount = fileSpineCount;
  tocCount = fileTocCount;
  dataOffset = static_cast<uint32_t>(fileLutOffset + lutSize);
  cacheFileSize = static_cast<uint32_t>(fileSize);
  coreMetadata = std::move(fileMetadata);
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

  uint32_t spineEntryPos = 0;
  const uint32_t lutEntryOffset = lutOffset + sizeof(uint32_t) * static_cast<uint32_t>(index);
  if (!readEntryPosition(lutEntryOffset, spineEntryPos)) return {};
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

  uint32_t tocEntryPos = 0;
  const uint32_t lutEntryOffset =
      lutOffset + sizeof(uint32_t) * (static_cast<uint32_t>(spineCount) + static_cast<uint32_t>(index));
  if (!readEntryPosition(lutEntryOffset, tocEntryPos)) return {};
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

bool BookMetadataCache::readEntryPosition(const uint32_t lutEntryOffset, uint32_t& entryPosition) {
  if (!bookFile.seek(lutEntryOffset) || !serialization::readPodChecked(bookFile, entryPosition) ||
      entryPosition < dataOffset || entryPosition >= cacheFileSize || !bookFile.seek(entryPosition)) {
    LOG_ERR(TAG, "Invalid cache entry position");
    return false;
  }
  return true;
}

bool BookMetadataCache::readTocEntries(std::vector<TocEntry>& entries, int maxCount) {
  entries.clear();
  if (!loaded || tocCount == 0) return false;

  const int count = (maxCount > 0 && maxCount < tocCount) ? maxCount : tocCount;
  entries.reserve(static_cast<size_t>(count));

  uint32_t firstTocPos = 0;
  if (!readEntryPosition(lutOffset + sizeof(uint32_t) * spineCount, firstTocPos)) return false;

  for (int i = 0; i < count; i++) {
    auto entry = readTocEntry(bookFile);
    if (entry.title.empty() && entry.href.empty()) {
      entries.clear();
      return false;
    }
    entries.push_back(std::move(entry));
  }

  return true;
}
