#include "Epub.h"

#include <CoverHelpers.h>
#include <FsHelpers.h>
#include <Html5Normalizer.h>
#include <ImageConverter.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <ZipFile.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>

#define TAG "EPUB"

#include <new>

#include "../../src/config.h"
#include "Epub/parsers/ContainerParser.h"
#include "Epub/parsers/ContentOpfParser.h"
#include "Epub/parsers/CoverLinkParser.h"
#include "Epub/parsers/TocNavParser.h"
#include "Epub/parsers/TocNcxParser.h"

namespace {
constexpr size_t MIN_HEAP_FOR_TOC = 20 * 1024;
constexpr int MAX_TOC_RECOVERY_ATTEMPTS = 3;
constexpr uint8_t TOC_RETRY_NO_TOC = 0xFF;
constexpr char tocRetryFile[] = "/toc.retry";

// Retry state values: attempts reserved, TOC_RETRY_ABSENT when no state file
// exists, TOC_RETRY_READ_ERROR when an existing file is unreadable.
constexpr int TOC_RETRY_ABSENT = -1;
constexpr int TOC_RETRY_READ_ERROR = -2;

int readTocRetryState(const std::string& dir) {
  const std::string path = dir + tocRetryFile;
  if (!SdMan.exists(path.c_str())) return TOC_RETRY_ABSENT;
  FsFile file;
  if (!SdMan.openFileForRead("EPUB", path, file)) return TOC_RETRY_READ_ERROR;
  uint8_t value = 0;
  const bool read = file.read(&value, 1) == 1;
  file.close();
  return read ? value : TOC_RETRY_READ_ERROR;
}

bool writeTocRetryState(const std::string& dir, const uint8_t state) {
  FsFile file;
  if (!SdMan.openFileForWrite("EPUB", dir + tocRetryFile, file)) return false;
  const bool ok = file.write(&state, 1) == 1 && file.sync();
  file.close();
  return ok;
}

constexpr size_t MAX_COVER_IMAGE_SIZE = 512 * 1024;
constexpr size_t MAX_COVER_WRAPPER_SIZE = 64 * 1024;

std::string coverHref(const std::string& document, const std::string& reference) {
  if (reference.size() > 512 || reference.find(':') != std::string::npos || reference.compare(0, 2, "//") == 0) {
    return {};
  }
  std::string path = reference.substr(0, reference.find_first_of("?#"));
  if (path.empty()) return reference.empty() ? path : document;
  if (path.front() == '/') {
    path.erase(0, 1);
  } else {
    path.insert(0, document.substr(0, document.find_last_of('/') + 1));
  }
  for (size_t pos = path.find("/./"); pos != std::string::npos; pos = path.find("/./")) path.erase(pos, 2);
  while (path.compare(0, 2, "./") == 0) path.erase(0, 2);
  return FsHelpers::normalisePath(path);
}

void migrateCoverFailures(const std::string& cachePath) {
  const std::string versionPath = cachePath + "/.cover-discovery.v1";
  if (SdMan.exists(versionPath.c_str())) return;
  for (const char* name : {"/.cover.failed", "/.thumb.failed"}) {
    const std::string path = cachePath + name;
    if (SdMan.exists(path.c_str()) && !SdMan.remove(path.c_str())) return;
  }
  FsFile version;
  if (SdMan.openFileForWrite("EPUB", versionPath, version)) {
    const bool synced = version.sync();
    version.close();
    if (!synced) SdMan.remove(versionPath.c_str());
  }
}

uint32_t coverImageArea(const std::string& path, const std::function<bool()>& shouldAbort) {
  FsFile file;
  if (!SdMan.openFileForRead("EPUB", path, file)) return 0;
  uint8_t header[26];
  if (file.read(header, sizeof(header)) != sizeof(header)) return 0;
  const auto be32 = [](const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  };
  const auto le32 = [](const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  };
  uint32_t width = 0, height = 0;
  if (memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(header + 12, "IHDR", 4) == 0) {
    width = be32(header + 16);
    height = be32(header + 20);
  } else if (header[0] == 'B' && header[1] == 'M' && le32(header + 14) >= 40) {
    width = le32(header + 18);
    const uint32_t rawHeight = le32(header + 22);
    height = (rawHeight & 0x80000000U) ? 0U - rawHeight : rawHeight;
  } else if (header[0] == 0xFF && header[1] == 0xD8 && file.seek(2)) {
    while (!CoverHelpers::isAbortRequested(shouldAbort)) {
      if (file.read() != 0xFF) return 0;
      int marker;
      do {
        if (CoverHelpers::isAbortRequested(shouldAbort)) return 0;
        marker = file.read();
      } while (marker == 0xFF);
      if (marker < 0 || marker == 0xDA || marker == 0xD9) return 0;
      uint8_t length[2];
      if (file.read(length, 2) != 2) return 0;
      const uint16_t size = (uint16_t(length[0]) << 8) | length[1];
      if (size < 2 || size - 2U > file.size() - file.position()) return 0;
      if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
        uint8_t frame[5];
        if (size < 7 || file.read(frame, 5) != 5 || frame[0] != 8) return 0;
        height = (uint16_t(frame[1]) << 8) | frame[2];
        width = (uint16_t(frame[3]) << 8) | frame[4];
        break;
      }
      if (!file.seek(file.position() + size - 2)) return 0;
    }
  }
  return width >= 20 && width <= 2048 && height >= 20 && height <= 3072 ? width * height : 0;
}
}  // namespace

