/**
 * Fb2.h
 *
 * FictionBook 2.0 XML e-book handler for Papyrix Reader
 * Provides EPUB-like interface for FB2 file handling
 */

#pragma once

#include <expat.h>

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

/**
 * Fb2 File Handler
 *
 * Handles FB2 file loading, XML parsing, and metadata extraction.
 */
class Fb2 {
 public:
  struct TocItem {
    std::string title;
    int sectionIndex = -1;  // Sequential section number (0-based)
  };

 private:
  std::string filepath;
  std::string cachePath;
  std::string title;
  std::string author;
  std::string coverRef;
  std::string coverContentType;
  std::string language;
  int64_t coverBinaryOffset_ = -1;  // Byte offset of cover <binary> tag (-1 = not found)
  size_t fileSize;
  bool loaded;

  // XML parsing state
  XML_Parser xmlParser_ = nullptr;
  int depth = 0;
  int skipUntilDepth = INT_MAX;  // Skip content inside binary tags

  // Metadata extraction state
  bool inBookTitle = false;
  bool inFirstName = false;
  bool inLastName = false;
  bool inAuthor = false;
  bool inTitleInfo = false;
  bool inCoverPage = false;
  bool inLang = false;
  std::string currentAuthorFirst;
  std::string currentAuthorLast;

  // Body tracking (for TOC section counting)
  bool inBody = false;
  int bodyCount_ = 0;

  // TOC extraction state (tocItems_ used only during initial parse, then freed)
  std::vector<TocItem> tocItems_;
  // Lazy TOC: compact LUT of file offsets into meta.bin (4 bytes/entry vs ~68 bytes/entry)
  std::vector<uint32_t> tocLut_;
  uint16_t tocItemCount_ = 0;
  int sectionCounter_ = 0;
  bool inSectionTitle_ = false;
  int sectionTitleDepth_ = 0;
  std::string currentSectionTitle_;

  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL endElement(void* userData, const XML_Char* name);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);

  // Helper methods
  bool parseXmlStream();
  void postProcessMetadata();
  bool loadMetaCache();
  bool saveMetaCache() const;
  std::string metaCachePath() const;

 public:
  explicit Fb2(std::string filepath, const std::string& cacheDir);
  ~Fb2();

  /**
   * Load FB2 file (verify existence and parse metadata)
   * @return true on success
   */
  bool load();

  /**
   * Clear cached data
   * @return true on success
   */
  bool clearCache() const;

  /**
   * Setup cache directory
   */
  void setupCacheDir() const;

  // Path accessors
  const std::string& getCachePath() const { return cachePath; }
  const std::string& getPath() const { return filepath; }

  // Metadata
  const std::string& getTitle() const { return title; }
  const std::string& getAuthor() const { return author; }
  const std::string& getLanguage() const { return language; }
  size_t getFileSize() const { return fileSize; }

  // Cover image support
  std::string getCoverBmpPath() const;
  bool generateCoverBmp(bool use1BitDithering = false) const;
  std::string getThumbBmpPath() const;
  bool generateThumbBmp() const;

  /**
   * Read content from file at specified offset
   * @param buffer Output buffer
   * @param offset Byte offset in file
   * @param length Number of bytes to read
   * @return Number of bytes actually read
   */
  size_t readContent(uint8_t* buffer, size_t offset, size_t length) const;

  /**
   * Extract embedded cover image from FB2 <binary> element to file
   * @param outputPath Path to write the decoded image
   * @return true on success
   */
  bool extractEmbeddedCover(const std::string& outputPath) const;

  /**
   * Find a cover image in the same directory as the FB2 file
   * @return Path to cover image, or empty string if not found
   */
  std::string findCoverImage() const;

  // Check if file is loaded
  bool isLoaded() const { return loaded; }

  // TOC access (lazy: reads from cache file on demand)
  uint16_t tocCount() const { return tocItemCount_; }
  TocItem getTocItem(uint16_t index) const;
};
