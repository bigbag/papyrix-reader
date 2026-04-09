#pragma once

#include <cstddef>
#include <cstdint>

#include "EncodingTables.h"

enum class Encoding : uint8_t { Utf8 = 0, Windows1251, Koi8R, Iso8859_1, Cp1252 };

inline int codepointToUtf8(int cp, char* out) {
  if (cp < 0x80) {
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  out[0] = static_cast<char>(0xE0 | (cp >> 12));
  out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[2] = static_cast<char>(0x80 | (cp & 0x3F));
  return 3;
}

inline const int* getEncodingTable(Encoding enc) {
  switch (enc) {
    case Encoding::Windows1251:
      return kWindows1251High;
    case Encoding::Koi8R:
      return kKoi8RHigh;
    case Encoding::Iso8859_1:
      return kIso8859_1High;
    case Encoding::Cp1252:
      return kCp1252High;
    default:
      return nullptr;
  }
}

inline Encoding detectEncoding(const uint8_t* data, size_t len, size_t& bomBytes) {
  bomBytes = 0;

  if (len == 0) return Encoding::Utf8;

  // Check for UTF-8 BOM
  if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
    bomBytes = 3;
    return Encoding::Utf8;
  }

  // Validate UTF-8: walk bytes checking multi-byte sequences
  bool hasHighBytes = false;
  bool validUtf8 = true;
  size_t i = 0;

  while (i < len) {
    uint8_t b = data[i];
    if (b < 0x80) {
      i++;
      continue;
    }

    hasHighBytes = true;
    int expected;

    if ((b & 0xE0) == 0xC0) {
      expected = 1;
      if (b < 0xC2) {
        validUtf8 = false;
        break;
      }
    } else if ((b & 0xF0) == 0xE0) {
      expected = 2;
    } else if ((b & 0xF8) == 0xF0) {
      expected = 3;
      if (b > 0xF4) {
        validUtf8 = false;
        break;
      }
    } else {
      validUtf8 = false;
      break;
    }

    if (i + expected >= len) break;

    for (int j = 1; j <= expected; j++) {
      if ((data[i + j] & 0xC0) != 0x80) {
        validUtf8 = false;
        break;
      }
    }
    if (!validUtf8) break;
    i += 1 + expected;
  }

  if (validUtf8) return Encoding::Utf8;

  // Not valid UTF-8 — use byte frequency heuristic to distinguish encodings
  // Count bytes in Cyrillic-specific ranges
  int cyrillicCp1251 = 0;   // CP1251 Cyrillic: 0xC0-0xFF
  int cyrillicKoi8r = 0;    // KOI8-R Cyrillic: 0xC0-0xFF
  int highNonCyrillic = 0;  // bytes 0x80-0xBF (non-Cyrillic in both encodings)

  for (size_t j = 0; j < len; j++) {
    uint8_t b = data[j];
    if (b < 0x80) continue;

    if (b >= 0xC0) {
      // Both CP1251 and KOI8-R have Cyrillic in 0xC0-0xFF
      // Discriminator: CP1251 uppercase = 0xC0-0xDF, lowercase = 0xE0-0xFF
      //                KOI8-R lowercase = 0xC0-0xDF, uppercase = 0xE0-0xFF
      // In natural text, lowercase >> uppercase
      // So CP1251 has more bytes in 0xE0-0xFF, KOI8-R has more in 0xC0-0xDF
      if (b >= 0xE0)
        cyrillicCp1251++;
      else
        cyrillicKoi8r++;
    } else {
      highNonCyrillic++;
    }
  }

  int totalCyrillic = cyrillicCp1251 + cyrillicKoi8r;

  if (totalCyrillic > 0) {
    // Significant Cyrillic content — distinguish CP1251 vs KOI8-R
    // In CP1251 natural Russian text: lowercase (0xE0-0xFF) dominates
    // In KOI8-R natural Russian text: lowercase (0xC0-0xDF) dominates
    if (cyrillicCp1251 > cyrillicKoi8r) {
      return Encoding::Windows1251;
    }
    return Encoding::Koi8R;
  }

  // No Cyrillic patterns — likely Western European
  if (hasHighBytes) return Encoding::Cp1252;

  return Encoding::Utf8;
}