bool Epub::findContentOpfFile(std::string* contentOpfFile, uint8_t* decompressBuffer, BuildArena* scratch,
                              const std::function<bool()>& shouldAbort) const {
  const auto containerPath = "META-INF/container.xml";
  size_t containerSize;

  // Get file size without loading it all into heap
  if (!getItemSize(containerPath, &containerSize)) {
    LOG_ERR(TAG, "Could not find or size META-INF/container.xml");
    return false;
  }

  ContainerParser containerParser(containerSize);

  if (!containerParser.setup()) {
    return false;
  }

  // Stream read (reusing your existing stream logic)
  if (!readItemContentsToStream(containerPath, containerParser, 512, decompressBuffer, scratch, shouldAbort)) {
    LOG_ERR(TAG, "Could not read META-INF/container.xml");
    return false;
  }

  // Extract the result
  if (containerParser.fullPath.empty()) {
    LOG_ERR(TAG, "Could not find valid rootfile in container.xml");
    return false;
  }

  *contentOpfFile = std::move(containerParser.fullPath);
  return true;
}

bool Epub::parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, const bool metadataOnly,
                           uint8_t* decompressBuffer, BuildArena* scratch, const std::function<bool()>& shouldAbort) {
  std::string contentOpfFilePath;
  if (!findContentOpfFile(&contentOpfFilePath, decompressBuffer, scratch, shouldAbort)) {
    LOG_ERR(TAG, "Could not find content.opf in zip");
    return false;
  }

  contentBasePath = contentOpfFilePath.substr(0, contentOpfFilePath.find_last_of('/') + 1);

  LOG_INF(TAG, "Parsing content.opf: %s", contentOpfFilePath.c_str());

  size_t contentOpfSize;
  if (!getItemSize(contentOpfFilePath, &contentOpfSize)) {
    LOG_ERR(TAG, "Could not get size of content.opf");
    return false;
  }

  ContentOpfParser opfParser(getCachePath(), getBasePath(), contentOpfSize,
                             metadataOnly ? nullptr : bookMetadataCache.get());
  if (!opfParser.setup()) {
    LOG_ERR(TAG, "Could not setup content.opf parser");
    return false;
  }

  if (!readItemContentsToStream(contentOpfFilePath, opfParser, 1024, decompressBuffer, scratch, shouldAbort)) {
    LOG_ERR(TAG, "Could not read content.opf");
    return false;
  }

  // Grab data from opfParser into epub
  bookMetadata.title = opfParser.title;
  bookMetadata.author = opfParser.author;
  bookMetadata.language = opfParser.language;
  bookMetadata.coverItemHref = opfParser.coverItemHref;
  bookMetadata.textReferenceHref = opfParser.textReferenceHref;

  if (!opfParser.tocNcxPath.empty()) {
    tocNcxItem = opfParser.tocNcxPath;
  }

  if (!opfParser.tocNavPath.empty()) {
    tocNavItem = opfParser.tocNavPath;
  }

  // Capture CSS files from manifest
  cssFiles_ = opfParser.getCssFiles();
  cssFilesKnown_ = true;
  LOG_DBG(TAG, "Found %d CSS files in manifest", static_cast<int>(cssFiles_.size()));

  LOG_INF(TAG, "Successfully parsed content.opf");
  return true;
}

bool Epub::parseTocNcxFile() const {
  // the ncx file should have been specified in the content.opf file
  if (tocNcxItem.empty()) {
    LOG_DBG(TAG, "No ncx file specified");
    return false;
  }

  LOG_INF(TAG, "Parsing toc ncx file: %s", tocNcxItem.c_str());

  const auto tmpNcxPath = getCachePath() + "/toc.ncx";
  FsFile tempNcxFile;
  if (!SdMan.openFileForWrite("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  if (!readItemContentsToStream(tocNcxItem, tempNcxFile, 1024)) {
    tempNcxFile.close();
    return false;
  }
  tempNcxFile.close();
  if (!SdMan.openFileForRead("EBP", tmpNcxPath, tempNcxFile)) {
    return false;
  }
  const auto ncxSize = tempNcxFile.size();

  TocNcxParser ncxParser(contentBasePath, ncxSize, bookMetadataCache.get());

  if (!ncxParser.setup()) {
    LOG_ERR(TAG, "Could not setup toc ncx parser");
    tempNcxFile.close();
    return false;
  }

  const auto ncxBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!ncxBuffer) {
    LOG_ERR(TAG, "Could not allocate memory for toc ncx parser");
    tempNcxFile.close();
    return false;
  }

  while (tempNcxFile.available()) {
    const auto readSize = tempNcxFile.read(ncxBuffer, 1024);
    const auto processedSize = ncxParser.write(ncxBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR(TAG, "Could not process all toc ncx data");
      free(ncxBuffer);
      tempNcxFile.close();
      return false;
    }
  }

  free(ncxBuffer);
  tempNcxFile.close();
  SdMan.remove(tmpNcxPath.c_str());

  LOG_INF(TAG, "Parsed TOC items");
  return true;
}

