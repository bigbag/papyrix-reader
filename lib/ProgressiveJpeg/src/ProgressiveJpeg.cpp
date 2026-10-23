#include "ProgressiveJpeg.h"

#if PAPYRIX_CAP_PROGRESSIVE_JPEG
#include <algorithm>
#include <cstring>
#include <new>

namespace ProgressiveJpeg {
namespace {

constexpr uint8_t MAX_COMPONENTS = 3;
constexpr uint8_t MAX_LUMA_SCANS = 32;

constexpr uint8_t TABLE_POOL = 16;
constexpr size_t INPUT_BUFFER = 512;

constexpr uint8_t ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

bool isStartOfFrame(const uint8_t marker) {
  return (marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
         (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF);
}

struct Source {
  FsFile* file = nullptr;
  uint32_t size = 0;
  AbortCallback shouldAbort = nullptr;
  void* abortUser = nullptr;
  Result failure = Result::Ok;
  uint8_t buffer[INPUT_BUFFER] = {};
  uint32_t start = 0;
  uint16_t length = 0;

  bool poll() {
    if (failure != Result::Ok) return false;
    if (shouldAbort && shouldAbort(abortUser)) failure = Result::Aborted;
    return failure == Result::Ok;
  }

  int at(const uint32_t pos) {
    if (failure != Result::Ok || pos >= size) return -1;
    if (pos - start >= length) {
      if (!poll()) return -1;
      const size_t requested = std::min<size_t>(INPUT_BUFFER, size - pos);
      if (!file->seek(pos) || file->read(buffer, requested) != static_cast<int>(requested)) {
        failure = Result::IoError;
        return -1;
      }
      start = pos;
      length = static_cast<uint16_t>(requested);
    }
    return buffer[pos - start];
  }

  int word(uint32_t pos) {
    const int hi = at(pos);
    const int lo = at(pos + 1);
    return hi < 0 || lo < 0 ? -1 : (hi << 8) | lo;
  }
};

struct Lookahead {
  uint16_t entry[256];
};

struct HuffmanTable {
  uint8_t count[17] = {};
  uint16_t firstCode[17] = {};
  uint16_t firstSymbol[17] = {};
  const Lookahead* lookahead = nullptr;
  uint16_t symbolCount = 0;
  uint8_t symbols[256] = {};

  bool build(const uint8_t counts[16]) {
    uint32_t code = 0;
    uint16_t offset = 0;
    for (uint8_t length = 1; length <= 16; ++length) {
      const uint8_t n = counts[length - 1];
      if (code + n >= (1UL << length)) return false;
      count[length] = n;
      firstCode[length] = static_cast<uint16_t>(code);
      firstSymbol[length] = offset;
      code = (code + n) << 1;
      offset = static_cast<uint16_t>(offset + n);
    }
    symbolCount = offset;
    return offset > 0 && offset <= 256;
  }

  void buildLookahead(Lookahead& la) {
    memset(la.entry, 0, sizeof(la.entry));
    for (uint8_t length = 1; length <= 8; ++length) {
      for (uint16_t i = 0; i < count[length]; ++i) {
        const uint32_t code = static_cast<uint32_t>(firstCode[length] + i);
        const uint16_t value = static_cast<uint16_t>((length << 8) | symbols[firstSymbol[length] + i]);
        const uint32_t first = code << (8 - length);
        for (uint32_t k = 0; k < (1u << (8 - length)); ++k) la.entry[first + k] = value;
      }
    }
    lookahead = &la;
  }

  bool sameAs(const HuffmanTable& other) const {
    return symbolCount == other.symbolCount && memcmp(count, other.count, sizeof(count)) == 0 &&
           memcmp(symbols, other.symbols, symbolCount) == 0;
  }
};

struct Component {
  uint8_t id = 0;
  uint8_t h = 1;
  uint8_t v = 1;
  uint8_t quantizer = 0;
};

struct Scan {
  uint32_t dataStart = 0;
  uint32_t dataEnd = 0;
  uint16_t restartInterval = 0;
  uint8_t compCount = 0;
  uint8_t comp[MAX_COMPONENTS] = {};
  uint8_t dcTable[MAX_COMPONENTS] = {};
  uint8_t acTable = 0;
  uint8_t ss = 0, se = 0, ah = 0, al = 0;
};

struct Cursor {
  uint32_t pos = 0;
  uint32_t bits = 0;
  uint8_t bitCount = 0;
  bool markerHit = false;
  bool failed = false;
  uint8_t restartIndex = 0;
  uint32_t unitsRemaining = 0;
  uint16_t unitsToRestart = 0;
  uint32_t eobrun = 0;
  int32_t predictor[MAX_COMPONENTS] = {};
};

struct State {
  Source source;

  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t compCount = 0;
  uint8_t maxH = 1;
  uint8_t maxV = 1;
  Component comps[MAX_COMPONENTS];
  uint16_t quant[4][64] = {};
  bool quantPresent[4] = {};
  bool quantUsed[4] = {};
  int8_t approximation[MAX_COMPONENTS][64];
  int adobeTransform = -1;

  HuffmanTable pool[TABLE_POOL];
  uint8_t poolUsed = 0;
  static constexpr uint8_t kMaxLookaheads = 6;
  Lookahead lookaheads[kMaxLookaheads];
  uint8_t lookaheadsUsed = 0;
  int8_t current[2][4] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}};

