#pragma once

#include <cstring>

// Strips data URIs from HTML buffer before XML parsing to prevent expat OOM.
// Data URIs like src="data:image/jpeg;base64,..." are replaced with src="#"
// which is small enough for expat to handle safely.
//
// Note: Does not handle whitespace around '=' (e.g., src = "data:...").
// Such cases are caught by the fallback check in cacheImage().
class DataUriStripper {
 public:
  // Strip data URIs from buffer in-place. Returns new length.
  // Returns 0 if buffer is empty/null OR if entire buffer was data URI content
  // being skipped (safe to pass to XML_ParseBuffer which handles 0-length).
  //
  // @param buf        Buffer to process in-place
  // @param len        Length of data in buffer
  // @param bufCapacity Total capacity of buffer (must be >= len)
  size_t strip(char* buf, size_t len, size_t bufCapacity) {
    if (!buf || len == 0) return 0;
    if (bufCapacity < len) return 0;  // Invalid: capacity less than data length

    size_t writePos = 0;
    size_t readPos = 0;
    size_t lastReplacementEnd = 0;

    // Prepend any partial match from previous buffer (only if it fits safely)
    // Use subtraction to avoid integer overflow: len <= bufCapacity - partialLen_
    if (partialLen_ > 0 && partialLen_ <= bufCapacity && len <= bufCapacity - partialLen_) {
      memmove(buf + partialLen_, buf, len);
      memcpy(buf, partialBuf_, partialLen_);
      len += partialLen_;
      partialLen_ = 0;
    }

    // If we're in the middle of skipping a data URI from a previous buffer, continue skipping
    if (skippingDataUri_) {
      while (readPos < len && buf[readPos] != skipUntilQuote_) {
        readPos++;
      }
      if (readPos < len) {
        readPos++;  // Skip the closing quote
        skippingDataUri_ = false;
      } else {
        return 0;  // Entire buffer is data URI content, skip it all
      }
    }

    while (readPos < len) {
      // Look for src="data: or src='data:' (case insensitive for 'src' and 'data')
      if (readPos + 9 < len && (buf[readPos] == 's' || buf[readPos] == 'S') &&
          (buf[readPos + 1] == 'r' || buf[readPos + 1] == 'R') &&
          (buf[readPos + 2] == 'c' || buf[readPos + 2] == 'C') && buf[readPos + 3] == '=') {
        char quote = buf[readPos + 4];
        if (quote == '"' || quote == '\'') {
          if (readPos + 9 < len && (buf[readPos + 5] == 'd' || buf[readPos + 5] == 'D') &&
              (buf[readPos + 6] == 'a' || buf[readPos + 6] == 'A') &&
              (buf[readPos + 7] == 't' || buf[readPos + 7] == 'T') &&
              (buf[readPos + 8] == 'a' || buf[readPos + 8] == 'A') && buf[readPos + 9] == ':') {
            // Found src="data: - replace with src="#"
            buf[writePos++] = buf[readPos];      // s
            buf[writePos++] = buf[readPos + 1];  // r
            buf[writePos++] = buf[readPos + 2];  // c
            buf[writePos++] = '=';
            buf[writePos++] = quote;
            buf[writePos++] = '#';
            buf[writePos++] = quote;
            lastReplacementEnd = writePos;

            // Skip past the data URI content until closing quote
            readPos += 10;
            while (readPos < len && buf[readPos] != quote) {
              readPos++;
            }
            if (readPos < len) {
              readPos++;  // Skip the closing quote
            } else {
              // Data URI spans to next buffer - track state
              skippingDataUri_ = true;
              skipUntilQuote_ = quote;
            }
            continue;
          }
        }
      }

      buf[writePos++] = buf[readPos++];
    }

    // Check if we ended in the middle of a 'src="data:' pattern.
    // Only check content AFTER the last replacement to avoid detecting 'SRC="#"' as partial.
    if (!skippingDataUri_ && writePos >= 10) {
      // Start checking from after the last replacement, or at most 9 bytes from end
      size_t checkStart = writePos > 9 ? writePos - 9 : 0;
      if (lastReplacementEnd > checkStart) {
        checkStart = lastReplacementEnd;
      }

      for (size_t i = checkStart; i < writePos; i++) {
        if ((buf[i] == 's' || buf[i] == 'S') && i + 1 < writePos && (buf[i + 1] == 'r' || buf[i + 1] == 'R')) {
          partialLen_ = writePos - i;
          if (partialLen_ > sizeof(partialBuf_) - 1) {
            partialLen_ = sizeof(partialBuf_) - 1;
          }
          memcpy(partialBuf_, buf + i, partialLen_);
          writePos = i;
          break;
        }
        if ((buf[i] == 's' || buf[i] == 'S') && i + 1 >= writePos) {
          partialLen_ = 1;
          partialBuf_[0] = buf[i];
          writePos = i;
          break;
        }
      }
    }

    return writePos;
  }

  void reset() {
    partialLen_ = 0;
    skippingDataUri_ = false;
  }

 private:
  char partialBuf_[10] = {};
  size_t partialLen_ = 0;
  bool skippingDataUri_ = false;
  char skipUntilQuote_ = '"';
};
