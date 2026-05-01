#include "test_utils.h"

#include <cstdint>

// ContentType enum (matches src/core/Types.h exactly)
enum class ContentType : uint8_t { None = 0, Epub, Xtc, Txt, Markdown, Fb2, Html };

// Progress struct matching ProgressManager::Progress
struct Progress {
  int spineIndex = 0;
  int sectionPage = 0;
  uint32_t flatPage = 0;
};

// Mirrors ProgressManager save/load encoding (EPUB/FB2 branch)
static void encodeSpineBased(Progress p, uint8_t data[4]) {
  data[0] = p.spineIndex & 0xFF;
  data[1] = (p.spineIndex >> 8) & 0xFF;
  data[2] = p.sectionPage & 0xFF;
  data[3] = (p.sectionPage >> 8) & 0xFF;
}

static Progress decodeSpineBased(const uint8_t data[4]) {
  Progress p;
  p.spineIndex = data[0] | (data[1] << 8);
  p.sectionPage = data[2] | (data[3] << 8);
  return p;
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

  // Test 1: FB2 encode/decode round-trip
  {
    uint8_t data[4];
    Progress p;
    p.spineIndex = 3;
    p.sectionPage = 42;
    encodeSpineBased(p, data);
    Progress r = decodeSpineBased(data);
    runner.expectEq(3, r.spineIndex, "FB2 encode/decode: spineIndex round-trips");
    runner.expectEq(42, r.sectionPage, "FB2 encode/decode: sectionPage round-trips");
  }

  // Test 2: FB2 encode/decode zeros
  {
    uint8_t data[4];
    Progress p;
    encodeSpineBased(p, data);
    Progress r = decodeSpineBased(data);
    runner.expectEq(0, r.spineIndex, "FB2 encode/decode zero: spineIndex=0");
    runner.expectEq(0, r.sectionPage, "FB2 encode/decode zero: sectionPage=0");
  }

  // Test 3: FB2 encode/decode large values
  {
    uint8_t data[4];
    Progress p;
    p.spineIndex = 255;
    p.sectionPage = 1000;
    encodeSpineBased(p, data);
    Progress r = decodeSpineBased(data);
    runner.expectEq(255, r.spineIndex, "FB2 encode/decode: spineIndex=255");
    runner.expectEq(1000, r.sectionPage, "FB2 encode/decode: sectionPage=1000");
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

  return runner.allPassed() ? 0 : 1;
}
