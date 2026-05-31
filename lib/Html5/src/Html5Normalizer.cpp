#include "Html5Normalizer.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <new>

namespace html5 {

namespace {

constexpr const char* VOID_ELEMENTS[] = {"img",  "br",  "hr",    "input", "meta",   "link",  "area",
                                         "base", "col", "embed", "param", "source", "track", "wbr"};
constexpr size_t VOID_ELEMENT_COUNT = sizeof(VOID_ELEMENTS) / sizeof(VOID_ELEMENTS[0]);
constexpr size_t MAX_TAG_NAME_LENGTH = 8;
constexpr size_t MAX_ATTR_NAME_LENGTH = 32;
constexpr size_t BUFFER_SIZE = 1024;

enum class State { Normal, InTagStart, InTagName, InTagAttrs, InQuote, InClosingTagName, InClosingTagRest };

char toLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c; }

bool isVoidElement(const char* name, size_t len) {
  for (size_t i = 0; i < VOID_ELEMENT_COUNT; i++) {
    const char* ve = VOID_ELEMENTS[i];
    size_t veLen = 0;
    while (ve[veLen] != '\0') veLen++;
    if (len == veLen) {
      bool match = true;
      for (size_t j = 0; j < len && match; j++) {
        if (toLowerAscii(name[j]) != ve[j]) match = false;
      }
      if (match) return true;
    }
  }
  return false;
}

bool isAttrNameStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }

bool isAttrNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == ':' || c == '.' || c == '_';
}

}  // namespace