bool Epub::parseTocNavFile() const {
  // the nav file should have been specified in the content.opf file (EPUB 3)
  if (tocNavItem.empty()) {
    LOG_DBG(TAG, "No nav file specified");
    return false;
  }

  LOG_INF(TAG, "Parsing toc nav file: %s", tocNavItem.c_str());

  const auto tmpNavPath = getCachePath() + "/toc.nav";
  FsFile tempNavFile;
  if (!SdMan.openFileForWrite("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  if (!readItemContentsToStream(tocNavItem, tempNavFile, 1024)) {
    tempNavFile.close();
    return false;
  }
  tempNavFile.close();
  if (!SdMan.openFileForRead("EBP", tmpNavPath, tempNavFile)) {
    return false;
  }
  const auto navSize = tempNavFile.size();

  // Note: We can't use `contentBasePath` here as the nav file may be in a different folder to the content.opf
  // and the HTMLX nav file will have hrefs relative to itself
  const std::string navContentBasePath = tocNavItem.substr(0, tocNavItem.find_last_of('/') + 1);
  TocNavParser navParser(navContentBasePath, navSize, bookMetadataCache.get());

  if (!navParser.setup()) {
    LOG_ERR(TAG, "Could not setup toc nav parser");
    tempNavFile.close();
    return false;
  }

  const auto navBuffer = static_cast<uint8_t*>(malloc(1024));
  if (!navBuffer) {
    LOG_ERR(TAG, "Could not allocate memory for toc nav parser");
    tempNavFile.close();
    return false;
  }

  while (tempNavFile.available()) {
    const auto readSize = tempNavFile.read(navBuffer, 1024);
    const auto processedSize = navParser.write(navBuffer, readSize);

    if (processedSize != readSize) {
      LOG_ERR(TAG, "Could not process all toc nav data");
      free(navBuffer);
      tempNavFile.close();
      return false;
    }
  }

  free(navBuffer);
  tempNavFile.close();
  SdMan.remove(tmpNavPath.c_str());

  LOG_INF(TAG, "Parsed TOC nav items");
  return true;
}

// Parse the declared TOC sources and keep the valid candidate that covers
// more distinct spine items. The nav wins ties and a nav that covers every
// spine item skips the NCX entirely. Only completely parsed candidates
// compete; partial rows from a failed parse are discarded. Each candidate
// pass is closed before any candidate file is renamed. The caller has opened
// the TOC pass; this method closes it and returns false only when the pass
// could not be written.
bool Epub::parseTocCandidates() {
  bool passOpen = true;
  const auto endPass = [&]() {
    if (!passOpen) return true;
    passOpen = false;
    return bookMetadataCache->endTocPass();
  };
  const auto beginPass = [&]() {
    if (!bookMetadataCache->beginTocPass()) return false;
    passOpen = true;
    return true;
  };

  bool navValid = false;
  uint16_t navCount = 0;
  int navCoverage = 0;
  if (!tocNavItem.empty()) {
    LOG_DBG(TAG, "Attempting to parse EPUB 3 nav document");
    navValid = parseTocNavFile();
    if (navValid) {
      navCount = static_cast<uint16_t>(bookMetadataCache->getTocCount());
      navCoverage = bookMetadataCache->getUsableTocCount();
    } else if (bookMetadataCache->getTocCount() > 0) {
      if (!endPass() || !beginPass()) return false;
    }
  }

  const int spineCount = bookMetadataCache->getSpineCount();
  if (tocNcxItem.empty() || (navValid && spineCount > 0 && navCoverage >= spineCount)) {
    return endPass();
  }

  if (!endPass()) return false;
  if (navCount > 0 && !bookMetadataCache->stashTocCandidate()) return false;
  if (!beginPass()) return false;
  LOG_DBG(TAG, "Comparing NCX TOC with the nav document");
  const bool ncxValid = parseTocNcxFile();
  const int ncxCoverage = bookMetadataCache->getUsableTocCount();
  if (!ncxValid && bookMetadataCache->getTocCount() > 0) {
    if (!endPass() || !beginPass()) return false;
  }
  if (!endPass()) return false;
  if (navCount == 0 || (ncxValid && ncxCoverage > navCoverage)) {
    bookMetadataCache->discardStashedTocCandidate();
    return true;
  }
  return bookMetadataCache->restoreStashedTocCandidate(navCount, static_cast<uint16_t>(navCoverage));
}

void Epub::maybeRecoverLostToc() {
  if (bookMetadataCache->getTocCount() > 0) return;
  const int state = readTocRetryState(cachePath);
  // An unreadable state file must not authorize recovery.
  if (state == TOC_RETRY_READ_ERROR) return;
  if (state != TOC_RETRY_ABSENT && (state == TOC_RETRY_NO_TOC || state >= MAX_TOC_RECOVERY_ATTEMPTS)) return;
  // A low-heap open defers the recovery without spending an attempt.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < MIN_HEAP_FOR_TOC) return;

  // Reserve the attempt before touching the book so a crash or a failed state
  // write cannot repeat an unrecorded attempt.
  const uint8_t reserved = static_cast<uint8_t>(state == TOC_RETRY_ABSENT ? 1 : state + 1);
  if (!writeTocRetryState(cachePath, reserved)) return;

  LOG_INF(TAG, "Cache has no TOC entries, recovery attempt %d", reserved);
  recoverLostToc();
}

// Rebuild only the TOC of a loaded cache. The spine temp is seeded from the
// cache itself, so metadata, spine order and split mappings are preserved and
// no full-spine vector is held in heap.
void Epub::recoverLostToc() {
  const BookMetadataCache::BookMetadata metadata = bookMetadataCache->coreMetadata;

  BookMetadataCache::BookMetadata opfMetadata;
  tocNcxItem.clear();
  tocNavItem.clear();
  if (!parseContentOpf(opfMetadata, true)) return;
  if (tocNavItem.empty() && tocNcxItem.empty()) {
    // The book declares no TOC; settle instead of retrying.
    writeTocRetryState(cachePath, TOC_RETRY_NO_TOC);
    return;
  }

  bool committed = false;
  if (bookMetadataCache->beginTocRecovery() && parseTocCandidates() && bookMetadataCache->getUsableTocCount() > 0) {
    committed = bookMetadataCache->buildBookBin(filepath, metadata);
  }
  bookMetadataCache->cleanupTmpFiles();

  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  if (!bookMetadataCache->load()) return;
  if (committed && bookMetadataCache->getTocCount() > 0) {
    SdMan.remove((cachePath + tocRetryFile).c_str());
    LOG_INF(TAG, "Recovered TOC with %d entries", bookMetadataCache->getTocCount());
  }
}

bool Epub::prepareCssStyles(uint32_t sourceFingerprint, uint8_t* decompressBuffer, BuildArena* scratch,
                            const std::function<bool()>& shouldAbort) {
  bool aborted = false;
  const std::function<bool()> checkAbort = [&]() {
    aborted = aborted || (shouldAbort && shouldAbort());
    return aborted;
  };
  if (checkAbort()) return false;
  if (cssParser_ && cssFingerprint_ == sourceFingerprint) return true;
  cssParser_.reset();
  const size_t setupBytes = sizeof(CssParser) + cachePath.size() * 2 + 512;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < setupBytes ||
      heap_caps_get_free_size(MALLOC_CAP_8BIT) < setupBytes) {
    LOG_ERR(TAG, "Insufficient heap to prepare CSS");
    return false;
  }
  cssParser_.reset(new (std::nothrow) CssParser());
  if (!cssParser_) return false;
  auto fail = [&]() {
    cssParser_.reset();
    std::vector<std::string>().swap(cssFiles_);
    cssFilesKnown_ = false;
    return false;
  };
  const std::string stylesPath = cachePath + "/styles.bin";
  const bool cached = cssParser_->loadCache(stylesPath.c_str(), sourceFingerprint, checkAbort);
  if (!cached) {
    if (checkAbort()) return fail();
    if (!cssFilesKnown_) {
      BookMetadataCache::BookMetadata metadata;
      if (!parseContentOpf(metadata, true, decompressBuffer, scratch, checkAbort)) return fail();
    }
    const std::string temporary = cachePath + "/.tmp_css.css";
    for (const auto& href : cssFiles_) {
      if (checkAbort()) return fail();
      size_t size = 0;
      if (!getItemSize(href, &size) || size > 64 * 1024) {
        LOG_ERR(TAG, "Missing or oversized stylesheet: %s", href.c_str());
        return fail();
      }
      FsFile file;
      if (!SdMan.openFileForWrite(TAG, temporary, file)) return fail();
      bool ok = readItemContentsToStream(href, file, 1024, decompressBuffer, scratch, checkAbort) && file.sync();
      file.close();
      if (ok) ok = cssParser_->parseFile(temporary.c_str(), checkAbort);
      SdMan.remove(temporary.c_str());
      if (!ok) {
        LOG_ERR(TAG, "Failed to prepare stylesheet: %s", href.c_str());
        return fail();
      }
    }
    if (checkAbort()) return fail();
    if (sourceFingerprint && !cssParser_->saveCache(stylesPath.c_str(), sourceFingerprint, checkAbort)) {
      if (checkAbort()) return fail();
      LOG_ERR(TAG, "Could not save CSS rules; retaining complete rules in memory");
    }
  }
  cssFingerprint_ = sourceFingerprint;
  std::vector<std::string>().swap(cssFiles_);
  cssFilesKnown_ = false;
  LOG_INF(TAG, "%s %u CSS rules", cached ? "Loaded cached" : "Prepared",
          static_cast<unsigned>(cssParser_->getStyleCount()));
  return true;
}