  Scan scans[MAX_LUMA_SCANS];
  Cursor cursors[MAX_LUMA_SCANS];
  uint8_t scanCount = 0;
  uint16_t restartInterval = 0;
  bool frameSeen = false;

  int16_t basis[8][8] = {};
  int8_t keptIndex[64] = {};
};

struct Geometry {
  uint16_t lumaCols = 0;
  uint16_t lumaRows = 0;
  uint16_t mcuCols = 0;
  uint16_t paddedCols = 0;
  uint8_t bandBlockRows = 1;
  uint16_t bandCount = 0;
  uint8_t n = 8;
  uint8_t kept = 64;
};

Geometry geometryFor(const uint16_t width, const uint16_t height, const uint8_t compCount, const uint8_t maxH,
                     const uint8_t maxV) {
  Geometry g;
  g.lumaCols = static_cast<uint16_t>((width + 7) / 8);
  g.lumaRows = static_cast<uint16_t>((height + 7) / 8);
  if (compCount == 1) {
    g.mcuCols = g.lumaCols;
    g.paddedCols = g.lumaCols;
    g.bandBlockRows = 1;
    g.bandCount = g.lumaRows;
  } else {
    g.mcuCols = static_cast<uint16_t>((width + 8 * maxH - 1) / (8 * maxH));
    g.paddedCols = static_cast<uint16_t>(g.mcuCols * maxH);
    g.bandBlockRows = maxV;
    g.bandCount = static_cast<uint16_t>((height + 8 * maxV - 1) / (8 * maxV));
  }
  return g;
}

constexpr size_t alignUp(const size_t v) { return (v + 7) & ~static_cast<size_t>(7); }

struct Layout {
  size_t maskOffset, coefOffset, pixelOffset, total;
};

}  // namespace
struct Session {
  State st;
};
namespace {

Layout layoutFor(const Geometry& g) {
  const size_t blocks = static_cast<size_t>(g.bandBlockRows) * g.paddedCols;
  Layout l{};
  l.maskOffset = alignUp(sizeof(Session));
  l.coefOffset = alignUp(l.maskOffset + blocks * sizeof(uint64_t));
  l.pixelOffset = alignUp(l.coefOffset + blocks * g.kept * sizeof(int16_t));
  l.total = alignUp(l.pixelOffset + static_cast<size_t>(g.n) * g.paddedCols * g.n) + 8;
  return l;
}

Result inputFailure(const Source& source) {
  return source.failure != Result::Ok ? source.failure : Result::InvalidData;
}

bool tableReferenced(const State& st, const uint8_t index) {
  for (const auto& cls : st.current) {
    for (const int8_t t : cls) {
      if (t == index) return true;
    }
  }
  for (uint8_t s = 0; s < st.scanCount; ++s) {
    const Scan& scan = st.scans[s];
    if (scan.ss == 0) {
      if (scan.ah == 0) {
        for (uint8_t k = 0; k < scan.compCount; ++k) {
          if (scan.dcTable[k] == index) return true;
        }
      }
    } else if (scan.acTable == index) {
      return true;
    }
  }
  return false;
}

int freeTableSlot(const State& st) {
  if (st.poolUsed < TABLE_POOL) return st.poolUsed;
  for (uint8_t i = 0; i < TABLE_POOL; ++i) {
    if (!tableReferenced(st, i)) return i;
  }
  return -1;
}

Result parseQuantizers(State& st, uint32_t p, uint32_t end) {
  while (p < end) {
    const int descriptor = st.source.at(p++);
    if (descriptor < 0) return inputFailure(st.source);
    const uint8_t precision = static_cast<uint8_t>(descriptor) >> 4;
    const uint8_t id = static_cast<uint8_t>(descriptor) & 0x0F;
    if (precision > 1 || id >= 4 || p + (precision ? 128U : 64U) > end) return Result::InvalidData;
    for (uint8_t k = 0; k < 64; ++k) {
      const int value = precision ? st.source.word(p) : st.source.at(p);
      p += precision ? 2 : 1;
      if (value < 0) return inputFailure(st.source);
      if (value == 0 || (st.quantUsed[id] && st.quant[id][ZIGZAG[k]] != value)) return Result::InvalidData;
      st.quant[id][ZIGZAG[k]] = static_cast<uint16_t>(value);
    }
    st.quantPresent[id] = true;
  }
  return Result::Ok;
}

Result parseHuffmanTables(State& st, uint32_t p, const uint32_t end) {
  while (p < end) {
    if (p + 17 > end) return Result::InvalidData;
    const int descriptor = st.source.at(p++);
    if (descriptor < 0) return inputFailure(st.source);
    const uint8_t cls = static_cast<uint8_t>(descriptor) >> 4;
    const uint8_t id = static_cast<uint8_t>(descriptor) & 0x0F;
    if (cls > 1 || id >= 4) return Result::InvalidData;
    const int slot = freeTableSlot(st);
    HuffmanTable scratch;
    HuffmanTable& table = slot < 0 ? scratch : st.pool[slot];
    table.lookahead = nullptr;
    uint8_t counts[16];
    for (uint8_t& c : counts) {
      const int value = st.source.at(p++);
      if (value < 0) return inputFailure(st.source);
      c = static_cast<uint8_t>(value);
    }
    if (!table.build(counts) || p + table.symbolCount > end) return Result::InvalidData;
    for (uint16_t i = 0; i < table.symbolCount; ++i) {
      const int value = st.source.at(p++);
      if (value < 0) return inputFailure(st.source);
      table.symbols[i] = static_cast<uint8_t>(value);
    }
    int index = slot;
    for (uint8_t i = 0; i < st.poolUsed; ++i) {
      if (i != slot && st.pool[i].sameAs(table)) {
        index = i;
        break;
      }
    }
    if (index < 0) return Result::Unsupported;
    st.current[cls][id] = static_cast<int8_t>(index);
    if (index == slot && slot == st.poolUsed) ++st.poolUsed;
  }
  return Result::Ok;
}

bool validGeometry(const ImageInfo& info) {
  if (info.width < 20 || info.width > 2048 || info.height < 20 || info.height > 3072 ||
      (info.componentCount != 1 && info.componentCount != 3))
    return false;
  uint8_t maxH = 1, maxV = 1;
  for (uint8_t i = 0; i < info.componentCount; ++i) {
    const auto& c = info.components[i];
    if (c.horizontal < 1 || c.horizontal > 4 || c.vertical < 1 || c.vertical > 4 || c.quantizer > 3) return false;
    for (uint8_t j = 0; j < i; ++j) {
      if (c.id == info.components[j].id) return false;
    }
    maxH = std::max(maxH, c.horizontal);
    maxV = std::max(maxV, c.vertical);
  }
  return info.maxHorizontal == maxH && info.maxVertical == maxV && info.components[0].horizontal == maxH &&
         info.components[0].vertical == maxV;
}

bool supportedColor(const ImageInfo& info, int adobeTransform) {
  if (adobeTransform > 1) return false;
  if (info.componentCount != 3) return true;
  return adobeTransform != 0 &&
         !(info.components[0].id == 'R' && info.components[1].id == 'G' && info.components[2].id == 'B');
}

Result readFrame(Source& src, uint32_t p, uint32_t end, ImageInfo& info) {
  if (end - p < 6) return Result::InvalidData;
  const int precision = src.at(p);
  const int height = src.word(p + 1);
  const int width = src.word(p + 3);
  const int count = src.at(p + 5);
  if (precision < 0 || height < 0 || width < 0 || count < 0) return inputFailure(src);
  if (precision != 8 || (count != 1 && count != 3)) return Result::Unsupported;
  if (end - p != 6U + 3U * count) return Result::InvalidData;
  info = {};
  info.width = static_cast<uint16_t>(width);
  info.height = static_cast<uint16_t>(height);
  info.componentCount = static_cast<uint8_t>(count);
  p += 6;
  for (int i = 0; i < count; ++i, p += 3) {
    const int id = src.at(p), sampling = src.at(p + 1), quantizer = src.at(p + 2);
    if (id < 0 || sampling < 0 || quantizer < 0) return inputFailure(src);
    info.components[i] = {static_cast<uint8_t>(id), static_cast<uint8_t>(sampling >> 4),
                          static_cast<uint8_t>(sampling & 15), static_cast<uint8_t>(quantizer)};
    for (int j = 0; j < i; ++j) {
      if (info.components[j].id == id) return Result::InvalidData;
    }
    info.maxHorizontal = std::max(info.maxHorizontal, info.components[i].horizontal);
    info.maxVertical = std::max(info.maxVertical, info.components[i].vertical);
  }
  return validGeometry(info) ? Result::Ok : Result::Unsupported;
}

Result readAdobe(Source& src, uint32_t p, uint32_t end, int& transform) {
  constexpr char signature[] = "Adobe";
  if (end - p < 5) return Result::Ok;
  for (unsigned i = 0; i < 5; ++i) {
    if (src.at(p + i) != signature[i]) return src.failure;
  }
  if (end - p != 12) return Result::InvalidData;
  const int value = src.at(p + 11);
  if (value < 0) return inputFailure(src);
  if (transform >= 0 && value != transform) return Result::InvalidData;
  transform = value;
  return Result::Ok;
}

Result parseFrame(State& st, uint32_t p, const uint32_t end) {
  if (st.frameSeen) return Result::InvalidData;
  ImageInfo info;
  const Result result = readFrame(st.source, p, end, info);
  if (result != Result::Ok) return result;
  st.width = info.width;
  st.height = info.height;
  st.compCount = info.componentCount;
  st.maxH = info.maxHorizontal;
  st.maxV = info.maxVertical;
  for (uint8_t i = 0; i < info.componentCount; ++i) {
    const auto& c = info.components[i];
    st.comps[i] = {c.id, c.horizontal, c.vertical, c.quantizer};
  }
  st.frameSeen = true;
  return Result::Ok;
}

Result parseScan(State& st, uint32_t p, const uint32_t end) {
  if (!st.frameSeen) return Result::InvalidData;
  const int n = st.source.at(p++);
  if (n <= 0 || n > MAX_COMPONENTS || end - p != 2U * n + 3U) return Result::InvalidData;
  Scan scan;
  scan.compCount = static_cast<uint8_t>(n);
  bool hasLuma = false;
  uint8_t acId = 0;
  uint8_t dcIds[MAX_COMPONENTS] = {};
  for (int k = 0; k < n; ++k, p += 2) {
    const int selector = st.source.at(p);
    const int tables = st.source.at(p + 1);
    int index = -1;
    for (uint8_t c = 0; c < st.compCount; ++c) {
      if (st.comps[c].id == selector) index = c;
    }
    if (index < 0 || tables < 0 || (tables >> 4) > 3 || (tables & 15) > 3) return Result::InvalidData;
    for (int j = 0; j < k; ++j) {
      if (scan.comp[j] == index) return Result::InvalidData;
    }
    scan.comp[k] = static_cast<uint8_t>(index);
    dcIds[k] = static_cast<uint8_t>(tables >> 4);
    acId = static_cast<uint8_t>(tables & 15);
    hasLuma |= index == 0;
  }
  scan.ss = static_cast<uint8_t>(st.source.at(p));
  scan.se = static_cast<uint8_t>(st.source.at(p + 1));
  const int approx = st.source.at(p + 2);
  scan.ah = static_cast<uint8_t>(approx >> 4);
  scan.al = static_cast<uint8_t>(approx & 0x0F);
  const bool badSpectrum = scan.ss == 0 ? scan.se != 0 : (scan.se < scan.ss || scan.se > 63 || n != 1);
  if (badSpectrum || scan.al > 13 || scan.ah > 13 || (scan.ah != 0 && scan.ah != scan.al + 1)) {
    return Result::InvalidData;
  }
  for (int k = 0; k < n; ++k) {
    const uint8_t comp = scan.comp[k];
    if (!st.quantPresent[st.comps[comp].quantizer] || (scan.ss != 0 && st.approximation[comp][0] < 0))
      return Result::InvalidData;
    for (int coefficient = scan.ss; coefficient <= scan.se; ++coefficient) {
      const int previous = st.approximation[comp][coefficient];
      if (scan.ah == 0 ? previous != -1 : previous != scan.ah) return Result::InvalidData;
    }
  }

  if (scan.ss == 0) {
    if (scan.ah == 0) {
      for (int k = 0; k < n; ++k) {
        const int8_t t = st.current[0][dcIds[k]];
        if (t < 0) return Result::InvalidData;
        scan.dcTable[k] = static_cast<uint8_t>(t);
      }
    }
  } else {
    const int8_t t = st.current[1][acId];
    if (t < 0) return Result::InvalidData;
    scan.acTable = static_cast<uint8_t>(t);
    if (!st.pool[t].lookahead && st.lookaheadsUsed < State::kMaxLookaheads) {
      st.pool[t].buildLookahead(st.lookaheads[st.lookaheadsUsed++]);
    }
  }
  for (int k = 0; k < n; ++k) {
    const uint8_t comp = scan.comp[k];
    st.quantUsed[st.comps[comp].quantizer] = true;
    for (int coefficient = scan.ss; coefficient <= scan.se; ++coefficient) {
      st.approximation[comp][coefficient] = static_cast<int8_t>(scan.al);
    }
  }
  if (!hasLuma) return Result::Ok;
  if (st.scanCount >= MAX_LUMA_SCANS) return Result::Unsupported;
  scan.restartInterval = st.restartInterval;
  scan.dataStart = end;
  st.scans[st.scanCount++] = scan;
  return Result::Ok;
}

Result indexScans(State& st) {
  Source& src = st.source;
  if (src.at(0) != 0xFF || src.at(1) != 0xD8) return inputFailure(src);
  uint32_t p = 2;
  for (;;) {
    if (!src.poll()) return inputFailure(src);
    if (src.at(p++) != 0xFF) return inputFailure(src);
    int marker;
    do {
      marker = src.at(p++);
    } while (marker == 0xFF);
    if (marker < 0) return inputFailure(src);
    if (marker == 0xD9) {
      return st.frameSeen && st.scanCount > 0 ? Result::Ok : Result::InvalidData;
    }
    if (marker == 0 || marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      return Result::InvalidData;
    }
    const int length = src.word(p);
    if (length < 2 || p > src.size || static_cast<uint32_t>(length) > src.size - p) return inputFailure(src);
    const uint32_t payload = p + 2;
    const uint32_t end = p + length;
    Result result = Result::Ok;
    const uint8_t previousScans = st.scanCount;
    if (marker == 0xDB)
      result = parseQuantizers(st, payload, end);
    else if (marker == 0xC4)
      result = parseHuffmanTables(st, payload, end);
    else if (isStartOfFrame(static_cast<uint8_t>(marker))) {
      result = marker == 0xC2 ? parseFrame(st, payload, end) : Result::Unsupported;
    } else if (marker == 0xEE)
      result = readAdobe(src, payload, end, st.adobeTransform);
    else if (marker == 0xCC || marker == 0xDC)
      return Result::Unsupported;
    else if (marker == 0xDD) {
      if (length != 4) return Result::InvalidData;
      const int interval = src.word(payload);
      if (interval < 0) return inputFailure(src);
      st.restartInterval = static_cast<uint16_t>(interval);
    } else if (marker == 0xDA)
      result = parseScan(st, payload, end);
    if (result != Result::Ok) return result;
    if (src.failure != Result::Ok) return src.failure;
    p = end;
    if (marker != 0xDA) continue;
    for (;;) {
      const int byte = src.at(p);
      if (byte < 0) return inputFailure(src);
      if (byte != 0xFF) {
        ++p;
        continue;
      }
      uint32_t next = p + 1;
      int code;
      do {
        code = src.at(next++);
      } while (code == 0xFF);
      if (code < 0) return inputFailure(src);
      if (code == 0) {
        if (next != p + 2) return Result::InvalidData;
        p = next;
      } else if (code >= 0xD0 && code <= 0xD7) {
        p = next;
      } else {
        if (st.scanCount != previousScans) st.scans[st.scanCount - 1].dataEnd = p;
        break;
      }
    }
  }
}

struct BitReader {
  Source& src;
  Cursor& c;

