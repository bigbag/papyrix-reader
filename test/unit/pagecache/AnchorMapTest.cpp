#include "test_utils.h"

#include <climits>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Include mocks before the library
#include "HardwareSerial.h"
#include "SDCardManager.h"
#include "SdFat.h"

// Now include the serialization header
#include "Serialization.h"

// Include ContentParser base class to test default getAnchorMap()
#include "ContentParser.h"

// ============================================================================
// Anchor map write/read helpers (mirrors ReaderState::saveAnchorMap/loadAnchorPage)
// ============================================================================
constexpr uint32_t kAnchorMapMagic = 0x48434E41;
constexpr uint8_t kAnchorMapVersion = 1;


static bool writeAnchorMap(FsFile& file, const std::vector<std::pair<std::string, uint32_t>>& anchors) {
  bool writeOk = serialization::writePodChecked(file, kAnchorMapMagic) &&
                 serialization::writePodChecked(file, kAnchorMapVersion);
  if (anchors.size() > UINT16_MAX) {
    const uint16_t zero = 0;
    writeOk = writeOk && serialization::writePodChecked(file, zero);
  } else {
    const uint16_t count = static_cast<uint16_t>(anchors.size());
    writeOk = writeOk && serialization::writePodChecked(file, count);
    for (const auto& entry : anchors) {
      writeOk = writeOk && serialization::writeStringChecked(file, entry.first) &&
                serialization::writePodChecked(file, entry.second);
    }
  }
  return writeOk && file.sync();
}

static int readAnchorPage(FsFile& file, const std::string& anchor) {
  uint32_t magic;
  uint8_t version;
  uint16_t count;
  if (!serialization::readPodChecked(file, magic) || magic != kAnchorMapMagic ||
      !serialization::readPodChecked(file, version) || version != kAnchorMapVersion ||
      !serialization::readPodChecked(file, count)) {
    return -1;
  }

  for (uint16_t i = 0; i < count; i++) {
    std::string anchorId;
    uint32_t page;
    if (!serialization::readString(file, anchorId) || !serialization::readPodChecked(file, page)) {
      return -1;
    }
    if (anchorId == anchor) {
      return page <= static_cast<uint32_t>(INT_MAX) ? static_cast<int>(page) : -1;
    }
  }

  return -1;
}

// Minimal ContentParser subclass for testing getAnchorMap override
class MockContentParserWithAnchors : public ContentParser {
 public:
  std::vector<std::pair<std::string, uint32_t>> anchors;

  bool parsePages(const std::function<void(std::unique_ptr<Page>)>&, uint32_t,
                  const AbortCallback&) override {
    return true;
  }
  bool hasMoreContent() const override { return false; }
  void reset() override {}

  const std::vector<std::pair<std::string, uint32_t>>& getAnchorMap() const override { return anchors; }
};

// Minimal ContentParser subclass that does NOT override getAnchorMap (tests default)
class MockContentParserDefault : public ContentParser {
 public:
  bool parsePages(const std::function<void(std::unique_ptr<Page>)>&, uint32_t,
                  const AbortCallback&) override {
    return true;
  }
  bool hasMoreContent() const override { return false; }
  void reset() override {}
};

