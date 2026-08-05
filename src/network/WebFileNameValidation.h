#pragma once

#include <cstddef>
#include <cstdint>

namespace papyrix::web {

constexpr size_t MAX_FILE_NAME_BYTES = 255;
constexpr size_t MAX_FILE_PATH_BYTES = 1023;

enum class FileNameError : uint8_t {
  None,
  Empty,
  InvalidUtf8,
  InvalidCharacter,
  HiddenName,
  TooLong,
  UnsupportedCjk,
};

bool isValidUtf8(const char* name, size_t length);
FileNameError validateFileName(const char* name, size_t length);
bool canAppendPathComponent(size_t parentLength, bool parentEndsWithSlash, size_t nameLength);
const char* fileNameErrorMessage(FileNameError error);

}  // namespace papyrix::web
