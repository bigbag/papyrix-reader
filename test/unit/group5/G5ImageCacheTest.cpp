#include "test_utils.h"

#include <G5ImageCache.h>
#include <SDCardManager.h>

#include <cstring>
#include <string>
#include <vector>

namespace {

std::string makeG5File(const G5ImageHeader& header, size_t payloadSize) {
  std::string file(sizeof(header) + payloadSize, '\0');
  memcpy(&file[0], &header, sizeof(header));
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

  return runner.allPassed() ? 0 : 1;
}
