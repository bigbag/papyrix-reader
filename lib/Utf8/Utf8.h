#pragma once

#include <cstdint>
#include <string>

#include "Utf8Nfc.h"

uint32_t utf8NextCodepoint(const unsigned char** string);

inline bool utf8IsCombiningMark(const uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) ||  // Combining Diacritical Marks
         (cp >= 0x1DC0 && cp <= 0x1DFF) ||  // Supplement
         (cp >= 0x20D0 && cp <= 0x20FF) ||  // For Symbols
         (cp >= 0xFE20 && cp <= 0xFE2F);    // Half Marks
}

/**
 * UTF-8 safe string truncation - removes one character from the end.
 * Returns the new size after removing one UTF-8 character.
 */
size_t utf8RemoveLastChar(std::string& str);

/**
 * UTF-8 safe truncation - removes N characters from the end.
 */
void utf8TruncateChars(std::string& str, size_t numChars);
