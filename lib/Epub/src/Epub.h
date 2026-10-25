#pragma once

#include <HomeThumbnail.h>
#include <Print.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/BookMetadataCache.h"
#include "Epub/css/CssParser.h"

class BuildArena;
class ZipFile;

class Epub {
 public:
  static constexpr size_t MIN_CACHED_CHAPTER_SIZE = 64 * 1024;
  static constexpr size_t MIN_HEAP_FOR_PREPARE = 48 * 1024;

 private:
  std::string tocNcxItem;
  std::string tocNavItem;
  std::string filepath;
  std::string contentBasePath;
  std::string cachePath;
  std::unique_ptr<BookMetadataCache> bookMetadataCache;
  std::unique_ptr<CssParser> cssParser_;
  std::vector<std::string> cssFiles_;

  bool findContentOpfFile(std::string* contentOpfFile) const;
  bool parseCssFiles();
  bool parseContentOpf(BookMetadataCache::BookMetadata& bookMetadata, bool metadataOnly = false);
  bool parseTocNcxFile() const;
  bool parseTocNavFile() const;
  bool parseTocCandidates();
  void maybeRecoverLostToc();
  void recoverLostToc();

  home_thumbnail::Result extractCoverSource(const std::string& itemHref,
                                            const std::function<bool()>& shouldAbort) const;
  bool generateCoverFromItem(const std::string& itemHref, bool use1BitDithering,
                             const std::function<bool()>& shouldAbort) const;

 public:
  explicit Epub(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
    cachePath = cacheDir + "/epub_" + std::to_string(std::hash<std::string>{}(this->filepath));
  }
  ~Epub() = default;
  std::string& getBasePath() { return contentBasePath; }
  bool load(bool buildIfMissing = true);
  bool loadMetadataOnly();
  bool prepareChapterHtml(int spineIndex, uint32_t sourceFingerprint, std::string& preparedPath,
                          uint8_t* decompressBuffer, BuildArena* scratch, const std::function<bool()>& shouldAbort);
  bool clearCache() const;
  void setupCacheDir() const;
  const std::string& getCachePath() const;
  const std::string& getPath() const;
  const std::string& getTitle() const;
  const std::string& getAuthor() const;
  const std::string& getLanguage() const;
  std::string getCoverBmpPath() const;
  bool generateCoverBmp(bool use1BitDithering = false, const std::function<bool()>& shouldAbort = nullptr) const;
  std::string findCoverImage(const std::function<bool()>& shouldAbort = nullptr) const;
  uint8_t* readItemContentsToBytes(const std::string& itemHref, size_t* size = nullptr,
                                   bool trailingNullByte = false) const;
  bool readItemContentsToStream(const std::string& itemHref, Print& out, size_t chunkSize,
                                uint8_t* dictBuffer = nullptr, BuildArena* scratch = nullptr,
                                const std::function<bool()>& shouldAbort = nullptr) const;
  bool getItemSize(const std::string& itemHref, size_t* size) const;
  bool getSpineItemSizes(std::vector<size_t>& sizes) const;
  BookMetadataCache::SpineEntry getSpineItem(int spineIndex) const;
  BookMetadataCache::TocEntry getTocItem(int tocIndex) const;
  bool getTocItems(std::vector<BookMetadataCache::TocEntry>& entries, int maxCount) const;
  int getSpineItemsCount() const;
  int getTocItemsCount() const;
  int getSpineIndexForTocIndex(int tocIndex) const;
  int getTocIndexForSpineIndex(int spineIndex) const;
  int getSpineIndexForTextReference() const;
  const CssParser* getCssParser() const { return cssParser_.get(); }
};
