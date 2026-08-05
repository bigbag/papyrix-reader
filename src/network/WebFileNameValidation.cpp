#include "WebFileNameValidation.h"

#include <ScriptDetector.h>

namespace papyrix::web {
namespace {

bool isContinuation(uint8_t byte) { return (byte & 0xC0) == 0x80; }

bool nextCodepoint(const char* data, size_t length, size_t& offset, uint32_t& codepoint) {
  if (!data || offset >= length) return false;

  const auto* bytes = reinterpret_cast<const uint8_t*>(data);
  const uint8_t first = bytes[offset];
  if (first < 0x80) {
    codepoint = first;
    offset++;
    return true;
  }

  size_t count = 0;
  uint32_t value = 0;
  if (first >= 0xC2 && first <= 0xDF) {
    count = 2;
    value = first & 0x1F;
  } else if (first >= 0xE0 && first <= 0xEF) {
    count = 3;
    value = first & 0x0F;
  } else if (first >= 0xF0 && first <= 0xF4) {
    count = 4;
    value = first & 0x07;
  } else {
    return false;
  }

  if (count > length - offset) return false;
  for (size_t i = 1; i < count; i++) {
    if (!isContinuation(bytes[offset + i])) return false;
    value = (value << 6) | (bytes[offset + i] & 0x3F);
  }

  if ((count == 3 && first == 0xE0 && bytes[offset + 1] < 0xA0) ||
      (count == 3 && first == 0xED && bytes[offset + 1] >= 0xA0) ||
      (count == 4 && first == 0xF0 && bytes[offset + 1] < 0x90) ||
      (count == 4 && first == 0xF4 && bytes[offset + 1] >= 0x90)) {
    return false;
  }

  codepoint = value;
  offset += count;
  return true;
}

bool isReserved(uint32_t codepoint) {
  if (codepoint < 0x20) return true;
  switch (codepoint) {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
      return true;
    default:
      return false;
  }
}

}  // namespace

bool isValidUtf8(const char* name, size_t length) {
  if (!name && length != 0) return false;
  size_t offset = 0;
  while (offset < length) {
    uint32_t codepoint = 0;
    if (!nextCodepoint(name, length, offset, codepoint)) return false;
  }
  return true;
}

FileNameError validateFileName(const char* name, size_t length) {
  if (!name || length == 0) return FileNameError::Empty;
  if (length > MAX_FILE_NAME_BYTES) return FileNameError::TooLong;
  if (!isValidUtf8(name, length)) return FileNameError::InvalidUtf8;
  if (name[0] == '.') return FileNameError::HiddenName;

  size_t offset = 0;
  while (offset < length) {
    uint32_t codepoint = 0;
    if (!nextCodepoint(name, length, offset, codepoint)) return FileNameError::InvalidUtf8;
    if (isReserved(codepoint)) return FileNameError::InvalidCharacter;
    if (ScriptDetector::isCjkCodepoint(codepoint)) return FileNameError::UnsupportedCjk;
  }
  return FileNameError::None;
}

bool canAppendPathComponent(size_t parentLength, bool parentEndsWithSlash, size_t nameLength) {
  if (parentLength > MAX_FILE_PATH_BYTES) return false;
  const size_t separatorLength = parentEndsWithSlash ? 0 : 1;
  const size_t remaining = MAX_FILE_PATH_BYTES - parentLength;
  return separatorLength <= remaining && nameLength <= remaining - separatorLength;
}

const char* fileNameErrorMessage(FileNameError error) {
  switch (error) {
    case FileNameError::None:
      return "";
    case FileNameError::Empty:
      return "Name cannot be empty";
    case FileNameError::InvalidUtf8:
      return "Invalid UTF-8 in name";
    case FileNameError::InvalidCharacter:
      return "Invalid characters in name";
    case FileNameError::HiddenName:
      return "Hidden names are not supported";
    case FileNameError::TooLong:
      return "Name is too long";
    case FileNameError::UnsupportedCjk:
      return "CJK filenames are not supported";
  }
  return "Invalid name";
}

}  // namespace papyrix::web
