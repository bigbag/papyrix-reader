#pragma once
#include <Print.h>

#include <algorithm>
#include <vector>

#include "Epub.h"
#include "expat.h"

constexpr size_t MAX_TITLE_LENGTH = 256;
constexpr size_t MAX_AUTHOR_LENGTH = 128;

class BookMetadataCache;

class ContentOpfParser final : public Print {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_LANGUAGE,
    IN_MANIFEST,
    IN_SPINE,
    IN_GUIDE,
  };

  struct ManifestIndexEntry {
    uint32_t idHash;
    uint32_t fileOffset;
    bool operator<(const ManifestIndexEntry& o) const { return idHash < o.idHash; }
  };

  static uint32_t fnvHash32(const char* s) {
    uint32_t h = 2166136261u;
    while (*s) {
      h ^= static_cast<uint8_t>(*s++);
      h *= 16777619u;
    }
    return h;
  }

  const std::string& cachePath;
  const std::string& baseContentPath;
  size_t remainingSize;
  XML_Parser parser = nullptr;
  ParserState state = START;
  BookMetadataCache* cache;
  FsFile tempItemStore;
  std::vector<ManifestIndexEntry> manifestCompact_;
  std::string coverItemId;
  std::vector<std::string> cssFiles_;

  static void startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void characterData(void* userData, const XML_Char* s, int len);
  static void endElement(void* userData, const XML_Char* name);

 public:
  std::string title;
  std::string author;
  std::string language;
  std::string tocNcxPath;
  std::string tocNavPath;  // EPUB 3 nav document path
  std::string coverItemHref;
  std::string guideCoverPageHref;
  std::string textReferenceHref;
  const std::vector<std::string>& getCssFiles() const { return cssFiles_; }

  explicit ContentOpfParser(const std::string& cachePath, const std::string& baseContentPath, const size_t xmlSize,
                            BookMetadataCache* cache)
      : cachePath(cachePath), baseContentPath(baseContentPath), remainingSize(xmlSize), cache(cache) {}
  ~ContentOpfParser() override;

  bool setup();

  size_t write(uint8_t) override;
  size_t write(const uint8_t* buffer, size_t size) override;
};