int main() {
  TestUtils::TestRunner runner("AnchorMap");

  // ============================================
  // Anchor map serialization roundtrip tests
  // ============================================

  // Test 1: Basic roundtrip - write anchors, read back specific one
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"chapter1", 0}, {"section1", 5}, {"section2", 12}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "section1");
    runner.expectEq(5, page, "roundtrip: finds section1 at page 5");
  }

  // Test 2: Read first anchor
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"first", 0}, {"middle", 10}, {"last", 20}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "first");
    runner.expectEq(0, page, "roundtrip: finds first anchor at page 0");
  }

  // Test 3: Read last anchor
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"first", 0}, {"middle", 10}, {"last", 20}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "last");
    runner.expectEq(20, page, "roundtrip: finds last anchor at page 20");
  }

  // Test 4: Missing anchor returns -1
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"chapter1", 0}, {"chapter2", 5}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "nonexistent");
    runner.expectEq(-1, page, "missing_anchor: returns -1");
  }

  // Test 5: Empty anchor map
  {
    std::vector<std::pair<std::string, uint32_t>> anchors;

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "anything");
    runner.expectEq(-1, page, "empty_map: returns -1");
  }

  // Test 6: Empty file returns -1
  {
    FsFile file;
    file.setBuffer("");

    int page = readAnchorPage(file, "anything");
    runner.expectEq(-1, page, "empty_file: returns -1");
  }

  // Test 7: Truncated file (count says 5, but only 1 entry) returns -1 for missing
  {
    FsFile file;
    file.setBuffer("");

    // Write a valid header and count = 5, but only 1 entry
    serialization::writePod(file, kAnchorMapMagic);
    serialization::writePod(file, kAnchorMapVersion);
    uint16_t fakeCount = 5;
    serialization::writePod(file, fakeCount);
    serialization::writeString(file, std::string("only-one"));
    uint32_t page = 3;
    serialization::writePod(file, page);

    file.seek(0);
    // First entry is found
    int result = readAnchorPage(file, "only-one");
    runner.expectEq(3, result, "truncated: finds existing anchor");

    // Second search should fail on truncated data
    file.seek(0);
    result = readAnchorPage(file, "missing");
    runner.expectEq(-1, result, "truncated: returns -1 for missing anchor");
  }

  // Test 8: Single anchor roundtrip
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {{"solo", 42}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "solo");
    runner.expectEq(42, page, "single_anchor: correct page");
  }

  // Test 9: Anchor with special characters
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"sec-1.2", 3}, {"id_with_underscores", 7}, {"CamelCaseId", 15}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "sec-1.2");
    runner.expectEq(3, page, "special_chars: finds hyphen-dot anchor");

    file.seek(0);
    page = readAnchorPage(file, "CamelCaseId");
    runner.expectEq(15, page, "special_chars: finds camelCase anchor");
  }

  // Test 10: Multiple anchors on same page
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"anchor-a", 5}, {"anchor-b", 5}, {"anchor-c", 5}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "anchor-b");
    runner.expectEq(5, page, "same_page: all anchors on page 5");
  }

  // ============================================
  // ContentParser::getAnchorMap() tests
  // ============================================

  // Test 11: Default ContentParser returns empty anchor map
  {
    MockContentParserDefault parser;
    const auto& anchors = parser.getAnchorMap();
    runner.expectEq(static_cast<size_t>(0), anchors.size(), "default_parser: empty anchor map");
  }

  // Test 12: Overridden getAnchorMap returns populated anchors
  {
    MockContentParserWithAnchors parser;
    parser.anchors = {{"ch1", 0}, {"ch2", 10}};

    const auto& anchors = parser.getAnchorMap();
    runner.expectEq(static_cast<size_t>(2), anchors.size(), "override_parser: two anchors");
    runner.expectEqual("ch1", anchors[0].first, "override_parser: first anchor id");
    runner.expectEq(static_cast<uint32_t>(0), anchors[0].second, "override_parser: first anchor page");
    runner.expectEqual("ch2", anchors[1].first, "override_parser: second anchor id");
    runner.expectEq(static_cast<uint32_t>(10), anchors[1].second, "override_parser: second anchor page");
  }

  // Test 13: Anchor map serialization preserves page indexes beyond uint16_t
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"wide-page", 70000}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    int page = readAnchorPage(file, "wide-page");
    runner.expectEq(70000, page, "wide_page: preserves page indexes above 65535");
  }

  // Test 14: Page indexes that cannot fit the navigation API are rejected
  {
    std::vector<std::pair<std::string, uint32_t>> anchors = {
        {"too-wide", static_cast<uint32_t>(INT_MAX) + 1u}};

    FsFile file;
    file.setBuffer("");
    writeAnchorMap(file, anchors);

    file.seek(0);
    runner.expectEq(-1, readAnchorPage(file, "too-wide"), "wide_page: rejects indexes above INT_MAX");
  }

  // Test 15: Short writes are rejected
  {
    FsFile file;
    file.setBuffer("");
    file.setWriteLimit(1);
    runner.expectFalse(writeAnchorMap(file, {{"anchor", 1}}), "short_write: rejected");
  }

  // Test 16: Sync failures are rejected
  {
    FsFile file;
    file.setBuffer("");
    file.setSyncResult(false);
    runner.expectFalse(writeAnchorMap(file, {{"anchor", 1}}), "sync_failure: rejected");
  }

  return runner.allPassed() ? 0 : 1;
}
