#include "Html5Normalizer.h"

#include <SDCardManager.h>

#include <algorithm>
#include <cctype>

namespace html5 {

namespace {

// HTML5 void elements that cannot have closing tags (lowercase for case-insensitive matching)
constexpr const char* VOID_ELEMENTS[] = {"img",  "br",  "hr",    "input", "meta",   "link",  "area",
                                         "base", "col", "embed", "param", "source", "track", "wbr"};
constexpr size_t VOID_ELEMENT_COUNT = sizeof(VOID_ELEMENTS) / sizeof(VOID_ELEMENTS[0]);
constexpr size_t MAX_TAG_NAME_LENGTH = 8;
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

}  // namespace

bool normalizeVoidElements(const std::string& inputPath, const std::string& outputPath) {
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
  char closingTagWhitespace[8] = {0};  // Buffer for whitespace in closing tags
  size_t closingTagWsLen = 0;
  bool isCurrentTagVoid = false;
  char quoteChar = 0;
  char prevChar = 0;

  uint8_t readBuffer[BUFFER_SIZE];
  uint8_t writeBuffer[BUFFER_SIZE + 64];  // Extra space for insertions
  size_t writePos = 0;

  auto flushWrite = [&]() -> bool {
    if (writePos > 0) {
      if (outFile.write(writeBuffer, writePos) != writePos) {
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

  while (inFile.available()) {
    int bytesRead = inFile.read(readBuffer, BUFFER_SIZE);
    if (bytesRead <= 0) break;

    for (int i = 0; i < bytesRead; i++) {
      char c = static_cast<char>(readBuffer[i]);

      switch (state) {
        case State::Normal:
          if (c == '<') {
            state = State::InTagStart;
            tagNameLen = 0;
            isCurrentTagVoid = false;
            // Don't write '<' yet - might need to skip if it's a void element closing tag
          } else {
            if (!writeChar(c)) goto error;
          }
          break;

        case State::InTagStart:
          if (c == '/') {
            // Closing tag - need to check if it's a void element
            state = State::InClosingTagName;
            tagNameLen = 0;
            closingTagWsLen = 0;
            // Don't write '</' yet - buffer it in case we need to skip
          } else if (c == '!' || c == '?') {
            // Comment or processing instruction - skip normalization
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
            // End of tag name
            tagName[tagNameLen] = '\0';
            isCurrentTagVoid = isVoidElement(tagName, tagNameLen);

            if (c == '>') {
              // Tag ends immediately after name
              if (isCurrentTagVoid && prevChar != '/') {
                if (!writeChar(' ')) goto error;
                if (!writeChar('/')) goto error;
              }
              if (!writeChar(c)) goto error;
              state = State::Normal;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
              state = State::InTagAttrs;
              if (!writeChar(c)) goto error;
            } else if (c == '/') {
              // Self-closing indicator
              if (!writeChar(c)) goto error;
              state = State::InTagAttrs;
            } else {
              // Unexpected character
              if (!writeChar(c)) goto error;
              state = State::Normal;
            }
          }
          break;

        case State::InTagAttrs:
          if (c == '"' || c == '\'') {
            state = State::InQuote;
            quoteChar = c;
            if (!writeChar(c)) goto error;
          } else if (c == '>') {
            // End of tag - insert self-closing if needed
            if (isCurrentTagVoid && prevChar != '/') {
              if (!writeChar(' ')) goto error;
              if (!writeChar('/')) goto error;
            }
            if (!writeChar(c)) goto error;
            state = State::Normal;
          } else {
            if (!writeChar(c)) goto error;
          }
          break;

        case State::InQuote:
          if (c == quoteChar) {
            state = State::InTagAttrs;
          }
          if (!writeChar(c)) goto error;
          break;

        case State::InClosingTagName:
          if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == ':') {
            if (tagNameLen < MAX_TAG_NAME_LENGTH) {
              tagName[tagNameLen++] = c;
            } else {
              // Tag too long to be void - flush buffer and passthrough
              if (!writeChar('<')) goto error;
              if (!writeChar('/')) goto error;
              for (size_t j = 0; j < tagNameLen; j++) {
                if (!writeChar(tagName[j])) goto error;
              }
              if (!writeChar(c)) goto error;
              state = State::InClosingTagRest;
            }
          } else if (c == '>') {
            // End of closing tag - check if it's a void element
            tagName[tagNameLen] = '\0';
            if (isVoidElement(tagName, tagNameLen)) {
              // Skip the entire closing tag (don't output anything)
            } else {
              // Not a void element - output the buffered "</tagname>" with any whitespace
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
            // Whitespace before '>' in closing tag (unusual but valid)
            // Buffer it in case we need to replay for non-void elements
            if (closingTagWsLen < sizeof(closingTagWhitespace)) {
              closingTagWhitespace[closingTagWsLen++] = c;
            }
          } else {
            // Unexpected character - output what we have and return to normal
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

  // Handle EOF - flush any buffered but uncommitted content
  if (state == State::InTagStart) {
    // We saw '<' but nothing else
    if (!writeChar('<')) goto error;
  } else if (state == State::InClosingTagName) {
    // We were in the middle of a closing tag - output what we have
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