// load in the meta data for the epub file
bool Epub::load(const bool buildIfMissing) {
  LOG_INF(TAG, "Loading ePub: %s", filepath.c_str());
  std::vector<std::string>().swap(cssFiles_);
  cssFilesKnown_ = false;

  // Initialize spine/TOC cache
  bookMetadataCache.reset(new BookMetadataCache(cachePath));

  // Try to load existing cache first
  if (bookMetadataCache->load()) {
    if (buildIfMissing) {
      maybeRecoverLostToc();
      if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
        LOG_ERR(TAG, "Cache became invalid during TOC recovery");
        return false;
      }
    }
    LOG_INF(TAG, "Loaded ePub: %s", filepath.c_str());
    migrateCoverFailures(cachePath);
    return true;
  }

  // If we didn't load from cache above and we aren't allowed to build, fail now
  if (!buildIfMissing) {
    return false;
  }

  // Cache doesn't exist or is invalid, build it
  LOG_INF(TAG, "Cache not found, building spine/TOC cache");
  setupCacheDir();

  // Begin building cache - stream entries to disk immediately
  if (!bookMetadataCache->beginWrite()) {
    LOG_ERR(TAG, "Could not begin writing cache");
    return false;
  }

  // OPF Pass
  BookMetadataCache::BookMetadata bookMetadata;
  if (!bookMetadataCache->beginContentOpfPass()) {
    LOG_ERR(TAG, "Could not begin writing content.opf pass");
    return false;
  }
  if (!parseContentOpf(bookMetadata)) {
    LOG_ERR(TAG, "Could not parse content.opf");
    return false;
  }
  if (!bookMetadataCache->endContentOpfPass()) {
    LOG_ERR(TAG, "Could not end writing content.opf pass");
    return false;
  }

  // TOC Pass - skip if heap is too low for inflate + expat
  const size_t freeHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  bool tocPassOk;
  if (freeHeap < MIN_HEAP_FOR_TOC) {
    LOG_INF(TAG, "Skipping TOC: heap too low (%zu < %zu)", freeHeap, MIN_HEAP_FOR_TOC);
    tocPassOk = bookMetadataCache->skipTocPass();
  } else {
    tocPassOk = bookMetadataCache->beginTocPass() && parseTocCandidates();
  }
  if (!tocPassOk) {
    LOG_ERR(TAG, "Could not write toc pass");
    return false;
  }

  // Close the cache files
  if (!bookMetadataCache->endWrite()) {
    LOG_ERR(TAG, "Could not end writing cache");
    return false;
  }

  // Build final book.bin
  if (!bookMetadataCache->buildBookBin(filepath, bookMetadata)) {
    LOG_ERR(TAG, "Could not update mappings and sizes");
    return false;
  }

  // Record why the TOC is empty so later opens can recover it.
  if (bookMetadataCache->getTocCount() == 0) {
    const uint8_t state = (!tocNavItem.empty() || !tocNcxItem.empty()) ? 0 : TOC_RETRY_NO_TOC;
    if (!writeTocRetryState(cachePath, state)) {
      LOG_ERR(TAG, "Could not record TOC retry state");
    }
  }

  if (!bookMetadataCache->cleanupTmpFiles()) {
    LOG_ERR(TAG, "Could not cleanup tmp files - ignoring");
  }

  // Reload the cache from disk so it's in the correct state
  bookMetadataCache.reset(new BookMetadataCache(cachePath));
  if (!bookMetadataCache->load()) {
    LOG_ERR(TAG, "Failed to reload cache after writing");
    return false;
  }

  migrateCoverFailures(cachePath);
  LOG_INF(TAG, "Loaded ePub: %s", filepath.c_str());
  return true;
}

