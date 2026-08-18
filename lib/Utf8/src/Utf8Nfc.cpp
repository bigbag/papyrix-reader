#include "Utf8Nfc.h"

#include <esp_heap_caps.h>

#include <cstring>
#include <new>

#include "Utf8NfcTable.h"

namespace {

// Decode one UTF-8 codepoint from buf at position pos.
// Advances pos past the codepoint. Malformed bytes become '?' so in-place
// normalization can never expand beyond the caller-provided buffer.
uint32_t decodeUtf8(const char* buf, size_t len, size_t& pos) {
  if (pos >= len) return 0;
  auto c = static_cast<unsigned char>(buf[pos]);
  if (c < 0x80) {
    pos++;
    return c;
  }
  int bytes;
  uint32_t cp;
  if (c >= 0xC2 && c <= 0xDF) {
    bytes = 2;
    cp = c & 0x1F;
  } else if (c >= 0xE0 && c <= 0xEF) {
    bytes = 3;
    cp = c & 0x0F;
  } else if (c >= 0xF0 && c <= 0xF4) {
    bytes = 4;
    cp = c & 0x07;
  } else {
    pos++;
    return '?';
  }
  if (pos + bytes > len) {
    pos++;
    return '?';
  }
  for (int i = 1; i < bytes; i++) {
    auto cont = static_cast<unsigned char>(buf[pos + i]);
    if ((cont & 0xC0) != 0x80) {
      pos++;
      return '?';
    }
    cp = (cp << 6) | (cont & 0x3F);
  }
  const auto second = static_cast<unsigned char>(buf[pos + 1]);
  if ((bytes == 3 && ((c == 0xE0 && second < 0xA0) || (c == 0xED && second >= 0xA0))) ||
      (bytes == 4 && ((c == 0xF0 && second < 0x90) || (c == 0xF4 && second >= 0x90)))) {
    pos++;
    return '?';
  }
  pos += bytes;
  return cp;
}

// Encode one codepoint as UTF-8 into buf at position pos.
// Returns bytes written.
int encodeUtf8(char* buf, uint32_t cp) {
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  buf[0] = static_cast<char>(0xF0 | (cp >> 18));
  buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

// Binary search the NFC composition table for (base, combining) → result.
// Returns the composed codepoint, or 0 if not found.
uint32_t lookupComposition(uint32_t base, uint32_t combining) {
  if (base > 0xFFFF || combining > 0xFFFF) return 0;

  auto b = static_cast<uint16_t>(base);
  auto c = static_cast<uint16_t>(combining);

  int lo = 0;
  int hi = static_cast<int>(NFC_TABLE_SIZE) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const auto& entry = NFC_TABLE[mid];
    if (entry.base < b || (entry.base == b && entry.combining < c)) {
      lo = mid + 1;
    } else if (entry.base > b || (entry.base == b && entry.combining > c)) {
      hi = mid - 1;
    } else {
      return entry.result;
    }
  }
  return 0;
}

// Check if a codepoint is a combining mark (general category M).
// We only need to handle the combining marks that appear in our table.
bool isCombiningMark(uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) ||  // Combining Diacritical Marks
         (cp >= 0x0483 && cp <= 0x0489) ||  // Cyrillic combining marks
         (cp >= 0x1DC0 && cp <= 0x1DFF) ||  // Combining Diacritical Marks Supplement
         (cp >= 0x20D0 && cp <= 0x20FF);    // Combining Diacritical Marks for Symbols
}

}  // namespace

size_t utf8NormalizeNfc(char* buf, size_t len) {
  // Fast path: if all bytes are ASCII, no normalization needed
  bool allAscii = true;
  for (size_t i = 0; i < len; i++) {
    if (static_cast<unsigned char>(buf[i]) >= 0x80) {
      allAscii = false;
      break;
    }
  }
  if (allAscii) return len;

  // Decode to codepoints (max codepoints = len, since each UTF-8 char >= 1 byte)
  // Use stack buffer for small strings, heap for large ones.
  // NFC is cosmetic: on OOM leave valid UTF-8 untouched rather than abort.
  constexpr size_t STACK_SIZE = 64;
  uint32_t stackBuf[STACK_SIZE];
  if (len > SIZE_MAX / sizeof(uint32_t)) return len;
  const size_t allocationBytes = len * sizeof(uint32_t);
  if (len > STACK_SIZE && allocationBytes > heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) * 80 / 100) return len;
  uint32_t* cps = len <= STACK_SIZE ? stackBuf : new (std::nothrow) uint32_t[len];
  if (cps == nullptr) return len;

  size_t cpCount = 0;
  size_t pos = 0;
  while (pos < len) {
    uint32_t cp = decodeUtf8(buf, len, pos);
    if (cp == 0) break;
    cps[cpCount++] = cp;
  }

  // Iterative canonical composition
  // Walk through codepoints: try to compose each combining mark with preceding base
  size_t outCount = 0;
  for (size_t i = 0; i < cpCount; i++) {
    if (outCount > 0 && isCombiningMark(cps[i])) {
      uint32_t composed = lookupComposition(cps[outCount - 1], cps[i]);
      if (composed != 0) {
        cps[outCount - 1] = composed;
        continue;
      }
    }
    cps[outCount++] = cps[i];
  }

  // Re-encode to UTF-8 (composed output is always <= original length)
  size_t writePos = 0;
  for (size_t i = 0; i < outCount; i++) {
    writePos += encodeUtf8(buf + writePos, cps[i]);
  }
  buf[writePos] = '\0';

  if (cps != stackBuf) delete[] cps;
  return writePos;
}