  void fill() {
    while (!c.markerHit && c.bitCount <= 24) {
      const int byte = src.at(c.pos);
      if (byte < 0) {
        c.markerHit = true;
        break;
      }
      if (byte == 0xFF) {
        if (src.at(c.pos + 1) != 0) {
          c.markerHit = true;
          break;
        }
        ++c.pos;
      }
      ++c.pos;
      c.bits |= static_cast<uint32_t>(byte) << (24 - c.bitCount);
      c.bitCount = static_cast<uint8_t>(c.bitCount + 8);
    }
  }

  uint32_t bits(const uint8_t count) {
    if (count == 0) return 0;
    if (c.bitCount < count) fill();
    if (c.bitCount < count) {
      c.failed = true;
      return 0;
    }
    const uint32_t value = c.bits >> (32 - count);
    c.bits <<= count;
    c.bitCount = static_cast<uint8_t>(c.bitCount - count);
    return value;
  }

  bool readBit() { return bits(1) != 0; }

  int symbol(const HuffmanTable& table) {
    if (c.bitCount < 16) fill();
    uint8_t length = 1;
    if (table.lookahead && c.bitCount >= 8) {
      const uint16_t e = table.lookahead->entry[c.bits >> 24];
      if (e != 0) {
        const uint8_t len = static_cast<uint8_t>(e >> 8);
        c.bits <<= len;
        c.bitCount = static_cast<uint8_t>(c.bitCount - len);
        return e & 0xFF;
      }
      length = 9;
    }
    for (; length <= 16 && length <= c.bitCount; ++length) {
      const uint32_t code = c.bits >> (32 - length);
      const uint32_t offset = code - table.firstCode[length];
      if (code >= table.firstCode[length] && offset < table.count[length]) {
        c.bits <<= length;
        c.bitCount = static_cast<uint8_t>(c.bitCount - length);
        return table.symbols[table.firstSymbol[length] + offset];
      }
    }
    c.failed = true;
    return -1;
  }

