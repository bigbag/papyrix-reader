#include "ScriptDetector.h"

#include <Utf8.h>

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace ScriptDetector {

IRAM_ATTR bool isCjkCodepoint(uint32_t cp) {
  // CJK Unified Ideographs
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
  // CJK Extension A
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;
  // CJK Compatibility Ideographs
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;
  // Hiragana
  if (cp >= 0x3040 && cp <= 0x309F) return true;
  // Katakana
  if (cp >= 0x30A0 && cp <= 0x30FF) return true;
  // Hangul Syllables
  if (cp >= 0xAC00 && cp <= 0xD7AF) return true;
  // CJK Extension B and beyond (Plane 2)
  if (cp >= 0x20000 && cp <= 0x2A6DF) return true;
  // Fullwidth ASCII variants (often used in CJK context)
  if (cp >= 0xFF00 && cp <= 0xFFEF) return true;
  return false;
}

bool containsThai(const char* text) {
  if (text == nullptr) return false;

  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
  uint32_t cp;

  while ((cp = utf8NextCodepoint(&ptr))) {
    if (isThaiCodepoint(cp)) {
      return true;
    }
  }
  return false;
}

bool containsArabic(const char* text) {
  if (text == nullptr) return false;

  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
  uint32_t cp;

  while ((cp = utf8NextCodepoint(&ptr))) {
    if (isArabicCodepoint(cp)) {
      return true;
    }
  }
  return false;
}

bool containsCjk(const char* text) {
  if (text == nullptr) return false;

  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(text);
  uint32_t cp;

  while ((cp = utf8NextCodepoint(&ptr))) {
    if (isCjkCodepoint(cp)) {
      return true;
    }
  }
  return false;
}

Script classify(const char* word) {
  if (word == nullptr || *word == '\0') {
    return Script::OTHER;
  }

  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word);
  uint32_t cp;

  while ((cp = utf8NextCodepoint(&ptr))) {
    // Skip ASCII - continue to find first non-ASCII character
    if (cp < 0x80) {
      continue;
    }

    // Check Thai first (smaller range, fast check)
    if (isThaiCodepoint(cp)) {
      return Script::THAI;
    }

    // Check Arabic
    if (isArabicCodepoint(cp)) {
      return Script::ARABIC;
    }

    // Check CJK ranges
    if (isCjkCodepoint(cp)) {
      return Script::CJK;
    }

    // Extended Latin, Cyrillic, Greek, etc. - treat as Latin
    // Latin Extended: U+0080-U+024F
    // Latin Extended Additional: U+1E00-U+1EFF
    // Cyrillic: U+0400-U+04FF
    // Greek: U+0370-U+03FF
    if ((cp >= 0x0080 && cp <= 0x024F) ||  // Latin Extended
        (cp >= 0x1E00 && cp <= 0x1EFF) ||  // Latin Extended Additional
        (cp >= 0x0400 && cp <= 0x04FF) ||  // Cyrillic
        (cp >= 0x0370 && cp <= 0x03FF)) {  // Greek
      return Script::LATIN;
    }

    // Unknown non-ASCII - classify as OTHER
    return Script::OTHER;
  }

  // All ASCII - classify as LATIN
  return Script::LATIN;
}

}  // namespace ScriptDetector