bool normalizeHtmlForXml(const std::string& inputPath, const std::string& outputPath) {
  FsFile inFile, outFile;

  if (!SdMan.openFileForRead("H5N", inputPath, inFile)) {
    return false;
  }

  if (!SdMan.openFileForWrite("H5N", outputPath, outFile)) {
    inFile.close();
    return false;
  }

  State state = State::Normal;
  char tagName[MAX_TAG_NAME_LENGTH + 1] = {0};
  size_t tagNameLen = 0;
  char closingTagWhitespace[8] = {0};
  size_t closingTagWsLen = 0;
  bool isCurrentTagVoid = false;
  char quoteChar = 0;
  char prevChar = 0;

  char attrNameBuf[MAX_ATTR_NAME_LENGTH + 1] = {0};
  size_t attrNameLen = 0;
  bool inAttrName = false;

  // Keep these ~2 KB scratch buffers off the stack: normalization runs on the
  // foreground loopTask (8 KB stack) when an HTML/EPUB page isn't cached (Issue #137).
  auto readBuffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[BUFFER_SIZE]);
  auto writeBuffer = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[BUFFER_SIZE + 128]);
  if (!readBuffer || !writeBuffer) {
    inFile.close();
    outFile.close();
    return false;
  }
  size_t writePos = 0;

  auto flushWrite = [&]() -> bool {
    if (writePos > 0) {
      if (outFile.write(writeBuffer.get(), writePos) != writePos) {
        return false;
      }
      writePos = 0;
    }
    return true;
  };

  auto writeChar = [&](char c) -> bool {
    writeBuffer[writePos++] = static_cast<uint8_t>(c);
    if (writePos >= BUFFER_SIZE) {
      return flushWrite();
    }
    return true;
  };

  auto flushBareAttr = [&]() -> bool {
    for (size_t j = 0; j < attrNameLen; j++) {
      if (!writeChar(attrNameBuf[j])) return false;
    }
    if (!writeChar('=')) return false;
    if (!writeChar('"')) return false;
    if (!writeChar('"')) return false;
    attrNameLen = 0;
    inAttrName = false;
    return true;
  };

  auto flushAttrNameRaw = [&]() -> bool {
    for (size_t j = 0; j < attrNameLen; j++) {
      if (!writeChar(attrNameBuf[j])) return false;
    }
    attrNameLen = 0;
    inAttrName = false;
    return true;
  };

  auto closeCurrentTag = [&]() -> bool {
    if (isCurrentTagVoid && prevChar != '/') {
      if (!writeChar(' ')) return false;
      if (!writeChar('/')) return false;
    }
    return writeChar('>');
  };

  while (inFile.available()) {
    int bytesRead = inFile.read(readBuffer.get(), BUFFER_SIZE);
    if (bytesRead <= 0) break;

    for (int i = 0; i < bytesRead; i++) {
      char c = static_cast<char>(readBuffer[i]);

      switch (state) {
        case State::Normal:
          if (c == '<') {
            state = State::InTagStart;
            tagNameLen = 0;
            isCurrentTagVoid = false;
          } else {
            if (!writeChar(c)) goto error;
          }
          break;

        case State::InTagStart:
          if (c == '/') {
            state = State::InClosingTagName;
            tagNameLen = 0;
            closingTagWsLen = 0;
          } else if (c == '!' || c == '?') {
            state = State::Normal;
            if (!writeChar('<')) goto error;
            if (!writeChar(c)) goto error;
          } else if (std::isalpha(static_cast<unsigned char>(c))) {
            state = State::InTagName;
            tagName[0] = c;
            tagNameLen = 1;
            if (!writeChar('<')) goto error;
            if (!writeChar(c)) goto error;
          } else {
            state = State::Normal;
            if (!writeChar('<')) goto error;
            if (!writeChar(c)) goto error;
          }
          break;

        case State::InTagName:
          if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == ':') {
            if (tagNameLen < MAX_TAG_NAME_LENGTH) {
              tagName[tagNameLen++] = c;
            }
            if (!writeChar(c)) goto error;
          } else {
            tagName[tagNameLen] = '\0';
            isCurrentTagVoid = isVoidElement(tagName, tagNameLen);

            if (c == '>') {
              if (isCurrentTagVoid && prevChar != '/') {
                if (!writeChar(' ')) goto error;
                if (!writeChar('/')) goto error;
              }
              if (!writeChar(c)) goto error;
              state = State::Normal;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
              state = State::InTagAttrs;
              inAttrName = false;
              attrNameLen = 0;
              if (!writeChar(c)) goto error;
            } else if (c == '/') {
              if (!writeChar(c)) goto error;
              state = State::InTagAttrs;
              inAttrName = false;
              attrNameLen = 0;
            } else {
              if (!writeChar(c)) goto error;
              state = State::Normal;
            }
          }
          break;

        case State::InTagAttrs:
          if (c == '"' || c == '\'') {
            if (inAttrName) {
              if (!flushBareAttr()) goto error;
            }
            state = State::InQuote;
            quoteChar = c;
            if (!writeChar(c)) goto error;
          } else if (c == '=') {
            if (inAttrName) {
              if (!flushAttrNameRaw()) goto error;
            }
            if (!writeChar(c)) goto error;
          } else if (c == '>') {
            if (inAttrName) {
              if (!flushBareAttr()) goto error;
            }
            if (!closeCurrentTag()) goto error;
            state = State::Normal;
          } else if (c == '<') {
            if (inAttrName) {
              if (!flushBareAttr()) goto error;
            }
            if (!closeCurrentTag()) goto error;
            state = State::InTagStart;
            tagNameLen = 0;
            isCurrentTagVoid = false;
          } else if (isAttrNameStart(c) && !inAttrName) {
            if (prevChar == '"' || prevChar == '\'') {
              if (!writeChar(' ')) goto error;
            }
            inAttrName = true;
            attrNameLen = 0;
            attrNameBuf[attrNameLen++] = c;
          } else if (inAttrName && isAttrNameChar(c)) {
            if (attrNameLen < MAX_ATTR_NAME_LENGTH) {
              attrNameBuf[attrNameLen++] = c;
            }
          } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (inAttrName) {
              if (!flushBareAttr()) goto error;
            }
            if (!writeChar(c)) goto error;
          } else {
            if (inAttrName) {
              if (!flushAttrNameRaw()) goto error;
            }
            if (!writeChar(c)) goto error;
          }
          break;

        case State::InQuote:
          if (c == quoteChar) {
            state = State::InTagAttrs;
            inAttrName = false;
            attrNameLen = 0;
            if (!writeChar(c)) goto error;
          } else if (c == '<') {
            if (!writeChar('&')) goto error;
            if (!writeChar('l')) goto error;
            if (!writeChar('t')) goto error;
            if (!writeChar(';')) goto error;
          } else {
            if (!writeChar(c)) goto error;
          }
          break;

        case State::InClosingTagName:
          if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == ':') {
            if (tagNameLen < MAX_TAG_NAME_LENGTH) {
              tagName[tagNameLen++] = c;
            } else {
              if (!writeChar('<')) goto error;
              if (!writeChar('/')) goto error;
              for (size_t j = 0; j < tagNameLen; j++) {
                if (!writeChar(tagName[j])) goto error;
              }
              if (!writeChar(c)) goto error;
              state = State::InClosingTagRest;
            }
          } else if (c == '>') {
            tagName[tagNameLen] = '\0';
            if (isVoidElement(tagName, tagNameLen)) {
              // Skip closing tag for void elements
            } else {
              if (!writeChar('<')) goto error;
              if (!writeChar('/')) goto error;
              for (size_t j = 0; j < tagNameLen; j++) {
                if (!writeChar(tagName[j])) goto error;
              }
              for (size_t j = 0; j < closingTagWsLen; j++) {
                if (!writeChar(closingTagWhitespace[j])) goto error;
              }
              if (!writeChar('>')) goto error;
            }
            state = State::Normal;
          } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (closingTagWsLen < sizeof(closingTagWhitespace)) {
              closingTagWhitespace[closingTagWsLen++] = c;
            }
          } else {
            if (!writeChar('<')) goto error;
            if (!writeChar('/')) goto error;
            for (size_t j = 0; j < tagNameLen; j++) {
              if (!writeChar(tagName[j])) goto error;
            }
            if (!writeChar(c)) goto error;
            state = State::Normal;
          }
          break;

        case State::InClosingTagRest:
          if (!writeChar(c)) goto error;
          if (c == '>') {
            state = State::Normal;
          }
          break;
      }

      prevChar = c;
    }
  }

  if (state == State::InTagAttrs || state == State::InTagName) {
    if (inAttrName) {
      if (!flushBareAttr()) goto error;
    }
    if (!closeCurrentTag()) goto error;
  } else if (state == State::InQuote) {
    if (!writeChar(quoteChar)) goto error;
    if (!closeCurrentTag()) goto error;
  } else if (state == State::InTagStart) {
    if (!writeChar('<')) goto error;
  } else if (state == State::InClosingTagName) {
    if (!writeChar('<')) goto error;
    if (!writeChar('/')) goto error;
    for (size_t j = 0; j < tagNameLen; j++) {
      if (!writeChar(tagName[j])) goto error;
    }
    for (size_t j = 0; j < closingTagWsLen; j++) {
      if (!writeChar(closingTagWhitespace[j])) goto error;
    }
  }

  if (!flushWrite()) goto error;

  inFile.close();
  outFile.close();
  return true;

error:
  inFile.close();
  outFile.close();
  SdMan.remove(outputPath.c_str());
  return false;
}

}  // namespace html5