  static int32_t extend(const uint32_t value, const uint8_t size) {
    if (size == 0) return 0;
    return value < (1U << (size - 1)) ? static_cast<int32_t>(value) - (1 << size) + 1 : static_cast<int32_t>(value);
  }

  bool paddingValid() const {
    return !c.failed && c.bitCount <= 7 && (c.bitCount == 0 || (c.bits >> (32 - c.bitCount)) == (1U << c.bitCount) - 1);
  }

  bool restart() {
    if (!paddingValid() || c.eobrun != 0 || src.at(c.pos++) != 0xFF) return false;
    int marker;
    do {
      marker = src.at(c.pos++);
    } while (marker == 0xFF);
    if (marker != 0xD0 + c.restartIndex) return false;
    c.restartIndex = static_cast<uint8_t>((c.restartIndex + 1) & 7);
    c.bits = 0;
    c.bitCount = 0;
    c.markerHit = false;
    memset(c.predictor, 0, sizeof(c.predictor));
    return true;
  }
};

struct BlockRef {
  int16_t* coef = nullptr;
  uint64_t* mask = nullptr;
};

inline void setCoefficient(const State& st, const BlockRef& block, const uint8_t zigzag, const int32_t value) {
  const uint8_t natural = ZIGZAG[zigzag];
  *block.mask |= 1ULL << natural;
  const int8_t kept = st.keptIndex[natural];
  if (kept >= 0) block.coef[kept] = static_cast<int16_t>(std::clamp<int32_t>(value, -32768, 32767));
}

inline void refineCoefficient(const State& st, BitReader& in, const BlockRef& block, const uint8_t natural,
                              const int32_t p1) {
  if (!in.readBit()) return;
  const int8_t kept = st.keptIndex[natural];
  if (kept < 0) return;
  int16_t& coef = block.coef[kept];
  if ((coef & p1) == 0) coef = static_cast<int16_t>(coef >= 0 ? coef + p1 : coef - p1);
}

bool decodeDcFirst(const State& st, BitReader& in, const Scan& scan, const uint8_t scanComp, const BlockRef& block) {
  const int size = in.symbol(st.pool[scan.dcTable[scanComp]]);
  if (size < 0 || size > 11) return false;
  const uint8_t comp = scan.comp[scanComp];
  in.c.predictor[comp] += BitReader::extend(in.bits(static_cast<uint8_t>(size)), static_cast<uint8_t>(size));
  if (block.coef) {
    block.coef[0] = static_cast<int16_t>(
        std::clamp<int64_t>(static_cast<int64_t>(in.c.predictor[comp]) * (1 << scan.al), -32768, 32767));
  }
  return true;
}

void decodeDcRefine(BitReader& in, const Scan& scan, const BlockRef& block) {
  if (in.readBit() && block.coef) block.coef[0] = static_cast<int16_t>(block.coef[0] | (1 << scan.al));
}

bool decodeAcFirst(const State& st, BitReader& in, const Scan& scan, const BlockRef& block) {
  Cursor& c = in.c;
  if (c.eobrun > 0) {
    --c.eobrun;
    return true;
  }
  const HuffmanTable& table = st.pool[scan.acTable];
  for (int k = scan.ss; k <= scan.se; ++k) {
    const int rs = in.symbol(table);
    if (rs < 0) return false;
    const uint8_t r = static_cast<uint8_t>(rs >> 4);
    const uint8_t s = static_cast<uint8_t>(rs & 15);
    if (s) {
      k += r;
      if (k > scan.se || s > 10) return false;
      const int32_t value = BitReader::extend(in.bits(s), s) * (1 << scan.al);
      setCoefficient(st, block, static_cast<uint8_t>(k), value);
    } else if (r == 15) {
      k += 15;
      if (k > scan.se) return false;
    } else {
      c.eobrun = 1U << r;
      if (r) c.eobrun += in.bits(r);
      --c.eobrun;
      break;
    }
  }
  return true;
}

bool decodeAcRefine(const State& st, BitReader& in, const Scan& scan, const BlockRef& block) {
  Cursor& c = in.c;
  const int32_t p1 = 1 << scan.al;
  const HuffmanTable& table = st.pool[scan.acTable];
  int k = scan.ss;
  if (c.eobrun == 0) {
    for (; k <= scan.se; ++k) {
      const int rs = in.symbol(table);
      if (rs < 0) return false;
      int r = rs >> 4;
      const int s = rs & 15;
      int32_t value = 0;
      if (s) {
        if (s != 1) return false;
        value = in.readBit() ? p1 : -p1;
      } else if (r != 15) {
        c.eobrun = 1U << r;
        if (r) c.eobrun += in.bits(static_cast<uint8_t>(r));
        break;
      }

      do {
        const uint8_t natural = ZIGZAG[k];
        if (*block.mask & (1ULL << natural)) {
          refineCoefficient(st, in, block, natural, p1);
        } else {
          if (--r < 0) break;
        }
        ++k;
      } while (k <= scan.se);
      if (r >= 0) return false;
      if (value) {
        if (k > scan.se) return false;
        setCoefficient(st, block, static_cast<uint8_t>(k), value);
      }
    }
  }
  if (c.eobrun > 0) {
    for (; k <= scan.se; ++k) {
      const uint8_t natural = ZIGZAG[k];
      if (*block.mask & (1ULL << natural)) refineCoefficient(st, in, block, natural, p1);
    }
    --c.eobrun;
  }
  return true;
}

bool beginUnit(BitReader& in, const Scan& scan) {
  if (!in.src.poll() || in.c.failed || in.c.unitsRemaining == 0) return false;
  --in.c.unitsRemaining;
  if (scan.restartInterval == 0) return true;
  if (in.c.unitsToRestart == 0) {
    if (!in.restart()) return false;
    in.c.unitsToRestart = scan.restartInterval;
  }
  --in.c.unitsToRestart;
  return true;
}

bool finishUnit(const BitReader& in, const Scan& scan) {
  return !in.c.failed && in.c.eobrun <= in.c.unitsRemaining &&
         (scan.restartInterval == 0 || in.c.eobrun <= in.c.unitsToRestart);
}

constexpr int16_t COS16[32] = {512,  502,  473,  426,  362,  284,  196,  100,  0,    -100, -196,
                               -284, -362, -426, -473, -502, -512, -502, -473, -426, -362, -284,
                               -196, -100, 0,    100,  196,  284,  362,  426,  473,  502};

void buildBasis(State& st, const uint8_t n) {
  const int s = 8 / n;
  for (int X = 0; X < n; ++X) {
    for (int u = 0; u < n; ++u) {
      st.basis[X][u] = u == 0 ? 362 : COS16[((2 * X + 1) * u * s) % 32];
    }
  }
  for (int natural = 0; natural < 64; ++natural) {
    const int row = natural / 8;
    const int col = natural % 8;
    st.keptIndex[natural] = static_cast<int8_t>(row < n && col < n ? row * n + col : -1);
  }
}

void fillDcBlock(const int16_t dc, const uint16_t quant0, const uint8_t n, uint8_t* out, const size_t stride) {
  const int32_t f = std::clamp<int32_t>(dc * static_cast<int32_t>(quant0), -8191, 8191);
  const int32_t t = (f * 362 + 64) >> 7;
  const uint8_t level = static_cast<uint8_t>(std::clamp<int32_t>(128 + ((362 * t + (1 << 12)) >> 13), 0, 255));
  for (int Y = 0; Y < n; ++Y) memset(out + static_cast<size_t>(Y) * stride, level, n);
}

void idctBlock(const State& st, const int16_t* coef, const uint16_t* quant, const uint8_t n, uint8_t* out,
               const size_t stride) {
  int32_t t[8][8];
  for (int v = 0; v < n; ++v) {
    int32_t f[8];
    bool used = false;
    for (int u = 0; u < n; ++u) {
      f[u] = std::clamp<int32_t>(coef[v * n + u] * static_cast<int32_t>(quant[v * 8 + u]), -8191, 8191);
      used |= f[u] != 0;
    }
    for (int X = 0; X < n; ++X) {
      int32_t sum = 0;
      if (used) {
        for (int u = 0; u < n; ++u) sum += f[u] * st.basis[X][u];
      }
      t[v][X] = (sum + 64) >> 7;
    }
  }
  for (int Y = 0; Y < n; ++Y) {
    uint8_t* row = out + static_cast<size_t>(Y) * stride;
    for (int X = 0; X < n; ++X) {
      int32_t sum = 0;
      for (int v = 0; v < n; ++v) sum += st.basis[Y][v] * t[v][X];
      row[X] = static_cast<uint8_t>(std::clamp<int32_t>(128 + ((sum + (1 << 12)) >> 13), 0, 255));
    }
  }
}

void initCursors(State& st, const Geometry& g) {
  for (uint8_t s = 0; s < st.scanCount; ++s) {
    Cursor& c = st.cursors[s];
    c = Cursor{};
    c.pos = st.scans[s].dataStart;
    c.unitsToRestart = st.scans[s].restartInterval;
    c.unitsRemaining = st.scans[s].compCount > 1 ? static_cast<uint32_t>(g.mcuCols) * g.bandCount
                                                 : static_cast<uint32_t>(g.lumaCols) * g.lumaRows;
  }
}

Result decodeBand(State& st, const Geometry& g, uint8_t* workspace, const Layout& layout, const uint16_t band,
                  const BandCallback callback, void* user) {
  auto* masks = reinterpret_cast<uint64_t*>(workspace + layout.maskOffset);
  auto* coefs = reinterpret_cast<int16_t*>(workspace + layout.coefOffset);
  uint8_t* pixels = workspace + layout.pixelOffset;
  const size_t bandBlocks = static_cast<size_t>(g.bandBlockRows) * g.paddedCols;
  const size_t pixelStride = static_cast<size_t>(g.paddedCols) * g.n;
  const uint16_t outWidth = st.width;
  const uint16_t outHeight = st.height;
  const uint16_t* quant = st.quant[st.comps[0].quantizer];

  auto blockAt = [&](const int bandRow, const int col) {
    const size_t index = static_cast<size_t>(bandRow) * g.paddedCols + col;
    return BlockRef{coefs + index * g.kept, masks + index};
  };

  memset(masks, 0, bandBlocks * sizeof(uint64_t));
  memset(coefs, 0, bandBlocks * g.kept * sizeof(int16_t));
  const int firstRow = band * g.bandBlockRows;
  for (uint8_t s = 0; s < st.scanCount; ++s) {
    const Scan& scan = st.scans[s];
    BitReader in{st.source, st.cursors[s]};
    if (scan.compCount > 1) {
      for (uint16_t mx = 0; mx < g.mcuCols; ++mx) {
        if (!beginUnit(in, scan)) return Result::InvalidData;
        for (uint8_t k = 0; k < scan.compCount; ++k) {
          const Component& comp = st.comps[scan.comp[k]];
          for (uint8_t by = 0; by < comp.v; ++by) {
            for (uint8_t bx = 0; bx < comp.h; ++bx) {
              const BlockRef block = scan.comp[k] == 0 ? blockAt(by, mx * st.maxH + bx) : BlockRef{};
              if (scan.ah == 0) {
                if (!decodeDcFirst(st, in, scan, k, block)) return Result::InvalidData;
              } else {
                decodeDcRefine(in, scan, block);
              }
              if (in.c.failed) return inputFailure(st.source);
            }
          }
        }
        if (!finishUnit(in, scan)) return inputFailure(st.source);
      }
      continue;
    }

    const int lastRow = std::min<int>(firstRow + g.bandBlockRows, g.lumaRows);
    for (int row = firstRow; row < lastRow; ++row) {
      for (uint16_t col = 0; col < g.lumaCols; ++col) {
        if (!beginUnit(in, scan)) return Result::InvalidData;
        const BlockRef block = blockAt(row - firstRow, col);
        bool ok = true;
        if (scan.ss == 0) {
          if (scan.ah == 0) {
            ok = decodeDcFirst(st, in, scan, 0, block);
          } else {
            decodeDcRefine(in, scan, block);
          }
        } else {
          ok = scan.ah == 0 ? decodeAcFirst(st, in, scan, block) : decodeAcRefine(st, in, scan, block);
        }
        if (!ok || !finishUnit(in, scan)) return inputFailure(st.source);
      }
    }
  }
  if (st.source.failure != Result::Ok) return st.source.failure;

  const uint16_t colsNeeded = static_cast<uint16_t>((outWidth + g.n - 1) / g.n);
  for (int r = 0; r < g.bandBlockRows; ++r) {
    const int y = (firstRow + r) * g.n;
    if (y >= outHeight) break;
    for (uint16_t col = 0; col < colsNeeded; ++col) {
      if (!st.source.poll()) return inputFailure(st.source);
      const BlockRef block = blockAt(r, col);
      uint8_t* dst = pixels + static_cast<size_t>(col) * g.n;
      if ((*block.mask & ~1ULL) == 0) {
        fillDcBlock(block.coef[0], quant[0], g.n, dst, pixelStride);
      } else {
        idctBlock(st, block.coef, quant, g.n, dst, pixelStride);
      }
    }
    const uint16_t rows = static_cast<uint16_t>(std::min<int>(g.n, outHeight - y));
    if (!callback(user, static_cast<uint16_t>(y), pixels, outWidth, rows, static_cast<uint16_t>(pixelStride))) {
      return Result::Stopped;
    }
  }
  return Result::Ok;
}

}  // namespace

