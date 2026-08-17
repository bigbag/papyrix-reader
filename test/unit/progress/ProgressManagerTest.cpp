#include "test_utils.h"

#include <array>
#include <climits>
#include <cstdint>

// ContentType enum (matches src/core/Types.h exactly)
enum class ContentType : uint8_t { None = 0, Epub, Xtc, Txt, Markdown, Fb2, Html };

// Progress struct matching ProgressManager::Progress
struct Progress {
  int spineIndex = 0;
  int sectionPage = 0;
  uint32_t flatPage = 0;
};

constexpr uint32_t kProgressMagic = 0x47525050;
constexpr uint8_t kProgressVersion = 1;
using ProgressRecord = std::array<uint8_t, 14>;

static void writeU32(ProgressRecord& data, size_t offset, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) data[offset + i] = static_cast<uint8_t>(value >> (i * 8));
}

static uint32_t readU32(const ProgressRecord& data, size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  return value;
}

static ProgressRecord encodeProgress(ContentType type, const Progress& progress) {
  ProgressRecord data{};
  writeU32(data, 0, kProgressMagic);
  data[4] = kProgressVersion;
  data[5] = static_cast<uint8_t>(type);
  writeU32(data, 6, static_cast<uint32_t>(progress.spineIndex));
  const uint32_t page = type == ContentType::Xtc
                            ? progress.flatPage
                            : (progress.sectionPage > 0 ? static_cast<uint32_t>(progress.sectionPage) : 0);
  writeU32(data, 10, page);
  return data;
}

static Progress decodeProgress(ContentType type, const ProgressRecord& data, size_t size = ProgressRecord{}.size()) {
  Progress progress;
  if (size != data.size() || readU32(data, 0) != kProgressMagic || data[4] != kProgressVersion ||
      data[5] != static_cast<uint8_t>(type)) {
    return progress;
  }
  const uint32_t page = readU32(data, 10);
  if (type != ContentType::Xtc && page > static_cast<uint32_t>(INT_MAX)) return progress;
  if (type == ContentType::Epub || type == ContentType::Fb2) {
    progress.spineIndex = static_cast<int32_t>(readU32(data, 6));
    progress.sectionPage = static_cast<int>(page);
  } else if (type == ContentType::Xtc) {
    progress.flatPage = page;
  } else {
    progress.sectionPage = static_cast<int>(page);
  }
  return progress;
}

