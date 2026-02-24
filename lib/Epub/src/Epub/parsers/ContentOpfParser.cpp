#include "ContentOpfParser.h"

#include <FsHelpers.h>
#include <Logging.h>
#include <Serialization.h>
#include <Utf8.h>

#include "../BookMetadataCache.h"

#define TAG "OPF"

namespace {
constexpr char MEDIA_TYPE_NCX[] = "application/x-dtbncx+xml";
constexpr char itemCacheFile[] = "/.items.bin";

// Find the last valid UTF-8 boundary within maxLen bytes
// Returns the safe length to use for truncation (may be less than maxLen)
size_t findUtf8Boundary(const char* s, size_t maxLen) {
  if (maxLen == 0) return 0;

  // Work backwards from maxLen to find a valid boundary
  size_t pos = maxLen;
  while (pos > 0) {
    const unsigned char c = static_cast<unsigned char>(s[pos - 1]);
    // If this byte is ASCII or a UTF-8 start byte, previous position is safe
    if (c < 0x80 || c >= 0xC0) {
      // Previous char ended cleanly at pos-1, so pos is a valid boundary
      // But we need to check if we're in the middle of a multi-byte sequence
      if (c < 0x80) {
        return pos;  // ASCII byte, safe to cut after it
      }
      // This is a UTF-8 start byte - don't cut before it completes
      // Check if the full character fits
      size_t charLen = 1;
      if ((c & 0xE0) == 0xC0)
        charLen = 2;
      else if ((c & 0xF0) == 0xE0)
        charLen = 3;
      else if ((c & 0xF8) == 0xF0)
        charLen = 4;

      if (pos - 1 + charLen <= maxLen) {
        return pos - 1 + charLen;  // Full character fits
      }
      // Character doesn't fit, try earlier
      pos--;
      continue;
    }
    // Continuation byte (0x80-0xBF), keep going back
    pos--;
  }
  return 0;
}
}  // namespace

bool ContentOpfParser::setup() {
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    LOG_ERR(TAG, "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

ContentOpfParser::~ContentOpfParser() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
    XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
    XML_SetCharacterDataHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
  if (tempItemStore) {
    tempItemStore.close();
  }
  if (SdMan.exists((cachePath + itemCacheFile).c_str())) {
    SdMan.remove((cachePath + itemCacheFile).c_str());
  }
}

size_t ContentOpfParser::write(const uint8_t data) { return write(&data, 1); }

size_t ContentOpfParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser) return 0;

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);

    if (!buf) {
      LOG_ERR(TAG, "Couldn't allocate memory for buffer");
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      parser = nullptr;
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), remainingSize == toRead) == XML_STATUS_ERROR) {
      LOG_ERR(TAG, "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      XML_StopParser(parser, XML_FALSE);                // Stop any pending processing
      XML_SetElementHandler(parser, nullptr, nullptr);  // Clear callbacks
      XML_SetCharacterDataHandler(parser, nullptr);
      XML_ParserFree(parser);
      parser = nullptr;
      return 0;
    }

    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }

  return size;
}