Result probe(FsFile& file, ImageInfo& info, AbortCallback shouldAbort, void* abortUser) {
  info = {};
  if (!file) return Result::IoError;
  const auto entry = file.position();
  Source src;
  src.file = &file;
  src.shouldAbort = shouldAbort;
  src.abortUser = abortUser;
  auto finish = [&](Result result) {
    if (src.failure != Result::Ok) result = src.failure;
    if (!file.seek(entry)) result = Result::IoError;
    if (result != Result::Ok) info = {};
    return result;
  };
  if (!src.poll()) return finish(src.failure);
  if (file.size() > 512 * 1024) return finish(Result::Unsupported);
  src.size = static_cast<uint32_t>(file.size());
  if (src.at(0) != 0xFF || src.at(1) != 0xD8) return finish(inputFailure(src));
  uint32_t p = 2;
  int transform = -1;
  for (;;) {
    if (src.at(p++) != 0xFF) return finish(inputFailure(src));
    int marker;
    do {
      marker = src.at(p++);
    } while (marker == 0xFF);
    if (marker < 0) return finish(inputFailure(src));
    if (marker == 0 || marker == 0xD8 || marker == 0xD9 || marker == 0xDA || marker == 0x01 ||
        (marker >= 0xD0 && marker <= 0xD7))
      return finish(Result::InvalidData);
    const int length = src.word(p);
    if (length < 2 || p > src.size || static_cast<uint32_t>(length) > src.size - p) {
      return finish(inputFailure(src));
    }
    if (isStartOfFrame(static_cast<uint8_t>(marker))) {
      if (marker != 0xC2) return finish(Result::Unsupported);
      const Result result = readFrame(src, p + 2, p + length, info);
      if (result != Result::Ok) return finish(result);
      return finish(supportedColor(info, transform) ? Result::Ok : Result::Unsupported);
    }
    if (marker == 0xEE) {
      const Result result = readAdobe(src, p + 2, p + length, transform);
      if (result != Result::Ok) return finish(result);
    }
    p += length;
  }
}

