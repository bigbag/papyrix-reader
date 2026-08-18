#include "test_utils.h"

#include <G5ImageCache.h>
#include <SDCardManager.h>

#include <climits>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string makeG5File(const G5ImageHeader& header, size_t payloadSize) {
  std::string file(sizeof(header) + payloadSize, '\0');
  memcpy(&file[0], &header, sizeof(header));
  return file;
}

std::string makeG5File(const G5ImageHeader& header, const std::vector<uint8_t>& payload) {
  std::string file = makeG5File(header, payload.size());
  memcpy(&file[sizeof(header)], payload.data(), payload.size());
  return file;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("G5ImageCache Tests");
  G5ImageHeader parsed{};

  {
    SdMan.reset();
    const G5ImageHeader header{G5_MAGIC, 0, 8, 1};
    SdMan.registerFile("/zero.g5", makeG5File(header, 1));
    runner.expectFalse(G5ImageCache::readHeader("/zero.g5", parsed), "header: rejects zero dimensions");
  }

  {
    SdMan.reset();
    const G5ImageHeader header{G5_MAGIC, UINT16_MAX, UINT16_MAX, 1};
    SdMan.registerFile("/huge.g5", makeG5File(header, 1));
    runner.expectFalse(G5ImageCache::readHeader("/huge.g5", parsed), "header: rejects decoded bitmap above limit");
  }

  {
    SdMan.reset();
    const G5ImageHeader header{G5_MAGIC, static_cast<uint16_t>(INT16_MAX + 1), 1, 1};
    SdMan.registerFile("/wide.g5", makeG5File(header, 1));
    runner.expectFalse(G5ImageCache::readHeader("/wide.g5", parsed),
                       "header: rejects width outside decoder flip range");
  }

  {
    SdMan.reset();
    const G5ImageHeader header{G5_MAGIC, 8, 8, 512 * 1024 + 1};
    SdMan.registerFile("/oversized.g5", makeG5File(header, 0));
    runner.expectFalse(G5ImageCache::readHeader("/oversized.g5", parsed),
                       "header: rejects compressed size above limit");
  }

  {
    SdMan.reset();
    const G5ImageHeader header{G5_MAGIC, 8, 8, 64};
    SdMan.registerFile("/truncated.g5", makeG5File(header, 1));
    runner.expectFalse(G5ImageCache::readHeader("/truncated.g5", parsed),
                       "header: rejects compressed size beyond file");
    int callbacks = 0;
    runner.expectFalse(
        G5ImageCache::decompressFromFile("/truncated.g5", [&](const uint8_t*, int, int) { callbacks++; }),
        "decompress: rejects truncated payload");
    runner.expectEq(0, callbacks, "decompress: truncated payload invokes no callbacks");
  }

  {
    SdMan.reset();
    const std::vector<uint8_t> rows = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
    runner.expectTrue(G5ImageCache::compressToFile(rows.data(), 8, 8, "/roundtrip.g5"),
                      "roundtrip: compression succeeds");
    SdMan.registerFile("/roundtrip.g5", SdMan.getWrittenData("/roundtrip.g5"));

    std::vector<uint8_t> decoded;
    const bool ok = G5ImageCache::decompressFromFile("/roundtrip.g5", [&](const uint8_t* row, int rowBytes, int y) {
      runner.expectEq(1, rowBytes, "roundtrip: callback row width");
      runner.expectEq(static_cast<int>(decoded.size()), y, "roundtrip: callback row index");
      decoded.push_back(row[0]);
    });
    runner.expectTrue(ok, "roundtrip: decompression succeeds");
    runner.expectEq(rows.size(), decoded.size(), "roundtrip: callback count");
    runner.expectTrue(decoded == rows, "roundtrip: decoded rows match input");
  }

  {
    SdMan.reset();
    const std::vector<uint8_t> rows = {0x00, 0x00, 0xFF, 0xFF, 0xF0, 0x0F};
    runner.expectTrue(G5ImageCache::compressToFile(rows.data(), 16, 3, "/aligned.g5"),
                      "aligned: compression succeeds");
    SdMan.registerFile("/aligned.g5", SdMan.getWrittenData("/aligned.g5"));

    std::vector<uint8_t> decoded;
    const bool ok = G5ImageCache::decompressFromFile("/aligned.g5", [&](const uint8_t* row, int rowBytes, int) {
      decoded.insert(decoded.end(), row, row + rowBytes);
    });
    runner.expectTrue(ok, "aligned: decompression succeeds");
    runner.expectTrue(decoded == rows, "aligned: byte-boundary runs round-trip");
  }

  {
    uint8_t payload[] = {0};
    uint8_t row[] = {0};
    G5DECODER decoder;
    runner.expectEq(static_cast<int>(G5_SUCCESS), decoder.init(8, 1, payload, sizeof(payload)),
                    "decoder: accepts an exact short buffer");
    runner.expectTrue(decoder.decodeLine(row) != G5_SUCCESS, "decoder: exact short buffer fails safely");
  }

  {
    SdMan.reset();
    const G5ImageHeader header{G5_MAGIC, 8, 1, 1};
    SdMan.registerFile("/short-vlc.g5", makeG5File(header, 1));
    int callbacks = 0;
    runner.expectFalse(G5ImageCache::decompressFromFile("/short-vlc.g5", [&](const uint8_t*, int, int) {
                         callbacks++;
                       }),
                       "decoder: short bitstream fails safely");
    runner.expectEq(0, callbacks, "decoder: short bitstream invokes no callbacks");
  }

  {
    SdMan.reset();
    // Repeated horizontal short/short codes with zero-length runs consume bits
    // without advancing the row, forcing the decoder to enforce the VLC limit.
    std::vector<uint8_t> payload(11, 0);
    for (size_t code = 0; code < 8; ++code) {
      const size_t oneBit = code * 11 + 2;
      payload[oneBit / 8] |= static_cast<uint8_t>(0x80 >> (oneBit % 8));
    }
    const G5ImageHeader header{G5_MAGIC, 8, 1, static_cast<uint32_t>(payload.size())};
    SdMan.registerFile("/zero-runs.g5", makeG5File(header, payload));
    int callbacks = 0;
    runner.expectFalse(G5ImageCache::decompressFromFile("/zero-runs.g5", [&](const uint8_t*, int, int) {
                         callbacks++;
                       }),
                       "decoder: zero-progress stream stops at input boundary");
    runner.expectEq(0, callbacks, "decoder: zero-progress stream invokes no callbacks");
  }

  {
    SdMan.reset();
    std::vector<uint8_t> payload(5, 0);
    for (const size_t bit : {size_t{2}, size_t{13}, size_t{14}}) {
      payload[bit / 8] |= static_cast<uint8_t>(0x80 >> (bit % 8));
    }
    const G5ImageHeader header{G5_MAGIC, INT16_MAX, 1, static_cast<uint32_t>(payload.size())};
    SdMan.registerFile("/run-boundary.g5", makeG5File(header, payload));
    int callbacks = 0;
    runner.expectFalse(G5ImageCache::decompressFromFile("/run-boundary.g5", [&](const uint8_t*, int, int) {
                         callbacks++;
                       }),
                       "decoder: rejects a run that crosses the VLC window");
    runner.expectEq(0, callbacks, "decoder: VLC boundary stream invokes no callbacks");
  }

  {
    SdMan.reset();
    constexpr size_t codes = MAX_IMAGE_FLIPS / 2 + 1;
    std::vector<uint8_t> payload((codes * 11 + 7) / 8, 0);
    for (size_t code = 0; code < codes; ++code) {
      const size_t oneBit = code * 11 + 2;
      payload[oneBit / 8] |= static_cast<uint8_t>(0x80 >> (oneBit % 8));
    }
    const G5ImageHeader header{G5_MAGIC, 8, 1, static_cast<uint32_t>(payload.size())};
    SdMan.registerFile("/flip-overflow.g5", makeG5File(header, payload));
    int callbacks = 0;
    runner.expectFalse(G5ImageCache::decompressFromFile("/flip-overflow.g5", [&](const uint8_t*, int, int) {
                         callbacks++;
                       }),
                       "decoder: rejects flip-buffer exhaustion");
    runner.expectEq(0, callbacks, "decoder: overflow stream invokes no callbacks");
  }

  return runner.allPassed() ? 0 : 1;
}
