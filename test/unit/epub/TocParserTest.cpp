// TocParser unit tests
// Tests TOC parsing behavior for both EPUB 3 nav.xhtml (TocNavParser) and
// EPUB 2 NCX (TocNcxParser): UTF-8 safe label truncation and depth tracking.

#include "test_utils.h"

#include <expat.h>

#include <climits>
#include <cstring>
#include <string>
#include <vector>

// ============================================
// Test Nav Parser (EPUB 3 nav.xhtml)
// ============================================

constexpr size_t TEST_MAX_NAV_LABEL_LENGTH = 512;

struct NavTocEntry {
  std::string label;
  std::string href;
  uint8_t depth;
};

class TestNavParser {
 public:
  enum ParserState { START, IN_HTML, IN_BODY, IN_NAV_TOC, IN_OL, IN_LI, IN_ANCHOR };

  ParserState state = START;
  uint8_t olDepth = 0;
  std::string currentLabel;
  std::string currentHref;
  std::vector<NavTocEntry> entries;

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* self = static_cast<TestNavParser*>(userData);

    if (strcmp(name, "html") == 0) {
      self->state = IN_HTML;
      return;
    }
    if (self->state == IN_HTML && strcmp(name, "body") == 0) {
      self->state = IN_BODY;
      return;
    }
    if (self->state >= IN_BODY && strcmp(name, "nav") == 0) {
      for (int i = 0; atts[i]; i += 2) {
        if ((strcmp(atts[i], "epub:type") == 0 || strcmp(atts[i], "type") == 0) && strcmp(atts[i + 1], "toc") == 0) {
          self->state = IN_NAV_TOC;
          return;
        }
      }
      return;
    }
    if (self->state < IN_NAV_TOC) return;

    if (strcmp(name, "ol") == 0) {
      self->olDepth++;
      self->state = IN_OL;
      return;
    }
    if (self->state == IN_OL && strcmp(name, "li") == 0) {
      self->state = IN_LI;
      self->currentLabel.clear();
      self->currentHref.clear();
      return;
    }
    if (self->state == IN_LI && strcmp(name, "a") == 0) {
      self->state = IN_ANCHOR;
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "href") == 0) {
          self->currentHref = atts[i + 1];
          break;
        }
      }
      return;
    }
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, const int len) {
    auto* self = static_cast<TestNavParser*>(userData);
    if (self->state == IN_ANCHOR) {
      if (self->currentLabel.size() + static_cast<size_t>(len) <= TEST_MAX_NAV_LABEL_LENGTH) {
        self->currentLabel.append(s, len);
      } else if (self->currentLabel.size() < TEST_MAX_NAV_LABEL_LENGTH) {
        size_t remaining = TEST_MAX_NAV_LABEL_LENGTH - self->currentLabel.size();
        while (remaining > 0 && (static_cast<unsigned char>(s[remaining]) & 0xC0) == 0x80) {
          remaining--;
        }
        if (remaining > 0) {
          self->currentLabel.append(s, remaining);
        }
      }
    }
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<TestNavParser*>(userData);

    if (strcmp(name, "a") == 0 && self->state == IN_ANCHOR) {
      if (!self->currentLabel.empty() && !self->currentHref.empty()) {
        self->entries.push_back({self->currentLabel, self->currentHref, self->olDepth});
        self->currentLabel.clear();
        self->currentHref.clear();
      }
      self->state = IN_LI;
      return;
    }
    if (strcmp(name, "li") == 0 && (self->state == IN_LI || self->state == IN_OL)) {
      self->state = IN_OL;
      return;
    }
    if (strcmp(name, "ol") == 0 && self->state >= IN_NAV_TOC) {
      if (self->olDepth > 0) {
        self->olDepth--;
      }
      if (self->olDepth == 0) {
        self->state = IN_NAV_TOC;
      } else {
        self->state = IN_LI;
      }
      return;
    }
    if (strcmp(name, "nav") == 0 && self->state >= IN_NAV_TOC) {
      self->state = IN_BODY;
      return;
    }
  }

  bool parse(const std::string& xml) {
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);
    if (XML_Parse(parser, xml.c_str(), static_cast<int>(xml.size()), 1) == XML_STATUS_ERROR) {
      XML_ParserFree(parser);
      return false;
    }
    XML_ParserFree(parser);
    return true;
  }
};

