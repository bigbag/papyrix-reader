#include "Utf8.h"

#include <cstring>

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

IRAM_ATTR int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;          // 0xxxxxxx
  if ((c >> 5) == 0x6) return 2;   // 110xxxxx
  if ((c >> 4) == 0xE) return 3;   // 1110xxxx
  if ((c >> 3) == 0x1E) return 4;  // 11110xxx
  return 1;                        // fallback for invalid
}

IRAM_ATTR uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const int bytes = utf8CodepointLen(**string);
  const uint8_t* chr = *string;
  *string += bytes;

  if (bytes == 1) {
    return chr[0];
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);  // mask header bits

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  return cp;
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  // Walk back to find the start of the last UTF-8 character
  // UTF-8 continuation bytes start with 10xxxxxx (0x80-0xBF)
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

void utf8TruncateChars(std::string& str, size_t numChars) {
  if (str.empty() || numChars == 0) return;

  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(str.c_str());
  size_t totalCp = 0;
  while (*ptr) {
    utf8NextCodepoint(&ptr);
    totalCp++;
  }

  if (numChars >= totalCp) {
    str.clear();
    return;
  }

  const size_t keepCp = totalCp - numChars;
  ptr = reinterpret_cast<const unsigned char*>(str.c_str());
  for (size_t i = 0; i < keepCp; i++) {
    utf8NextCodepoint(&ptr);
  }
  str.resize(static_cast<size_t>(ptr - reinterpret_cast<const unsigned char*>(str.c_str())));
}

size_t utf8SafeCopy(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return 0;
  if (!src) {
    dst[0] = '\0';
    return 0;
  }
  size_t maxCopy = dstSize - 1;
  size_t srcLen = 0;
  while (srcLen < maxCopy && src[srcLen] != '\0') {
    ++srcLen;
  }
  if (srcLen == maxCopy && src[srcLen] != '\0') {
    while (srcLen > 0 && (static_cast<unsigned char>(src[srcLen]) & 0xC0) == 0x80) {
      --srcLen;
    }
  }
  if (srcLen > 0) {
    memcpy(dst, src, srcLen);
  }
  dst[srcLen] = '\0';
  return srcLen;
}