bool Epub::loadMetadataOnly() {
  LOG_INF(TAG, "Loading metadata only: %s", filepath.c_str());
  std::vector<std::string>().swap(cssFiles_);
  cssFilesKnown_ = false;

  bookMetadataCache.reset(new BookMetadataCache(cachePath));

  if (bookMetadataCache->load()) {
    LOG_INF(TAG, "Metadata from cache: %s", filepath.c_str());
    return true;
  }

  // No cache — lightweight OPF parse for title/author/cover only (no spine/TOC)
  BookMetadataCache::BookMetadata bookMetadata;
  if (!parseContentOpf(bookMetadata, true)) {
    bookMetadataCache.reset();
    return false;
  }

  bookMetadataCache->coreMetadata = bookMetadata;
  return true;
}

bool Epub::prepareChapterHtml(int spineIndex, uint32_t sourceFingerprint, std::string& preparedPath,
                              uint8_t* decompressBuffer, BuildArena* scratch,
                              const std::function<bool()>& shouldAbort) {
  preparedPath.clear();
  if (!bookMetadataCache || !bookMetadataCache->isLoaded() || spineIndex < 0 || spineIndex >= getSpineItemsCount() ||
      (shouldAbort && shouldAbort())) {
    return false;
  }
  const auto entry = bookMetadataCache->getSpineEntry(spineIndex);
  if (entry.href.empty() || entry.href[0] == '/') return false;

  const std::string directory = cachePath + "/chapters";
  const std::string finalPath =
      directory + "/" + std::to_string(sourceFingerprint) + "_" + std::to_string(spineIndex) + ".xhtml";
  if (sourceFingerprint != 0) {
    FsFile cached;
    if (SdMan.openFileForRead("EPUB", finalPath, cached)) {
      const bool complete = cached.size() > 0;
      cached.close();
      if (complete) {
        preparedPath = finalPath;
        return true;
      }
    }
  }
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < MIN_HEAP_FOR_PREPARE) return false;
  setupCacheDir();
  if (!SdMan.ensureDirectoryExists(directory.c_str())) return false;

  const std::string rawPath = finalPath + ".tmp";
  const std::string normPath = finalPath + ".norm";
  FsFile raw;
  bool ok = SdMan.openFileForWrite("EPUB", rawPath, raw);
  if (ok) {
    ok = readItemContentsToStream(entry.href, raw, 1024, decompressBuffer, scratch, shouldAbort) && raw.sync();
  }
  raw.close();
  if (ok) ok = html5::normalizeHtmlForXml(rawPath, normPath, scratch, shouldAbort);
  if (ok && !(shouldAbort && shouldAbort())) {
    ok = SdMan.commitFile(normPath.c_str(), finalPath.c_str());
    if (!ok) SdMan.remove(finalPath.c_str());
  } else {
    ok = false;
  }
  SdMan.remove(rawPath.c_str());
  SdMan.remove(normPath.c_str());
  if (ok) preparedPath = finalPath;
  return ok;
}

bool Epub::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) {
    LOG_DBG(TAG, "Cache does not exist, no action needed");
    return true;
  }

  if (!SdMan.removeDir(cachePath.c_str())) {
    LOG_ERR(TAG, "Failed to clear cache");
    return false;
  }

  LOG_INF(TAG, "Cache cleared successfully");
  return true;
}