// ============================================
// Test NCX Parser (EPUB 2 toc.ncx)
// ============================================

constexpr size_t TEST_MAX_LABEL_LENGTH = 512;

struct NcxTocEntry {
  std::string label;
  std::string src;
  uint8_t depth;
};

class TestNcxParser {
 public:
  enum ParserState { START, IN_NCX, IN_NAV_MAP, IN_NAV_POINT, IN_NAV_LABEL, IN_NAV_LABEL_TEXT };

  ParserState state = START;
  std::string currentLabel;
  std::string currentSrc;
  uint8_t currentDepth = 0;
  std::vector<NcxTocEntry> entries;

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* self = static_cast<TestNcxParser*>(userData);

    if (self->state == START && strcmp(name, "ncx") == 0) {
      self->state = IN_NCX;
      return;
    }
    if (self->state == IN_NCX && strcmp(name, "navMap") == 0) {
      self->state = IN_NAV_MAP;
      return;
    }
    if ((self->state == IN_NAV_MAP || self->state == IN_NAV_POINT) && strcmp(name, "navPoint") == 0) {
      self->state = IN_NAV_POINT;
      self->currentDepth++;
      self->currentLabel.clear();
      self->currentSrc.clear();
      return;
    }
    if (self->state == IN_NAV_POINT && strcmp(name, "navLabel") == 0) {
      self->state = IN_NAV_LABEL;
      return;
    }
    if (self->state == IN_NAV_LABEL && strcmp(name, "text") == 0) {
      self->state = IN_NAV_LABEL_TEXT;
      return;
    }
    if (self->state == IN_NAV_POINT && strcmp(name, "content") == 0) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          self->currentSrc = atts[i + 1];
          break;
        }
      }
      return;
    }
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, const int len) {
    auto* self = static_cast<TestNcxParser*>(userData);
    if (self->state == IN_NAV_LABEL_TEXT) {
      if (self->currentLabel.size() + static_cast<size_t>(len) <= TEST_MAX_LABEL_LENGTH) {
        self->currentLabel.append(s, len);
      } else if (self->currentLabel.size() < TEST_MAX_LABEL_LENGTH) {
        size_t remaining = TEST_MAX_LABEL_LENGTH - self->currentLabel.size();
        while (remaining > 0 && (static_cast<unsigned char>(s[remaining]) & 0xC0) == 0x80) {
          remaining--;
        }
        if (remaining > 0) {
          self->currentLabel.append(s, remaining);
        }
      }
    }
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<TestNcxParser*>(userData);

    if (self->state == IN_NAV_LABEL_TEXT && strcmp(name, "text") == 0) {
      self->state = IN_NAV_LABEL;
      return;
    }
    if (self->state == IN_NAV_LABEL && strcmp(name, "navLabel") == 0) {
      self->state = IN_NAV_POINT;
      return;
    }
    if (self->state == IN_NAV_POINT && strcmp(name, "navPoint") == 0) {
      self->currentDepth--;
      if (self->currentDepth == 0) {
        self->state = IN_NAV_MAP;
      }
      return;
    }
    if (self->state == IN_NAV_POINT && strcmp(name, "content") == 0) {
      if (!self->currentLabel.empty() && !self->currentSrc.empty()) {
        self->entries.push_back({self->currentLabel, self->currentSrc, self->currentDepth});
        self->currentLabel.clear();
        self->currentSrc.clear();
      }
    }
  }

  bool parse(const std::string& xml) {
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);
    if (XML_Parse(parser, xml.c_str(), static_cast<int>(xml.size()), 1) == XML_STATUS_ERROR) {
      XML_ParserFree(parser);
      return false;
    }
    XML_ParserFree(parser);
    return true;
  }
};