static Progress decodeLegacyProgress(ContentType type, const std::array<uint8_t, 4>& data) {
  Progress progress;
  if (type == ContentType::Epub || type == ContentType::Fb2) {
    progress.spineIndex = static_cast<int>(data[0]) | (static_cast<int>(data[1]) << 8);
    progress.sectionPage = static_cast<int>(data[2]) | (static_cast<int>(data[3]) << 8);
  } else if (type == ContentType::Xtc) {
    progress.flatPage = static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                        (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
  } else {
    progress.sectionPage = static_cast<int>(data[0]) | (static_cast<int>(data[1]) << 8);
  }
  return progress;
}

// Mirrors ProgressManager::validate() logic (no Core dependency)
static Progress validateProgress(ContentType type, Progress p, int spineCount) {
  Progress v = p;
  if (type == ContentType::Epub || type == ContentType::Fb2) {
    if (v.spineIndex < 0) v.spineIndex = 0;
    if (v.spineIndex >= spineCount) {
      if (type == ContentType::Fb2) {
        v.spineIndex = 0;
        v.sectionPage = 0;
      } else {
        v.spineIndex = spineCount > 0 ? spineCount - 1 : 0;
        v.sectionPage = 0;
      }
    }
  }
  return v;
}

int main() {
  TestUtils::TestRunner runner("ProgressManager");

  // Test 1: FB2 pages beyond the legacy uint16_t limit round-trip
  {
    Progress p;
    p.spineIndex = 3;
    p.sectionPage = 70000;
    const Progress r = decodeProgress(ContentType::Fb2, encodeProgress(ContentType::Fb2, p));
    runner.expectEq(3, r.spineIndex, "FB2 encode/decode: spineIndex round-trips");
    runner.expectEq(70000, r.sectionPage, "FB2 encode/decode: wide sectionPage round-trips");
  }

  // Test 2: FB2 encode/decode zeros
  {
    Progress p;
    const Progress r = decodeProgress(ContentType::Fb2, encodeProgress(ContentType::Fb2, p));
    runner.expectEq(0, r.spineIndex, "FB2 encode/decode zero: spineIndex=0");
    runner.expectEq(0, r.sectionPage, "FB2 encode/decode zero: sectionPage=0");
  }

  // Test 3: EPUB pages beyond the legacy uint16_t limit round-trip
  {
    Progress p;
    p.spineIndex = 255;
    p.sectionPage = 100000;
    const Progress r = decodeProgress(ContentType::Epub, encodeProgress(ContentType::Epub, p));
    runner.expectEq(255, r.spineIndex, "EPUB encode/decode: spineIndex=255");
    runner.expectEq(100000, r.sectionPage, "EPUB encode/decode: wide sectionPage round-trips");
  }

  // Test 4: FB2 validate — spineIndex in bounds unchanged
  {
    Progress p;
    p.spineIndex = 2;
    p.sectionPage = 7;
    Progress v = validateProgress(ContentType::Fb2, p, 5);
    runner.expectEq(2, v.spineIndex, "FB2 validate in-bounds: spineIndex unchanged");
    runner.expectEq(7, v.sectionPage, "FB2 validate in-bounds: sectionPage unchanged");
  }

  // Test 5: FB2 validate — negative spineIndex clamped to 0
  {
    Progress p;
    p.spineIndex = -1;
    p.sectionPage = 10;
    Progress v = validateProgress(ContentType::Fb2, p, 5);
    runner.expectEq(0, v.spineIndex, "FB2 validate negative: spineIndex clamped to 0");
    runner.expectEq(10, v.sectionPage, "FB2 validate negative: sectionPage unchanged");
  }

  // Test 6: FB2 validate — spineIndex == sectionCount resets to (0,0) [legacy migration]
  {
    Progress p;
    p.spineIndex = 5;
    p.sectionPage = 3;
    Progress v = validateProgress(ContentType::Fb2, p, 5);
    runner.expectEq(0, v.spineIndex, "FB2 validate ==sectionCount: reset to 0 (legacy migration)");
    runner.expectEq(0, v.sectionPage, "FB2 validate ==sectionCount: sectionPage reset to 0");
  }

  // Test 7: FB2 validate — spineIndex > sectionCount resets to (0,0)
  {
    Progress p;
    p.spineIndex = 99;
    p.sectionPage = 12;
    Progress v = validateProgress(ContentType::Fb2, p, 5);
    runner.expectEq(0, v.spineIndex, "FB2 validate >sectionCount: reset to 0");
    runner.expectEq(0, v.sectionPage, "FB2 validate >sectionCount: sectionPage reset to 0");
  }

  // Test 8: FB2 validate — zero sections resets to (0,0)
  {
    Progress p;
    p.spineIndex = 0;
    p.sectionPage = 5;
    Progress v = validateProgress(ContentType::Fb2, p, 0);
    runner.expectEq(0, v.spineIndex, "FB2 validate zero sections: reset to 0");
    runner.expectEq(0, v.sectionPage, "FB2 validate zero sections: sectionPage reset to 0");
  }

  // Test 9: EPUB validate — spineIndex >= spineCount clamped to last, not reset
  {
    Progress p;
    p.spineIndex = 7;
    p.sectionPage = 3;
    Progress v = validateProgress(ContentType::Epub, p, 5);
    runner.expectEq(4, v.spineIndex, "EPUB validate >=spineCount: clamped to spineCount-1");
    runner.expectEq(0, v.sectionPage, "EPUB validate >=spineCount: sectionPage reset to 0");
  }

  // Test 10: EPUB validate — zero spines clamps to 0
  {
    Progress p;
    p.spineIndex = 0;
    p.sectionPage = 3;
    Progress v = validateProgress(ContentType::Epub, p, 0);
    runner.expectEq(0, v.spineIndex, "EPUB validate zero spines: clamped to 0");
    runner.expectEq(0, v.sectionPage, "EPUB validate zero spines: sectionPage reset to 0");
  }

  // Test 11: flat text pages beyond the legacy uint16_t limit round-trip
  {
    Progress p;
    p.sectionPage = 90000;
    const Progress r = decodeProgress(ContentType::Txt, encodeProgress(ContentType::Txt, p));
    runner.expectEq(90000, r.sectionPage, "TXT encode/decode: wide sectionPage round-trips");
  }

  // Test 12: Negative section pages are stored as zero
  {
    Progress p;
    p.sectionPage = -1;
    const Progress r = decodeProgress(ContentType::Txt, encodeProgress(ContentType::Txt, p));
    runner.expectEq(0, r.sectionPage, "TXT encode/decode: negative sectionPage clamps to zero");
  }

  // Test 13: New-format metadata mismatches are rejected
  {
    Progress p;
    p.sectionPage = 7;
    ProgressRecord badMagic = encodeProgress(ContentType::Txt, p);
    badMagic[0] ^= 1u;
    runner.expectEq(0, decodeProgress(ContentType::Txt, badMagic).sectionPage,
                    "new format: bad magic is rejected");

    ProgressRecord badVersion = encodeProgress(ContentType::Txt, p);
    badVersion[4]++;
    runner.expectEq(0, decodeProgress(ContentType::Txt, badVersion).sectionPage,
                    "new format: bad version is rejected");

    const ProgressRecord wrongType = encodeProgress(ContentType::Markdown, p);
    runner.expectEq(0, decodeProgress(ContentType::Txt, wrongType).sectionPage,
                    "new format: wrong content type is rejected");

    const ProgressRecord truncated = encodeProgress(ContentType::Txt, p);
    runner.expectEq(0, decodeProgress(ContentType::Txt, truncated, truncated.size() - 1).sectionPage,
                    "new format: truncated record is rejected");
  }

  // Test 14: Page indexes above the signed navigation range are rejected
  {
    ProgressRecord data = encodeProgress(ContentType::Txt, {});
    writeU32(data, 10, static_cast<uint32_t>(INT_MAX) + 1u);
    runner.expectEq(0, decodeProgress(ContentType::Txt, data).sectionPage,
                    "new format: page above INT_MAX is rejected");
  }

  // Test 15: Legacy four-byte records remain readable
  {
    const Progress epub = decodeLegacyProgress(ContentType::Epub, {2, 0, 42, 0});
    runner.expectEq(2, epub.spineIndex, "legacy EPUB: spine index is decoded");
    runner.expectEq(42, epub.sectionPage, "legacy EPUB: section page is decoded");

    const Progress xtc = decodeLegacyProgress(ContentType::Xtc, {0x78, 0x56, 0x34, 0x12});
    runner.expectEq(static_cast<uint32_t>(0x12345678), xtc.flatPage,
                    "legacy XTC: flat page is decoded");

    const Progress text = decodeLegacyProgress(ContentType::Txt, {0x34, 0x12, 0xFF, 0xFF});
    runner.expectEq(0x1234, text.sectionPage, "legacy TXT: low 16-bit page is decoded");
  }

  return runner.allPassed() ? 0 : 1;
}