void Epub::setupCacheDir() const {
  if (!SdMan.exists(cachePath.c_str())) {
    SdMan.mkdir(cachePath.c_str());
  }

  // Create sections subdirectory for page cache
  const auto sectionsDir = cachePath + "/sections";
  if (!SdMan.exists(sectionsDir.c_str())) {
    SdMan.mkdir(sectionsDir.c_str());
  }

  // Create images subdirectory for cached images
  const auto imagesDir = cachePath + "/images";
  if (!SdMan.exists(imagesDir.c_str())) {
    SdMan.mkdir(imagesDir.c_str());
  }
}

const std::string& Epub::getCachePath() const { return cachePath; }

const std::string& Epub::getPath() const { return filepath; }

const std::string& Epub::getTitle() const {
  static std::string blank;
  if (!bookMetadataCache) return blank;
  return bookMetadataCache->coreMetadata.title;
}

const std::string& Epub::getAuthor() const {
  static std::string blank;
  if (!bookMetadataCache) return blank;
  return bookMetadataCache->coreMetadata.author;
}

const std::string& Epub::getLanguage() const {
  static std::string blank;
  if (!bookMetadataCache) return blank;
  return bookMetadataCache->coreMetadata.language;
}

std::string Epub::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }

std::string Epub::findCoverImage(const std::function<bool()>& shouldAbort) const {
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";

  std::string baseName = filepath.substr(lastSlash + 1);
  size_t lastDot = baseName.find_last_of('.');
  if (lastDot != std::string::npos) {
    baseName = baseName.substr(0, lastDot);
  }

  return CoverHelpers::findCoverImage(dirPath, baseName, shouldAbort);
}

home_thumbnail::Result Epub::extractCoverSource(const std::string& itemHref,
                                                const std::function<bool()>& shouldAbort) const {
  if (CoverHelpers::isAbortRequested(shouldAbort)) return home_thumbnail::Result::Cancelled;
  size_t size = 0;
  if (itemHref.empty() || !getItemSize(itemHref, &size) || size == 0 || size > MAX_COVER_IMAGE_SIZE) {
    return home_thumbnail::Result::Unavailable;
  }
  const std::string sourcePath = cachePath + "/.cover-source.tmp";
  SdMan.remove(sourcePath.c_str());

  FsFile output;
  if (!SdMan.openFileForWrite("EBP", sourcePath, output)) return home_thumbnail::Result::Unavailable;
  const bool extracted = readItemContentsToStream(itemHref, output, 1024, nullptr, nullptr, shouldAbort);
  const bool synced = extracted && output.sync();
  output.close();

  if (CoverHelpers::isAbortRequested(shouldAbort)) {
    SdMan.remove(sourcePath.c_str());
    return home_thumbnail::Result::Cancelled;
  }
  if (!synced || ImageConverterFactory::detectFormat(sourcePath) == ImageFormat::Unknown) {
    SdMan.remove(sourcePath.c_str());
    return home_thumbnail::Result::Unavailable;
  }
  return home_thumbnail::Result::Ready;
}

bool Epub::generateCoverFromItem(const std::string& itemHref, const bool use1BitDithering,
                                 const std::function<bool()>& shouldAbort) const {
  const std::string href = coverHref("", itemHref);
  if (href.empty() || CoverHelpers::isAbortRequested(shouldAbort)) return false;
  const std::string sourcePath = cachePath + "/.cover-source.tmp";
  const auto convert = [&](const std::string& candidate) {
    if (extractCoverSource(candidate, shouldAbort) != home_thumbnail::Result::Ready) return false;
    const bool success =
        CoverHelpers::convertImageToBmp(sourcePath, getCoverBmpPath(), "EBP", use1BitDithering, shouldAbort);
    SdMan.remove(sourcePath.c_str());
    return success;
  };
  if (convert(href)) return true;
  if (CoverHelpers::isAbortRequested(shouldAbort)) return false;

  size_t size = 0;
  if (!getItemSize(href, &size) || size == 0 || size > MAX_COVER_WRAPPER_SIZE) return false;
  CoverLinkParser wrapper(size, false);
  if (!wrapper.setup() || !readItemContentsToStream(href, wrapper, 1024, nullptr, nullptr, shouldAbort) ||
      !wrapper.complete())
    return false;

  struct Candidate {
    size_t index;
    uint32_t area;
  };
  std::array<Candidate, MAX_COVER_LINKS> candidates;
  size_t count = 0;
  const auto& links = wrapper.links();
  for (size_t i = 0; i < links.size() && count < candidates.size(); ++i) {
    const std::string candidate = coverHref(href, links[i]);
    const auto result = extractCoverSource(candidate, shouldAbort);
    if (result == home_thumbnail::Result::Cancelled) return false;
    if (result != home_thumbnail::Result::Ready) continue;
    const uint32_t area = coverImageArea(sourcePath, shouldAbort);
    SdMan.remove(sourcePath.c_str());
    if (area) candidates[count++] = {i, area};
  }
  std::sort(candidates.begin(), candidates.begin() + count, [](const Candidate& a, const Candidate& b) {
    return a.area != b.area ? a.area > b.area : a.index < b.index;
  });
  for (size_t i = 0; i < count; ++i) {
    if (CoverHelpers::isAbortRequested(shouldAbort)) return false;
    if (convert(coverHref(href, links[candidates[i].index]))) return true;
  }
  return false;
}