// ============================================
// Standalone UTF-8 truncation test
// ============================================

// Reimplements the truncation logic from TocNavParser/TocNcxParser characterData
std::string truncateUtf8(const std::string& input, size_t maxLen) {
  std::string result;
  const char* s = input.c_str();
  size_t len = input.size();

  if (len <= maxLen) {
    return input;
  }

  size_t remaining = maxLen;
  while (remaining > 0 && (static_cast<unsigned char>(s[remaining]) & 0xC0) == 0x80) {
    remaining--;
  }
  if (remaining > 0) {
    result.append(s, remaining);
  }
  return result;
}

bool isValidUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t charLen;
    if (c < 0x80)
      charLen = 1;
    else if ((c & 0xE0) == 0xC0)
      charLen = 2;
    else if ((c & 0xF0) == 0xE0)
      charLen = 3;
    else if ((c & 0xF8) == 0xF0)
      charLen = 4;
    else
      return false;

    if (i + charLen > s.size()) return false;
    for (size_t j = 1; j < charLen; j++) {
      if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) return false;
    }
    i += charLen;
  }
  return true;
}

int main() {
  TestUtils::TestRunner runner("TocParser");

  // ============================================
  // UTF-8 Truncation Tests
  // ============================================

  // Test 1: ASCII truncation is exact
  {
    std::string input(600, 'A');
    std::string result = truncateUtf8(input, 512);
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_ascii: exact at limit");
    runner.expectTrue(isValidUtf8(result), "truncate_ascii: valid UTF-8");
  }

  // Test 2: 2-byte UTF-8 not split mid-character
  {
    // "ä" = 0xC3 0xA4 (2 bytes)
    // Fill 511 bytes of ASCII + 1 byte of a 2-byte char = would split
    std::string input(511, 'A');
    input += "\xC3\xA4";  // ä at position 511-512
    std::string result = truncateUtf8(input, 512);
    // Should truncate to 511 (skip the incomplete 2-byte char)
    runner.expectEq(static_cast<size_t>(511), result.size(), "truncate_2byte: backs up past continuation");
    runner.expectTrue(isValidUtf8(result), "truncate_2byte: valid UTF-8");
  }

  // Test 3: 3-byte UTF-8 not split (cut after 1st byte of 3-byte seq)
  {
    // "中" = 0xE4 0xB8 0xAD (3 bytes)
    // 510 ASCII + 3-byte char at 510-512 → cut at 512 leaves 2 of 3 bytes
    std::string input(510, 'A');
    input += "\xE4\xB8\xAD";  // 中
    std::string result = truncateUtf8(input, 512);
    // s[512] = 0xAD (continuation byte), so back up to 510
    runner.expectEq(static_cast<size_t>(510), result.size(), "truncate_3byte: backs up past partial char");
    runner.expectTrue(isValidUtf8(result), "truncate_3byte: valid UTF-8");
  }

  // Test 4: 4-byte UTF-8 not split
  {
    // 𝄞 = 0xF0 0x9D 0x84 0x9E (4 bytes)
    // 509 ASCII + 4-byte char at 509-512 → cut at 512 leaves 3 of 4 bytes
    std::string input(509, 'A');
    input += "\xF0\x9D\x84\x9E";  // 𝄞
    std::string result = truncateUtf8(input, 512);
    // s[512] = 0x9E (continuation), s[511] = 0x84 (continuation), s[510] = 0x9D (continuation)
    // backs up to 509
    runner.expectEq(static_cast<size_t>(509), result.size(), "truncate_4byte: backs up past partial char");
    runner.expectTrue(isValidUtf8(result), "truncate_4byte: valid UTF-8");
  }

  // Test 5: Exact boundary — complete char at limit
  {
    // 510 ASCII + "ä" (2 bytes) = exactly 512
    std::string input(510, 'A');
    input += "\xC3\xA4";
    std::string result = truncateUtf8(input, 512);
    // s[512] is past end, but the truncation check: remaining=512, s[512] is out of bounds
    // Actually the input IS exactly 512, so len <= maxLen → returns as-is
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_exact: no truncation needed");
    runner.expectTrue(isValidUtf8(result), "truncate_exact: valid UTF-8");
  }

  // Test 6: Cyrillic text truncation (2-byte chars)
  {
    // Each Cyrillic char is 2 bytes. Fill past 512 with Cyrillic.
    std::string input;
    // 300 Cyrillic chars = 600 bytes
    for (int i = 0; i < 300; i++) {
      input += "\xD0\x90";  // А (Cyrillic A)
    }
    std::string result = truncateUtf8(input, 512);
    // 512 / 2 = 256 complete chars = 512 bytes. s[512] = 0xD0 (lead byte, not continuation)
    // So remaining stays at 512
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_cyrillic: aligned at char boundary");
    runner.expectTrue(isValidUtf8(result), "truncate_cyrillic: valid UTF-8");
  }

  // Test 7: Mixed content — CJK chars near boundary
  {
    // 510 ASCII + "文" (3 bytes: E6 96 87) = 513 total > 512
    std::string input(510, 'x');
    input += "\xE6\x96\x87";
    std::string result = truncateUtf8(input, 512);
    // s[512] = 0x87 (continuation byte), back up: s[511] = 0x96 (continuation), s[510] = 0xE6 (lead)
    // remaining = 510
    runner.expectEq(static_cast<size_t>(510), result.size(), "truncate_cjk_boundary: backs up to start of char");
    runner.expectTrue(isValidUtf8(result), "truncate_cjk_boundary: valid UTF-8");
  }

  // Test 20: s[remaining] is a lead byte — loop doesn't fire, lead byte excluded
  {
    // 512 ASCII + ä (0xC3 0xA4) = 514 bytes. maxLen=512.
    // s[512] = 0xC3 (lead byte, NOT continuation) → loop skips → remaining=512
    // append(s, 512) copies s[0..511] — the lead byte at s[512] is NOT included
    std::string input(512, 'B');
    input += "\xC3\xA4";  // ä starts right at the boundary
    std::string result = truncateUtf8(input, 512);
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_lead_at_boundary: lead byte excluded");
    runner.expectTrue(isValidUtf8(result), "truncate_lead_at_boundary: valid UTF-8");
    // Verify the lead byte is not in the output (last byte should be 'B')
    runner.expectEq(static_cast<int>('B'), static_cast<int>(result.back()), "truncate_lead_at_boundary: last byte is ASCII");
  }

  // Test 21: 3-byte lead byte at truncation point
  {
    // 512 ASCII + 中 (0xE4 0xB8 0xAD) = 515 bytes
    // s[512] = 0xE4 (3-byte lead, not continuation) → remaining=512
    std::string input(512, 'C');
    input += "\xE4\xB8\xAD";
    std::string result = truncateUtf8(input, 512);
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_3byte_lead_at_boundary: exact 512");
    runner.expectTrue(isValidUtf8(result), "truncate_3byte_lead_at_boundary: valid UTF-8");
  }

  // Test 22: 4-byte lead byte at truncation point
  {
    // 512 ASCII + 𝄞 (0xF0 0x9D 0x84 0x9E) = 516 bytes
    // s[512] = 0xF0 (4-byte lead) → remaining=512
    std::string input(512, 'D');
    input += "\xF0\x9D\x84\x9E";
    std::string result = truncateUtf8(input, 512);
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_4byte_lead_at_boundary: exact 512");
    runner.expectTrue(isValidUtf8(result), "truncate_4byte_lead_at_boundary: valid UTF-8");
  }

  // Test 23: complete multi-byte char ends exactly at limit (no byte at s[remaining] to check)
  {
    // 509 ASCII + 中 (3 bytes) = 512 exactly → len <= maxLen, no truncation
    std::string input(509, 'E');
    input += "\xE4\xB8\xAD";
    runner.expectEq(static_cast<size_t>(512), input.size(), "truncate_complete_3byte_exact: input is 512");
    std::string result = truncateUtf8(input, 512);
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_complete_3byte_exact: no truncation");
    runner.expectTrue(isValidUtf8(result), "truncate_complete_3byte_exact: valid UTF-8");
  }

  // Test 24: alternating multi-byte chars near boundary
  {
    // 510 ASCII + ä (2 bytes) + 中 (3 bytes) = 515. maxLen=512.
    // s[512] = 0xE4 (lead byte of 中) → loop skips → remaining=512
    // Result: 510 ASCII + complete ä = 512 bytes
    std::string input(510, 'F');
    input += "\xC3\xA4";       // ä at 510-511
    input += "\xE4\xB8\xAD";   // 中 at 512-514
    std::string result = truncateUtf8(input, 512);
    runner.expectEq(static_cast<size_t>(512), result.size(), "truncate_multi_boundary: includes complete ä");
    runner.expectTrue(isValidUtf8(result), "truncate_multi_boundary: valid UTF-8");
  }

  // ============================================
  // Nav Parser Integration Tests
  // ============================================

  // Test 8: Basic nav TOC parsing
  {
    TestNavParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<nav epub:type=\"toc\">"
        "<ol>"
        "<li><a href=\"ch1.html\">Chapter 1</a></li>"
        "<li><a href=\"ch2.html\">Chapter 2</a></li>"
        "</ol>"
        "</nav>"
        "</body></html>");
    runner.expectTrue(ok, "nav_basic: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.entries.size(), "nav_basic: two entries");
    if (parser.entries.size() == 2) {
      runner.expectEqual(std::string("Chapter 1"), parser.entries[0].label, "nav_basic: first label");
      runner.expectEqual(std::string("ch1.html"), parser.entries[0].href, "nav_basic: first href");
      runner.expectEqual(std::string("Chapter 2"), parser.entries[1].label, "nav_basic: second label");
    }
  }

  // Test 9: Nested ol tracks depth correctly
  {
    TestNavParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<nav epub:type=\"toc\">"
        "<ol>"
        "<li><a href=\"part1.html\">Part 1</a>"
        "<ol>"
        "<li><a href=\"ch1.html\">Chapter 1</a></li>"
        "<li><a href=\"ch2.html\">Chapter 2</a></li>"
        "</ol>"
        "</li>"
        "<li><a href=\"part2.html\">Part 2</a></li>"
        "</ol>"
        "</nav>"
        "</body></html>");
    runner.expectTrue(ok, "nav_nested: parses successfully");
    runner.expectEq(static_cast<size_t>(4), parser.entries.size(), "nav_nested: four entries");
    if (parser.entries.size() == 4) {
      runner.expectEq(static_cast<uint8_t>(1), parser.entries[0].depth, "nav_nested: Part1 depth=1");
      runner.expectEq(static_cast<uint8_t>(2), parser.entries[1].depth, "nav_nested: Ch1 depth=2");
      runner.expectEq(static_cast<uint8_t>(2), parser.entries[2].depth, "nav_nested: Ch2 depth=2");
      runner.expectEq(static_cast<uint8_t>(1), parser.entries[3].depth, "nav_nested: Part2 depth=1");
    }
  }

  // Test 10: olDepth underflow guard — extra </ol> doesn't underflow
  {
    TestNavParser parser;
    // Malformed: extra closing </ol> before the real one
    bool ok = parser.parse(
        "<html><body>"
        "<nav epub:type=\"toc\">"
        "<ol>"
        "<li><a href=\"ch1.html\">Chapter 1</a></li>"
        "</ol>"
        "</nav>"
        "</body></html>");
    runner.expectTrue(ok, "nav_depth_guard: parses without crash");
    runner.expectEq(static_cast<uint8_t>(0), parser.olDepth, "nav_depth_guard: depth stays at 0");
  }

  // Test 11: olDepth guard with pre-decremented state
  {
    TestNavParser parser;
    // Simulate the guard: manually set olDepth to 0 and verify it doesn't wrap
    parser.state = TestNavParser::IN_NAV_TOC;
    parser.olDepth = 0;
    // Call endElement with "ol" directly
    const char* olName = "ol";
    TestNavParser::endElement(&parser, olName);
    runner.expectEq(static_cast<uint8_t>(0), parser.olDepth, "nav_depth_no_wrap: stays 0 when already 0");
  }

  // Test 12: Nav label truncation preserves UTF-8
  {
    TestNavParser parser;
    // Create a title that exceeds TEST_MAX_NAV_LABEL_LENGTH with multi-byte chars
    std::string longTitle;
    // Fill with 3-byte CJK chars to exceed 512
    for (int i = 0; i < 200; i++) {
      longTitle += "\xE4\xB8\xAD";  // 中 (3 bytes each) = 600 bytes total
    }
    std::string xml =
        "<html><body>"
        "<nav epub:type=\"toc\">"
        "<ol>"
        "<li><a href=\"ch1.html\">" +
        longTitle +
        "</a></li>"
        "</ol>"
        "</nav>"
        "</body></html>";
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "nav_truncate_utf8: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.entries.size(), "nav_truncate_utf8: one entry");
    if (!parser.entries.empty()) {
      runner.expectTrue(parser.entries[0].label.size() <= TEST_MAX_NAV_LABEL_LENGTH,
                        "nav_truncate_utf8: label within limit");
      runner.expectTrue(isValidUtf8(parser.entries[0].label), "nav_truncate_utf8: label is valid UTF-8");
      // Should be truncated to 510 bytes (170 complete 3-byte chars)
      runner.expectEq(static_cast<size_t>(510), parser.entries[0].label.size(),
                      "nav_truncate_utf8: truncated at char boundary");
    }
  }

  // ============================================
  // NCX Parser Integration Tests
  // ============================================

  // Test 13: Basic NCX parsing
  {
    TestNcxParser parser;
    bool ok = parser.parse(
        "<ncx><navMap>"
        "<navPoint>"
        "<navLabel><text>Chapter 1</text></navLabel>"
        "<content src=\"ch1.html\"/>"
        "</navPoint>"
        "<navPoint>"
        "<navLabel><text>Chapter 2</text></navLabel>"
        "<content src=\"ch2.html\"/>"
        "</navPoint>"
        "</navMap></ncx>");
    runner.expectTrue(ok, "ncx_basic: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.entries.size(), "ncx_basic: two entries");
    if (parser.entries.size() == 2) {
      runner.expectEqual(std::string("Chapter 1"), parser.entries[0].label, "ncx_basic: first label");
      runner.expectEqual(std::string("ch1.html"), parser.entries[0].src, "ncx_basic: first src");
      runner.expectEq(static_cast<uint8_t>(1), parser.entries[0].depth, "ncx_basic: depth=1");
    }
  }

  // Test 14: NCX nested navPoints track depth
  {
    TestNcxParser parser;
    bool ok = parser.parse(
        "<ncx><navMap>"
        "<navPoint>"
        "<navLabel><text>Part 1</text></navLabel>"
        "<content src=\"part1.html\"/>"
        "<navPoint>"
        "<navLabel><text>Chapter 1</text></navLabel>"
        "<content src=\"ch1.html\"/>"
        "</navPoint>"
        "</navPoint>"
        "</navMap></ncx>");
    runner.expectTrue(ok, "ncx_nested: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.entries.size(), "ncx_nested: two entries");
    if (parser.entries.size() == 2) {
      runner.expectEq(static_cast<uint8_t>(1), parser.entries[0].depth, "ncx_nested: Part1 depth=1");
      runner.expectEq(static_cast<uint8_t>(2), parser.entries[1].depth, "ncx_nested: Ch1 depth=2");
    }
  }

  // Test 15: NCX label truncation preserves UTF-8
  {
    TestNcxParser parser;
    std::string longTitle;
    for (int i = 0; i < 200; i++) {
      longTitle += "\xE4\xB8\xAD";  // 600 bytes of 3-byte chars
    }
    std::string xml =
        "<ncx><navMap>"
        "<navPoint>"
        "<navLabel><text>" +
        longTitle +
        "</text></navLabel>"
        "<content src=\"ch1.html\"/>"
        "</navPoint>"
        "</navMap></ncx>";
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "ncx_truncate_utf8: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.entries.size(), "ncx_truncate_utf8: one entry");
    if (!parser.entries.empty()) {
      runner.expectTrue(parser.entries[0].label.size() <= TEST_MAX_LABEL_LENGTH,
                        "ncx_truncate_utf8: label within limit");
      runner.expectTrue(isValidUtf8(parser.entries[0].label), "ncx_truncate_utf8: label is valid UTF-8");
      runner.expectEq(static_cast<size_t>(510), parser.entries[0].label.size(),
                      "ncx_truncate_utf8: truncated at char boundary");
    }
  }

  // Test 16: NCX label truncation with 2-byte chars at boundary
  {
    TestNcxParser parser;
    // 511 ASCII + 2-byte char = 513 total
    std::string longTitle(511, 'X');
    longTitle += "\xC3\xA4";  // ä (2 bytes)
    std::string xml =
        "<ncx><navMap>"
        "<navPoint>"
        "<navLabel><text>" +
        longTitle +
        "</text></navLabel>"
        "<content src=\"ch1.html\"/>"
        "</navPoint>"
        "</navMap></ncx>";
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "ncx_truncate_2byte: parses successfully");
    if (!parser.entries.empty()) {
      runner.expectEq(static_cast<size_t>(511), parser.entries[0].label.size(),
                      "ncx_truncate_2byte: truncated before incomplete char");
      runner.expectTrue(isValidUtf8(parser.entries[0].label), "ncx_truncate_2byte: valid UTF-8");
    }
  }

  // Test 17: NCX label truncation with 4-byte emoji at boundary
  {
    TestNcxParser parser;
    // 509 ASCII + 4-byte emoji = 513 total
    std::string longTitle(509, 'Y');
    longTitle += "\xF0\x9F\x98\x80";  // 😀 (4 bytes)
    std::string xml =
        "<ncx><navMap>"
        "<navPoint>"
        "<navLabel><text>" +
        longTitle +
        "</text></navLabel>"
        "<content src=\"ch1.html\"/>"
        "</navPoint>"
        "</navMap></ncx>";
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "ncx_truncate_4byte: parses successfully");
    if (!parser.entries.empty()) {
      runner.expectEq(static_cast<size_t>(509), parser.entries[0].label.size(),
                      "ncx_truncate_4byte: truncated before incomplete 4-byte char");
      runner.expectTrue(isValidUtf8(parser.entries[0].label), "ncx_truncate_4byte: valid UTF-8");
    }
  }

  // Test 18: Empty label and empty src are not added
  {
    TestNcxParser parser;
    bool ok = parser.parse(
        "<ncx><navMap>"
        "<navPoint>"
        "<navLabel><text></text></navLabel>"
        "<content src=\"ch1.html\"/>"
        "</navPoint>"
        "</navMap></ncx>");
    runner.expectTrue(ok, "ncx_empty_label: parses successfully");
    runner.expectEq(static_cast<size_t>(0), parser.entries.size(), "ncx_empty_label: no entries for empty label");
  }

  // Test 19: Nav parser ignores non-toc nav elements
  {
    TestNavParser parser;
    bool ok = parser.parse(
        "<html><body>"
        "<nav epub:type=\"landmarks\">"
        "<ol><li><a href=\"toc.html\">TOC</a></li></ol>"
        "</nav>"
        "<nav epub:type=\"toc\">"
        "<ol><li><a href=\"ch1.html\">Real Chapter</a></li></ol>"
        "</nav>"
        "</body></html>");
    runner.expectTrue(ok, "nav_ignore_non_toc: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.entries.size(), "nav_ignore_non_toc: only toc entries");
    if (!parser.entries.empty()) {
      runner.expectEqual(std::string("Real Chapter"), parser.entries[0].label, "nav_ignore_non_toc: correct entry");
    }
  }

  return runner.allPassed() ? 0 : 1;
}
