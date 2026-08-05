#include "InflateReader.h"

#include <cstring>
#include <type_traits>

// Guarantee the cast pattern in the header comment is valid.
static_assert(std::is_standard_layout<InflateReader>::value,
              "InflateReader must be standard-layout for the uzlib callback cast to work");

InflateReader::~InflateReader() { deinit(); }

bool InflateReader::init(const bool streaming) { return init(streaming, nullptr); }

bool InflateReader::init(const bool streaming, uint8_t* externalBuffer) {
  deinit();

  if (streaming) {
    if (externalBuffer) {
      ringBuffer = externalBuffer;
      ownsRingBuffer = false;
    } else {
      ringBuffer = static_cast<uint8_t*>(malloc(STREAMING_DICTIONARY_SIZE));
      if (!ringBuffer) return false;
      ownsRingBuffer = true;
    }
    memset(ringBuffer, 0, STREAMING_DICTIONARY_SIZE);
  }

  uzlib_uncompress_init(&decomp, ringBuffer, ringBuffer ? STREAMING_DICTIONARY_SIZE : 0);
  return true;
}

void InflateReader::deinit() {
  if (ringBuffer && ownsRingBuffer) {
    free(ringBuffer);
  }
  ringBuffer = nullptr;
  ownsRingBuffer = false;
  memset(&decomp, 0, sizeof(decomp));
}

void InflateReader::setSource(const uint8_t* src, size_t len) {
  decomp.source = src;
  decomp.source_limit = src + len;
}

void InflateReader::setReadCallback(int (*cb)(struct uzlib_uncomp*)) { decomp.source_read_cb = cb; }

void InflateReader::skipZlibHeader() {
  uzlib_get_byte(&decomp);
  uzlib_get_byte(&decomp);
}

bool InflateReader::read(uint8_t* dest, size_t len) {
  if (!ringBuffer) {
    decomp.dest_start = dest;
  }
  decomp.dest = dest;
  decomp.dest_limit = dest + len;

  const int res = uzlib_uncompress(&decomp);
  if (res < 0) return false;
  return decomp.dest == decomp.dest_limit;
}

InflateStatus InflateReader::readAtMost(uint8_t* dest, size_t maxLen, size_t* produced) {
  if (!ringBuffer) {
    decomp.dest_start = dest;
  }
  decomp.dest = dest;
  decomp.dest_limit = dest + maxLen;

  const int res = uzlib_uncompress(&decomp);
  *produced = static_cast<size_t>(decomp.dest - dest);

  if (res == TINF_DONE) return InflateStatus::Done;
  if (res < 0) return InflateStatus::Error;
  return InflateStatus::Ok;
}