bool Epub::generateCoverBmp(const bool use1BitDithering, const std::function<bool()>& shouldAbort) const {
  if (CoverHelpers::isAbortRequested(shouldAbort)) return false;
  setupCacheDir();
  migrateCoverFailures(cachePath);
  const std::string coverPath = getCoverBmpPath();
  if (home_thumbnail::validateCover(coverPath)) return true;
  if (SdMan.exists(coverPath.c_str())) SdMan.remove(coverPath.c_str());
  const auto finish = [&]() {
    if (CoverHelpers::isAbortRequested(shouldAbort)) {
      SdMan.remove(coverPath.c_str());
      return false;
    }
    if (!home_thumbnail::validateCover(coverPath)) return false;
    SdMan.remove((cachePath + "/.thumb.failed").c_str());
    return true;
  };
  const std::string sidecar = findCoverImage(shouldAbort);
  if (!sidecar.empty()) {
    return CoverHelpers::convertImageToBmp(sidecar, coverPath, "EBP", use1BitDithering, shouldAbort) && finish();
  }
  if (CoverHelpers::isAbortRequested(shouldAbort)) return false;

  std::string opfPath;
  size_t opfSize = 0;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= MIN_HEAP_FOR_TOC && findContentOpfFile(&opfPath) &&
      getItemSize(opfPath, &opfSize)) {
    const std::string basePath = opfPath.substr(0, opfPath.find_last_of('/') + 1);
    ContentOpfParser opf(cachePath, basePath, opfSize, nullptr);
    if (opf.setup() && readItemContentsToStream(opfPath, opf, 1024, nullptr, nullptr, shouldAbort)) {
      const std::string* explicitTargets[] = {&opf.coverImageHref, &opf.coverItemHref, &opf.guideCoverPageHref};
      for (size_t i = 0; i < 3; ++i) {
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) duplicate |= *explicitTargets[i] == *explicitTargets[j];
        if (!duplicate && generateCoverFromItem(*explicitTargets[i], use1BitDithering, shouldAbort)) return finish();
        if (CoverHelpers::isAbortRequested(shouldAbort)) return false;
      }
      const std::string navPath = coverHref("", opf.tocNavPath);
      size_t navSize = 0;
      if (!navPath.empty() && getItemSize(navPath, &navSize) && navSize <= MAX_COVER_WRAPPER_SIZE) {
        CoverLinkParser landmarks(navSize, true);
        if (landmarks.setup() && readItemContentsToStream(navPath, landmarks, 1024, nullptr, nullptr, shouldAbort) &&
            landmarks.complete()) {
          for (const auto& link : landmarks.links()) {
            if (generateCoverFromItem(coverHref(navPath, link), use1BitDithering, shouldAbort)) return finish();
            if (CoverHelpers::isAbortRequested(shouldAbort)) return false;
          }
        }
      }
      if (opf.coverIdHref != opf.coverImageHref && opf.coverIdHref != opf.coverItemHref &&
          opf.coverIdHref != opf.guideCoverPageHref &&
          generateCoverFromItem(opf.coverIdHref, use1BitDithering, shouldAbort))
        return finish();
    }
  }
  if (CoverHelpers::isAbortRequested(shouldAbort)) return false;
  static const char* const commonCoverPaths[] = {
      "cover.jpg",
      "cover.jpeg",
      "cover.png",
      "cover.bmp",
      "images/cover.jpg",
      "images/cover.jpeg",
      "images/cover.png",
      "images/cover.bmp",
      "Images/cover.jpg",
      "Images/cover.jpeg",
      "Images/cover.png",
      "Images/cover.bmp",
      "OEBPS/cover.jpg",
      "OEBPS/cover.jpeg",
      "OEBPS/cover.png",
      "OEBPS/cover.bmp",
      "OEBPS/images/cover.jpg",
      "OEBPS/images/cover.jpeg",
      "OEBPS/images/cover.png",
      "OEBPS/images/cover.bmp",
      "OEBPS/Images/cover.jpg",
      "OEBPS/Images/cover.jpeg",
      "OEBPS/Images/cover.png",
      "OEBPS/Images/cover.bmp",
      "OPS/cover.jpg",
      "OPS/cover.jpeg",
      "OPS/cover.png",
      "OPS/cover.bmp",
      "OPS/images/cover.jpg",
      "OPS/images/cover.jpeg",
      "OPS/images/cover.png",
      "OPS/images/cover.bmp",
      "OPS/Images/cover.jpg",
      "OPS/Images/cover.jpeg",
      "OPS/Images/cover.png",
      "OPS/Images/cover.bmp",
      "EPUB/cover.jpg",
      "EPUB/cover.jpeg",
      "EPUB/cover.png",
      "EPUB/cover.bmp",
      "EPUB/images/cover.jpg",
      "EPUB/images/cover.jpeg",
      "EPUB/images/cover.png",
      "EPUB/images/cover.bmp",
      "EPUB/Images/cover.jpg",
      "EPUB/Images/cover.jpeg",
      "EPUB/Images/cover.png",
      "EPUB/Images/cover.bmp",
  };

  constexpr int pathCount = sizeof(commonCoverPaths) / sizeof(commonCoverPaths[0]);
  for (int start = 0; start < pathCount;) {
    const int found = ZipFile(filepath).findFirstExisting(commonCoverPaths + start, pathCount - start, shouldAbort);
    if (found < 0 || CoverHelpers::isAbortRequested(shouldAbort)) break;
    start += found;
    if (generateCoverFromItem(commonCoverPaths[start], use1BitDithering, shouldAbort)) return finish();
    ++start;
  }
  SdMan.remove((cachePath + "/.cover-source.tmp").c_str());
  return false;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref, size_t* size, const bool trailingNullByte) const {
  if (itemHref.empty()) {
    LOG_ERR(TAG, "Failed to read item, empty href");
    return nullptr;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);

  const auto content = ZipFile(filepath).readFileToMemory(path.c_str(), size, trailingNullByte);
  if (!content) {
    LOG_ERR(TAG, "Failed to read item %s", path.c_str());
    return nullptr;
  }

  return content;
}