size_t workspaceBytes(const ImageInfo& info) {
  if (!validGeometry(info)) return 0;
  return layoutFor(geometryFor(info.width, info.height, info.componentCount, info.maxHorizontal, info.maxVertical))
      .total;
}

Result decode(FsFile& file, const ImageInfo& info, const DecodeOptions& options, BandCallback callback, void* user) {
  if (!file) return Result::IoError;
  const auto entry = file.position();
  auto finish = [&](Result result) { return file.seek(entry) ? result : Result::IoError; };
  if (!validGeometry(info) || !callback) return finish(Result::InvalidData);
  if (options.shouldAbort && options.shouldAbort(options.abortUser)) return finish(Result::Aborted);
  if (file.size() > 512 * 1024) return finish(Result::Unsupported);
  const Geometry geometry =
      geometryFor(info.width, info.height, info.componentCount, info.maxHorizontal, info.maxVertical);
  const Layout layout = layoutFor(geometry);
  const size_t skew = (8 - (reinterpret_cast<uintptr_t>(options.workspace) & 7)) & 7;
  if (!options.workspace || options.workspaceSize < layout.total || options.workspaceSize - skew < layout.total - 8)
    return finish(Result::OutOfMemory);
  uint8_t* workspace = options.workspace + skew;
  auto* session = new (workspace) Session();
  State& st = session->st;
  st.source.file = &file;
  st.source.size = static_cast<uint32_t>(file.size());
  st.source.shouldAbort = options.shouldAbort;
  st.source.abortUser = options.abortUser;
  memset(st.approximation, -1, sizeof(st.approximation));
  const Result result = [&]() {
    const Result indexed = indexScans(st);
    if (indexed != Result::Ok) return indexed;
    if (st.width != info.width || st.height != info.height || st.compCount != info.componentCount ||
        st.maxH != info.maxHorizontal || st.maxV != info.maxVertical)
      return Result::InvalidData;
    for (uint8_t i = 0; i < st.compCount; ++i) {
      const auto& expected = info.components[i];
      const auto& actual = st.comps[i];
      if (actual.id != expected.id || actual.h != expected.horizontal || actual.v != expected.vertical ||
          actual.quantizer != expected.quantizer)
        return Result::InvalidData;
    }
    if (!supportedColor(info, st.adobeTransform)) return Result::Unsupported;
    buildBasis(st, 8);
    initCursors(st, geometry);
    for (uint16_t band = 0; band < geometry.bandCount; ++band) {
      const Result decoded = decodeBand(st, geometry, workspace, layout, band, callback, user);
      if (st.source.failure != Result::Ok) return st.source.failure;
      if (decoded != Result::Ok) return decoded;
    }
    for (uint8_t i = 0; i < st.scanCount; ++i) {
      BitReader reader{st.source, st.cursors[i]};
      if (!reader.paddingValid() || reader.c.unitsRemaining != 0 || reader.c.eobrun != 0 ||
          reader.c.pos != st.scans[i].dataEnd)
        return Result::InvalidData;
    }
    return Result::Ok;
  }();
  session->~Session();
  return finish(result);
}
}  // namespace ProgressiveJpeg
#endif
