#include "CoverLinkParser.h"

#include <ExpatEncodingHandler.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#define TAG "COVER_LINK"

namespace {
constexpr char NAMESPACE_SEP = '\n';
const char XHTML_NAMESPACE[] = "http://www.w3.org/1999/xhtml";
const char SVG_NAMESPACE[] = "http://www.w3.org/2000/svg";
const char EPUB_NAMESPACE[] = "http://www.idpf.org/2007/ops";
const char XLINK_NAMESPACE[] = "http://www.w3.org/1999/xlink";

// Namespace parsing expands a bound name to "uri<NAMESPACE_SEP>local".
// A name without namespace has no separator.
bool nameMatches(const XML_Char* name, const char* nsUri, const char* localName) {
  const XML_Char* sep = strchr(name, NAMESPACE_SEP);
  if (!sep) {
    return *nsUri == '\0' && strcmp(name, localName) == 0;
  }
  const size_t nsLength = strlen(nsUri);
  return static_cast<size_t>(sep - name) == nsLength && strncmp(name, nsUri, nsLength) == 0 &&
         strcmp(sep + 1, localName) == 0;
}

bool isXhtmlName(const XML_Char* name, const char* localName) {
  return nameMatches(name, XHTML_NAMESPACE, localName) || nameMatches(name, "", localName);
}

const XML_Char* attributeValue(const XML_Char** atts, const char* nsUri, const char* localName) {
  for (int i = 0; atts[i]; i += 2) {
    if (nameMatches(atts[i], nsUri, localName)) {
      return atts[i + 1];
    }
  }
  return nullptr;
}

bool hasToken(const XML_Char* value, const char* token) {
  const size_t tokenLength = strlen(token);
  const XML_Char* p = value;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    }
    const XML_Char* start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }
    if (static_cast<size_t>(p - start) == tokenLength && strncmp(start, token, tokenLength) == 0) {
      return true;
    }
  }
  return false;
}
}  // namespace

CoverLinkParser::CoverLinkParser(const size_t xmlSize, const bool landmarks)
    : remainingSize(xmlSize), landmarksMode(landmarks) {}

CoverLinkParser::~CoverLinkParser() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);
    XML_SetElementHandler(parser, nullptr, nullptr);
    XML_SetStartDoctypeDeclHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
}

bool CoverLinkParser::setup() {
  constexpr size_t required = MAX_COVER_LINKS * sizeof(std::string) + 4096;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < required ||
      heap_caps_get_free_size(MALLOC_CAP_8BIT) < required)
    return false;
  links_.reserve(MAX_COVER_LINKS);
  parser = XML_ParserCreateNS(nullptr, NAMESPACE_SEP);
  if (!parser) {
    LOG_ERR(TAG, "Couldn't allocate memory for parser");
    return false;
  }

  XML_SetUnknownEncodingHandler(parser, expatUnknownEncodingHandler, nullptr);
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, startElement, endElement);
  XML_SetStartDoctypeDeclHandler(parser, startDoctypeDecl);
  XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_NEVER);
  return true;
}

void CoverLinkParser::fail() {
  if (parser) {
    XML_StopParser(parser, XML_FALSE);
    XML_SetElementHandler(parser, nullptr, nullptr);
    XML_SetStartDoctypeDeclHandler(parser, nullptr);
    XML_ParserFree(parser);
    parser = nullptr;
  }
  links_.clear();
  complete_ = false;
}

size_t CoverLinkParser::write(const uint8_t data) { return write(&data, 1); }

size_t CoverLinkParser::write(const uint8_t* buffer, const size_t size) {
  if (!parser || size > remainingSize) {
    fail();
    return 0;
  }

  const uint8_t* currentBufferPos = buffer;
  auto remainingInBuffer = size;

  while (remainingInBuffer > 0) {
    void* const buf = XML_GetBuffer(parser, 1024);
    if (!buf) {
      LOG_ERR(TAG, "Couldn't allocate buffer");
      fail();
      return 0;
    }

    const auto toRead = remainingInBuffer < 1024 ? remainingInBuffer : 1024;
    memcpy(buf, currentBufferPos, toRead);
    const bool isFinal = remainingSize == toRead;

    if (XML_ParseBuffer(parser, static_cast<int>(toRead), isFinal) == XML_STATUS_ERROR) {
      LOG_ERR(TAG, "Parse error at line %lu: %s", XML_GetCurrentLineNumber(parser),
              XML_ErrorString(XML_GetErrorCode(parser)));
      fail();
      return 0;
    }

    if (isFinal) {
      complete_ = true;
    }
    currentBufferPos += toRead;
    remainingInBuffer -= toRead;
    remainingSize -= toRead;
  }
  return size;
}

void CoverLinkParser::addLink(const XML_Char* value) {
  if (std::find(links_.begin(), links_.end(), value) != links_.end() || links_.size() >= MAX_COVER_LINKS) {
    return;
  }
  const size_t length = strlen(value);
  if (length == 0 || length > MAX_COVER_LINK_LENGTH) {
    return;
  }
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < length + 4096 ||
      heap_caps_get_free_size(MALLOC_CAP_8BIT) < length + 4096) {
    XML_StopParser(parser, XML_FALSE);
    return;
  }
  links_.emplace_back(value, length);
}

void XMLCALL CoverLinkParser::startDoctypeDecl(void* userData, const XML_Char* doctypeName, const XML_Char* sysid,
                                               const XML_Char* pubid, int hasInternalSubset) {
  auto* self = static_cast<CoverLinkParser*>(userData);
  (void)doctypeName;
  (void)sysid;
  (void)pubid;
  if (hasInternalSubset) {
    LOG_ERR(TAG, "Internal DTD subsets are rejected");
    XML_StopParser(self->parser, XML_FALSE);
  }
}

void XMLCALL CoverLinkParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<CoverLinkParser*>(userData);

  if (self->depth >= MAX_COVER_XML_DEPTH) {
    XML_StopParser(self->parser, XML_FALSE);
    return;
  }
  self->depth++;

  if (self->landmarksMode) {
    if (isXhtmlName(name, "nav")) {
      const XML_Char* type = attributeValue(atts, EPUB_NAMESPACE, "type");
      if (!self->insideLandmarks && type && hasToken(type, "landmarks")) {
        self->insideLandmarks = true;
        self->landmarksDepth = self->depth;
      }
      return;
    }
    if (self->insideLandmarks && isXhtmlName(name, "a")) {
      const XML_Char* type = attributeValue(atts, EPUB_NAMESPACE, "type");
      if (type && hasToken(type, "cover")) {
        const XML_Char* href = attributeValue(atts, "", "href");
        if (!href) {
          href = attributeValue(atts, XLINK_NAMESPACE, "href");
        }
        if (href) {
          self->addLink(href);
        }
      }
    }
    return;
  }

  if (isXhtmlName(name, "img")) {
    const XML_Char* src = attributeValue(atts, "", "src");
    if (src) {
      self->addLink(src);
    }
    return;
  }

  if (nameMatches(name, SVG_NAMESPACE, "image")) {
    const XML_Char* href = attributeValue(atts, "", "href");
    if (!href) {
      href = attributeValue(atts, XLINK_NAMESPACE, "href");
    }
    if (href) {
      self->addLink(href);
    }
  }
}

void XMLCALL CoverLinkParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<CoverLinkParser*>(userData);
  (void)name;

  // Only the landmarks nav itself closes at its own open depth.
  if (self->insideLandmarks && self->depth == self->landmarksDepth) {
    self->insideLandmarks = false;
  }
  if (self->depth > 0) {
    self->depth--;
  }
}