bool Epub::readItemContentsToStream(const std::string& itemHref, Print& out, const size_t chunkSize,
                                    uint8_t* dictBuffer, BuildArena* scratch,
                                    const std::function<bool()>& shouldAbort) const {
  if (itemHref.empty()) {
    LOG_ERR(TAG, "Failed to read item, empty href");
    return false;
  }

  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).readFileToStream(path.c_str(), out, chunkSize, dictBuffer, shouldAbort, scratch);
}

bool Epub::getItemSize(const std::string& itemHref, size_t* size) const {
  const std::string path = FsHelpers::normalisePath(itemHref);
  return ZipFile(filepath).getInflatedFileSize(path.c_str(), size);
}

bool Epub::getSpineItemSizes(std::vector<size_t>& sizes) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return false;

  const int spineCount = bookMetadataCache->getSpineCount();
  sizes.resize(static_cast<size_t>(spineCount), 0);
  if (spineCount == 0) return true;

  std::vector<ZipFile::SizeTarget> targets;
  targets.reserve(static_cast<size_t>(spineCount));
  for (int i = 0; i < spineCount; ++i) {
    const auto entry = bookMetadataCache->getSpineEntry(i);
    const std::string path = FsHelpers::normalisePath(entry.href);
    if (path.size() >= 255) continue;  // fillUncompressedSizes stack buffer limit
    const auto len = static_cast<uint16_t>(path.size());
    targets.push_back({ZipFile::fnvHash64(path.c_str(), len), len, static_cast<uint16_t>(i)});
  }
  std::sort(targets.begin(), targets.end());

  std::vector<uint32_t> rawSizes(static_cast<size_t>(spineCount), 0);
  ZipFile zf(filepath);
  zf.fillUncompressedSizes(targets, rawSizes);

  for (int i = 0; i < spineCount; ++i) {
    sizes[static_cast<size_t>(i)] = static_cast<size_t>(rawSizes[static_cast<size_t>(i)]);
  }
  return true;
}

int Epub::getSpineItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }
  return bookMetadataCache->getSpineCount();
}

BookMetadataCache::SpineEntry Epub::getSpineItem(const int spineIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getSpineItem called but cache not loaded");
    return {};
  }

  if (spineIndex < 0 || spineIndex >= bookMetadataCache->getSpineCount()) {
    LOG_ERR(TAG, "getSpineItem index:%d is out of range", spineIndex);
    return bookMetadataCache->getSpineEntry(0);
  }

  return bookMetadataCache->getSpineEntry(spineIndex);
}

BookMetadataCache::TocEntry Epub::getTocItem(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getTocItem called but cache not loaded");
    return {};
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR(TAG, "getTocItem index:%d is out of range", tocIndex);
    return {};
  }

  return bookMetadataCache->getTocEntry(tocIndex);
}

bool Epub::getTocItems(std::vector<BookMetadataCache::TocEntry>& entries, int maxCount) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) return false;
  return bookMetadataCache->readTocEntries(entries, maxCount);
}

int Epub::getTocItemsCount() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    return 0;
  }

  return bookMetadataCache->getTocCount();
}

// work out the section index for a toc index
int Epub::getSpineIndexForTocIndex(const int tocIndex) const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getSpineIndexForTocIndex called but cache not loaded");
    return 0;
  }

  if (tocIndex < 0 || tocIndex >= bookMetadataCache->getTocCount()) {
    LOG_ERR(TAG, "getSpineIndexForTocIndex: tocIndex %d out of range", tocIndex);
    return 0;
  }

  const int spineIndex = bookMetadataCache->getTocEntry(tocIndex).spineIndex;
  if (spineIndex < 0) {
    LOG_ERR(TAG, "Section not found for TOC index %d", tocIndex);
    return 0;
  }

  return spineIndex;
}

int Epub::getTocIndexForSpineIndex(const int spineIndex) const { return getSpineItem(spineIndex).tocIndex; }

int Epub::getSpineIndexForTextReference() const {
  if (!bookMetadataCache || !bookMetadataCache->isLoaded()) {
    LOG_ERR(TAG, "getSpineIndexForTextReference called but cache not loaded");
    return 0;
  }
  LOG_DBG(TAG, "Core Metadata: cover(%d)=%s, textReference(%d)=%s",
          bookMetadataCache->coreMetadata.coverItemHref.size(), bookMetadataCache->coreMetadata.coverItemHref.c_str(),
          bookMetadataCache->coreMetadata.textReferenceHref.size(),
          bookMetadataCache->coreMetadata.textReferenceHref.c_str());

  if (bookMetadataCache->coreMetadata.textReferenceHref.empty()) {
    // there was no textReference in epub, so we return 0 (the first chapter)
    return 0;
  }

  // loop through spine items to get the correct index matching the text href
  for (size_t i = 0; i < getSpineItemsCount(); i++) {
    if (getSpineItem(i).href == bookMetadataCache->coreMetadata.textReferenceHref) {
      LOG_DBG(TAG, "Text reference %s found at index %d", bookMetadataCache->coreMetadata.textReferenceHref.c_str(), i);
      return i;
    }
  }
  // This should not happen, as we checked for empty textReferenceHref earlier
  LOG_ERR(TAG, "Section not found for text reference");
  return 0;
}
