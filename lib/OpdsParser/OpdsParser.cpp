#include "OpdsParser.h"

#include <HardwareSerial.h>

#include <cstring>

OpdsParser::~OpdsParser() {
  if (parser) {
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

bool OpdsParser::startParsing() {
  clear();
  parser = XML_ParserCreate(nullptr);
  if (!parser) {
    Serial.printf("[%lu] [OPDS] Couldn't allocate memory for parser\n", millis());
    return false;
  }

  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetCharacterDataHandler(parser, characterData);
  return true;
}

bool OpdsParser::feedChunk(const char* data, const size_t len) {
  if (!parser) {
    Serial.printf("[%lu] [OPDS] Parser not initialized\n", millis());
    return false;
  }

  void* const buf = XML_GetBuffer(parser, len);
  if (!buf) {
    Serial.printf("[%lu] [OPDS] Couldn't allocate memory for buffer\n", millis());
    return false;
  }

  memcpy(buf, data, len);

  if (XML_ParseBuffer(parser, static_cast<int>(len), 0) == XML_STATUS_ERROR) {
    Serial.printf("[%lu] [OPDS] Parse error at line %lu: %s\n", millis(), XML_GetCurrentLineNumber(parser),
                  XML_ErrorString(XML_GetErrorCode(parser)));
    return false;
  }

  return true;
}

bool OpdsParser::finishParsing() {
  if (!parser) {
    return false;
  }

  // Send final empty buffer to signal end of parsing
  if (XML_ParseBuffer(parser, 0, 1) == XML_STATUS_ERROR) {
    Serial.printf("[%lu] [OPDS] Parse error at line %lu: %s\n", millis(), XML_GetCurrentLineNumber(parser),
                  XML_ErrorString(XML_GetErrorCode(parser)));
    XML_ParserFree(parser);
    parser = nullptr;
    return false;
  }

  XML_ParserFree(parser);
  parser = nullptr;
  Serial.printf("[%lu] [OPDS] Parsed %zu entries\n", millis(), entries.size());
  return true;
}

bool OpdsParser::parse(const char* xmlData, const size_t length) {
  if (!startParsing()) {
    return false;
  }

  const char* currentPos = xmlData;
  size_t remaining = length;
  constexpr size_t chunkSize = 1024;

  while (remaining > 0) {
    const size_t toRead = remaining < chunkSize ? remaining : chunkSize;

    if (!feedChunk(currentPos, toRead)) {
      XML_ParserFree(parser);
      parser = nullptr;
      return false;
    }

    currentPos += toRead;
    remaining -= toRead;
  }

  return finishParsing();
}

void OpdsParser::clear() {
  entries.clear();
  currentEntry = OpdsEntry{};
  currentText.clear();
  searchTemplate.clear();
  openSearchUrl.clear();
  inEntry = false;
  inTitle = false;
  inAuthor = false;
  inAuthorName = false;
  inId = false;
}

std::vector<OpdsEntry> OpdsParser::getBooks() const {
  std::vector<OpdsEntry> books;
  for (const auto& entry : entries) {
    if (entry.type == OpdsEntryType::BOOK) {
      books.push_back(entry);
    }
  }
  return books;
}

const char* OpdsParser::findAttribute(const XML_Char** atts, const char* name) {
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) {
      return atts[i + 1];
    }
  }
  return nullptr;
}

void XMLCALL OpdsParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (strcmp(name, "entry") == 0 || strstr(name, ":entry") != nullptr) {
    self->inEntry = true;
    self->currentEntry = OpdsEntry{};
    return;
  }

  // Handle feed-level search links (outside entries)
  if (!self->inEntry && (strcmp(name, "link") == 0 || strstr(name, ":link") != nullptr)) {
    const char* rel = findAttribute(atts, "rel");
    const char* type = findAttribute(atts, "type");
    const char* href = findAttribute(atts, "href");

    if (rel && href && strcmp(rel, "search") == 0) {
      if (type && strstr(type, "application/atom+xml") != nullptr) {
        // Direct search template (preferred)
        self->searchTemplate = href;
        Serial.printf("[%lu] [OPDS] Found search template: %s\n", millis(), href);
      } else if (type && strstr(type, "opensearchdescription+xml") != nullptr) {
        // OpenSearch description URL (fallback)
        self->openSearchUrl = href;
        Serial.printf("[%lu] [OPDS] Found OpenSearch URL: %s\n", millis(), href);
      }
    }
    return;
  }

  if (!self->inEntry) return;

  if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
    self->inTitle = true;
    self->currentText.clear();
    return;
  }

  if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
    self->inAuthor = true;
    return;
  }

  if (self->inAuthor && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
    self->inAuthorName = true;
    self->currentText.clear();
    return;
  }

  if (strcmp(name, "id") == 0 || strstr(name, ":id") != nullptr) {
    self->inId = true;
    self->currentText.clear();
    return;
  }

  if (strcmp(name, "link") == 0 || strstr(name, ":link") != nullptr) {
    const char* rel = findAttribute(atts, "rel");
    const char* type = findAttribute(atts, "type");
    const char* href = findAttribute(atts, "href");

    if (href) {
      // Check for acquisition link (book download)
      if (rel && type && strstr(rel, "opds-spec.org/acquisition") != nullptr &&
          strcmp(type, "application/epub+zip") == 0) {
        self->currentEntry.type = OpdsEntryType::BOOK;
        self->currentEntry.href = href;
      } else if (type && strstr(type, "application/atom+xml") != nullptr) {
        // Navigation link
        if (self->currentEntry.type != OpdsEntryType::BOOK) {
          self->currentEntry.type = OpdsEntryType::NAVIGATION;
          self->currentEntry.href = href;
        }
      }
    }
  }
}

void XMLCALL OpdsParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<OpdsParser*>(userData);

  if (strcmp(name, "entry") == 0 || strstr(name, ":entry") != nullptr) {
    if (!self->currentEntry.title.empty() && !self->currentEntry.href.empty()) {
      self->entries.push_back(self->currentEntry);
    }
    self->inEntry = false;
    self->currentEntry = OpdsEntry{};
    return;
  }

  if (!self->inEntry) return;

  if (strcmp(name, "title") == 0 || strstr(name, ":title") != nullptr) {
    if (self->inTitle) {
      self->currentEntry.title = self->currentText;
    }
    self->inTitle = false;
    return;
  }

  if (strcmp(name, "author") == 0 || strstr(name, ":author") != nullptr) {
    self->inAuthor = false;
    return;
  }

  if (self->inAuthor && (strcmp(name, "name") == 0 || strstr(name, ":name") != nullptr)) {
    if (self->inAuthorName) {
      self->currentEntry.author = self->currentText;
    }
    self->inAuthorName = false;
    return;
  }

  if (strcmp(name, "id") == 0 || strstr(name, ":id") != nullptr) {
    if (self->inId) {
      self->currentEntry.id = self->currentText;
    }
    self->inId = false;
    return;
  }
}

void XMLCALL OpdsParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<OpdsParser*>(userData);
  if (self->inTitle || self->inAuthorName || self->inId) {
    self->currentText.append(s, len);
  }
}