void XMLCALL ContentOpfParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)atts;

  if (self->state == START && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:title") == 0) {
    self->state = IN_BOOK_TITLE;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:creator") == 0) {
    if (!self->author.empty()) {
      self->author.append(", ");
    }
    self->state = IN_BOOK_AUTHOR;
    return;
  }

  if (self->state == IN_METADATA && strcmp(name, "dc:language") == 0) {
    self->state = IN_BOOK_LANGUAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
    self->state = IN_MANIFEST;
    if (!SdMan.openFileForWrite("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR(TAG, "Couldn't open temp items file for writing. This is probably going to be a fatal error.");
    }
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "spine") == 0 || strcmp(name, "opf:spine") == 0)) {
    self->state = IN_SPINE;
    if (!SdMan.openFileForRead("COF", self->cachePath + itemCacheFile, self->tempItemStore)) {
      LOG_ERR(TAG, "Couldn't open temp items file for reading. This is probably going to be a fatal error.");
    } else {
      std::string itemId;
      std::string href;
      while (self->tempItemStore.available()) {
        if (!serialization::readString(self->tempItemStore, itemId) ||
            !serialization::readString(self->tempItemStore, href)) {
          LOG_ERR(TAG, "Failed to read manifest item from temp store");
          break;
        }
        self->manifestIndex[itemId] = href;
      }
      self->tempItemStore.close();
    }
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "guide") == 0 || strcmp(name, "opf:guide") == 0)) {
    self->state = IN_GUIDE;
    return;
  }

  if (self->state == IN_METADATA && (strcmp(name, "meta") == 0 || strcmp(name, "opf:meta") == 0)) {
    bool isCover = false;
    std::string coverItemId;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "name") == 0 && strcmp(atts[i + 1], "cover") == 0) {
        isCover = true;
      } else if (strcmp(atts[i], "content") == 0) {
        coverItemId = atts[i + 1];
      }
    }

    if (isCover) {
      self->coverItemId = coverItemId;
    }
    return;
  }

  if (self->state == IN_MANIFEST && (strcmp(name, "item") == 0 || strcmp(name, "opf:item") == 0)) {
    std::string itemId;
    std::string href;
    std::string mediaType;
    std::string properties;

    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0) {
        itemId = atts[i + 1];
      } else if (strcmp(atts[i], "href") == 0) {
        href = FsHelpers::normalisePath(self->baseContentPath + atts[i + 1]);
      } else if (strcmp(atts[i], "media-type") == 0) {
        mediaType = atts[i + 1];
      } else if (strcmp(atts[i], "properties") == 0) {
        properties = atts[i + 1];
      }
    }

    // Write items down to SD card
    serialization::writeString(self->tempItemStore, itemId);
    serialization::writeString(self->tempItemStore, href);

    if (itemId == self->coverItemId) {
      self->coverItemHref = href;
    }

    if (mediaType == MEDIA_TYPE_NCX) {
      if (self->tocNcxPath.empty()) {
        self->tocNcxPath = href;
      } else {
        LOG_DBG(TAG, "Warning: Multiple NCX files found in manifest. Ignoring duplicate: %s", href.c_str());
      }
    }

    // EPUB 3: Check for nav document (properties contains "nav")
    if (!properties.empty() && self->tocNavPath.empty()) {
      // Properties is space-separated, check if "nav" is present as a word
      if (properties == "nav" || properties.find("nav ") == 0 || properties.find(" nav") != std::string::npos) {
        self->tocNavPath = href;
        LOG_INF(TAG, "Found EPUB 3 nav document: %s", href.c_str());
      }
    }

    // Collect CSS files
    if (mediaType.find("css") != std::string::npos) {
      self->cssFiles_.push_back(href);
      LOG_DBG(TAG, "Found CSS file: %s", href.c_str());
    }
    return;
  }

  // NOTE: This relies on spine appearing after item manifest (which is pretty safe as it's part of the EPUB spec)
  // Only run the spine parsing if there's a cache to add it to
  if (self->cache) {
    if (self->state == IN_SPINE && (strcmp(name, "itemref") == 0 || strcmp(name, "opf:itemref") == 0)) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "idref") == 0) {
          const std::string idref = atts[i + 1];
          auto it = self->manifestIndex.find(idref);
          if (it != self->manifestIndex.end()) {
            self->cache->createSpineEntry(it->second);
          }
        }
      }
      return;
    }
  }
  // parse the guide
  if (self->state == IN_GUIDE && (strcmp(name, "reference") == 0 || strcmp(name, "opf:reference") == 0)) {
    std::string type;
    std::string textHref;
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "type") == 0) {
        type = atts[i + 1];
        if (type == "text" || type == "start") {
          continue;
        } else {
          LOG_DBG(TAG, "Skipping non-text reference in guide: %s", type.c_str());
          break;
        }
      } else if (strcmp(atts[i], "href") == 0) {
        textHref = FsHelpers::normalisePath(self->baseContentPath + atts[i + 1]);
      }
    }
    if ((type == "text" || (type == "start" && !self->textReferenceHref.empty())) && (textHref.length() > 0)) {
      LOG_INF(TAG, "Found %s reference in guide: %s", type.c_str(), textHref.c_str());
      self->textReferenceHref = textHref;
    }
    return;
  }
}

void XMLCALL ContentOpfParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ContentOpfParser*>(userData);

  if (self->state == IN_BOOK_TITLE) {
    if (self->title.size() + static_cast<size_t>(len) <= MAX_TITLE_LENGTH) {
      self->title.append(s, len);
    } else if (self->title.size() < MAX_TITLE_LENGTH) {
      const size_t remaining = MAX_TITLE_LENGTH - self->title.size();
      const size_t safeLen = findUtf8Boundary(s, remaining);
      if (safeLen > 0) {
        self->title.append(s, safeLen);
      }
      LOG_DBG(TAG, "Title truncated at %zu bytes", self->title.size());
    }
    return;
  }

  if (self->state == IN_BOOK_AUTHOR) {
    if (self->author.size() + static_cast<size_t>(len) <= MAX_AUTHOR_LENGTH) {
      self->author.append(s, len);
    } else if (self->author.size() < MAX_AUTHOR_LENGTH) {
      const size_t remaining = MAX_AUTHOR_LENGTH - self->author.size();
      const size_t safeLen = findUtf8Boundary(s, remaining);
      if (safeLen > 0) {
        self->author.append(s, safeLen);
      }
      LOG_DBG(TAG, "Author truncated at %zu bytes", self->author.size());
    }
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE) {
    // Language tags are ASCII, no UTF-8 boundary concerns
    constexpr size_t MAX_LANGUAGE_LENGTH = 32;
    if (self->language.size() + static_cast<size_t>(len) <= MAX_LANGUAGE_LENGTH) {
      self->language.append(s, len);
    }
    return;
  }
}

void XMLCALL ContentOpfParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ContentOpfParser*>(userData);
  (void)name;

  if (self->state == IN_SPINE && (strcmp(name, "spine") == 0 || strcmp(name, "opf:spine") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_GUIDE && (strcmp(name, "guide") == 0 || strcmp(name, "opf:guide") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_MANIFEST && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
    self->state = IN_PACKAGE;
    self->tempItemStore.close();
    return;
  }

  if (self->state == IN_BOOK_TITLE && strcmp(name, "dc:title") == 0) {
    self->title.resize(utf8NormalizeNfc(&self->title[0], self->title.size()));
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_AUTHOR && strcmp(name, "dc:creator") == 0) {
    self->author.resize(utf8NormalizeNfc(&self->author[0], self->author.size()));
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_BOOK_LANGUAGE && strcmp(name, "dc:language") == 0) {
    // Trim ASCII whitespace (pretty-printed OPF may have newlines around the tag text)
    auto& lang = self->language;
    const size_t start = lang.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
      lang.clear();
    } else {
      const size_t end = lang.find_last_not_of(" \t\r\n");
      lang = lang.substr(start, end - start + 1);
    }
    self->state = IN_METADATA;
    return;
  }

  if (self->state == IN_METADATA && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
    self->state = IN_PACKAGE;
    return;
  }

  if (self->state == IN_PACKAGE && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
    self->state = START;
    return;
  }
}
