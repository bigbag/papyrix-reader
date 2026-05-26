// SpineSplitTest: validates the section boundary scanning and grouping logic
// used to split large single-spine EPUBs into manageable sections.
//
// Tests use Expat directly to validate the scan algorithm, replicating the
// key logic from Epub::scanSectionBoundaries() and the grouping pass.

#include "test_utils.h"

#include <expat.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

struct SectionRange {
  uint32_t startOffset = 0;
  uint32_t endOffset = 0;
};

struct ScanCtx {
  std::vector<SectionRange>* splitPoints;
  std::unordered_map<std::string, uint32_t>* anchors;
  XML_Parser parser = nullptr;
  int depth = 0;
  bool inBody = false;
  int bodyDepth = 0;
  bool currentElementHasHeading = false;
  uint32_t currentElementStart = 0;
  int splitDepth = 0;
};

static void XMLCALL onStart(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* c = static_cast<ScanCtx*>(userData);
  c->depth++;

  if (strcmp(name, "body") == 0) {
    c->inBody = true;
    c->bodyDepth = c->depth;
    return;
  }
  if (!c->inBody) return;

  if (atts) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "id") == 0 && atts[i + 1][0] != '\0') {
        int64_t byteIdx = XML_GetCurrentByteIndex(c->parser);
        if (byteIdx >= 0) {
          (*c->anchors)[atts[i + 1]] = static_cast<uint32_t>(byteIdx);
        }
      }
    }
  }

  const int targetDepth = c->bodyDepth + 1 + c->splitDepth;
  if (c->depth == targetDepth) {
    int64_t byteIdx = XML_GetCurrentByteIndex(c->parser);
    if (byteIdx >= 0) {
      c->currentElementStart = static_cast<uint32_t>(byteIdx);
      c->currentElementHasHeading = false;
    }
  }

  if (c->depth > c->bodyDepth) {
    if (strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 || strcmp(name, "h3") == 0 ||
        strcmp(name, "h4") == 0 || strcmp(name, "h5") == 0 || strcmp(name, "h6") == 0) {
      c->currentElementHasHeading = true;
    }
  }
}

static void XMLCALL onEnd(void* userData, const XML_Char* name) {
  auto* c = static_cast<ScanCtx*>(userData);

  const int targetDepth = c->bodyDepth + 1 + c->splitDepth;
  if (c->inBody && c->depth == targetDepth) {
    int64_t byteIdx = XML_GetCurrentByteIndex(c->parser);
    if (byteIdx >= 0) {
      uint32_t endOffset = static_cast<uint32_t>(byteIdx) + static_cast<uint32_t>(XML_GetCurrentByteCount(c->parser));
      SectionRange sp;
      sp.startOffset = c->currentElementStart;
      sp.endOffset = endOffset;
      if (c->currentElementHasHeading) {
        sp.startOffset |= 0x80000000u;
      }
      c->splitPoints->push_back(sp);
    }
  }

  c->depth--;
}

static bool scanHtml(const char* html, std::vector<SectionRange>& points,
                     std::unordered_map<std::string, uint32_t>& anchors, int splitDepth = 0) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (!parser) return false;

  ScanCtx ctx;
  ctx.splitPoints = &points;
  ctx.anchors = &anchors;
  ctx.parser = parser;
  ctx.splitDepth = splitDepth;

  XML_SetUserData(parser, &ctx);
  XML_SetElementHandler(parser, onStart, onEnd);

  bool ok = XML_Parse(parser, html, static_cast<int>(strlen(html)), 1) != XML_STATUS_ERROR;
  XML_ParserFree(parser);
  return ok;
}

static std::vector<SectionRange> groupSections(const std::vector<SectionRange>& elements, size_t maxSize,
                                               size_t minSize) {
  std::vector<SectionRange> grouped;
  uint32_t sectionStart = 0;
  uint32_t cumulativeSize = 0;
  bool first = true;

  for (const auto& elem : elements) {
    const bool hasHeading = (elem.startOffset & 0x80000000u) != 0;
    const uint32_t start = elem.startOffset & 0x7FFFFFFFu;
    const uint32_t end = elem.endOffset;
    const uint32_t size = end - start;

    if (first) {
      sectionStart = start;
      cumulativeSize = size;
      first = false;
      continue;
    }

    bool shouldSplit = false;
    if (hasHeading && cumulativeSize >= minSize) shouldSplit = true;
    if (cumulativeSize + size > maxSize && cumulativeSize >= minSize) shouldSplit = true;

    if (shouldSplit) {
      grouped.push_back({sectionStart, start});
      sectionStart = start;
      cumulativeSize = size;
    } else {
      cumulativeSize += size;
    }
  }

  if (!first) {
    uint32_t lastEnd = elements.back().endOffset;
    grouped.push_back({sectionStart, lastEnd});
  }
  return grouped;
}

int main() {
  TestUtils::TestRunner runner("Spine Split");

  // Test 1: Simple body with 3 children
  {
    const char* html = "<html><body><div>a</div><div>b</div><div>c</div></body></html>";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors);
    runner.expectEq(static_cast<size_t>(3), points.size(), "3 body-child elements");
  }

  // Test 2: Heading detection sets flag
  {
    const char* html = "<html><body><div><h1>Title</h1></div><div>content</div></body></html>";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors);
    runner.expectEq(static_cast<size_t>(2), points.size(), "heading_detect_count");
    runner.expectTrue((points[0].startOffset & 0x80000000u) != 0, "heading_detect_first_has_heading");
    runner.expectTrue((points[1].startOffset & 0x80000000u) == 0, "heading_detect_second_no_heading");
  }

  // Test 3: Anchor tracking
  {
    const char* html = R"(<html><body><div id="ch1">text</div><div id="ch2">text</div></body></html>)";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors);
    runner.expectEq(static_cast<size_t>(2), anchors.size(), "anchor_count");
    runner.expectTrue(anchors.count("ch1") > 0, "anchor_ch1_found");
    runner.expectTrue(anchors.count("ch2") > 0, "anchor_ch2_found");
  }

  // Test 4: Grouping splits at headings
  {
    std::vector<SectionRange> elements;
    elements.push_back({0 | 0x80000000u, 100});
    elements.push_back({100, 200});
    elements.push_back({200 | 0x80000000u, 300});
    elements.push_back({300, 400});

    auto grouped = groupSections(elements, 10000, 10);
    runner.expectEq(static_cast<size_t>(2), grouped.size(), "group_heading_split_count");
    runner.expectEq(static_cast<uint32_t>(0), grouped[0].startOffset, "group_heading_split_start0");
    runner.expectEq(static_cast<uint32_t>(200), grouped[0].endOffset, "group_heading_split_end0");
    runner.expectEq(static_cast<uint32_t>(200), grouped[1].startOffset, "group_heading_split_start1");
    runner.expectEq(static_cast<uint32_t>(400), grouped[1].endOffset, "group_heading_split_end1");
  }

  // Test 5: Grouping splits at size cap
  {
    std::vector<SectionRange> elements;
    for (int i = 0; i < 10; i++) {
      elements.push_back({static_cast<uint32_t>(i * 100), static_cast<uint32_t>((i + 1) * 100)});
    }

    auto grouped = groupSections(elements, 300, 50);
    runner.expectTrue(grouped.size() >= 3, "group_size_cap_min_sections");
    for (const auto& s : grouped) {
      runner.expectTrue(s.endOffset - s.startOffset <= 400, "group_size_cap_section_not_too_large");
    }
  }

  // Test 6: Min section size prevents tiny splits
  {
    std::vector<SectionRange> elements;
    elements.push_back({0 | 0x80000000u, 5});
    elements.push_back({5 | 0x80000000u, 10});
    elements.push_back({10, 200});

    auto grouped = groupSections(elements, 10000, 50);
    runner.expectEq(static_cast<size_t>(1), grouped.size(), "min_size_prevents_tiny_split");
  }

  // Test 7: Deeper scan (splitDepth=1) finds grandchild elements
  {
    const char* html = "<html><body><div><p>a</p><p>b</p><p>c</p></div></body></html>";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors, 1);
    runner.expectEq(static_cast<size_t>(3), points.size(), "deep_scan_finds_grandchildren");
  }

  // Test 8: Empty body produces no split points
  {
    const char* html = "<html><body></body></html>";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors);
    runner.expectEq(static_cast<size_t>(0), points.size(), "empty_body");
  }

  // Test 9: Self-closing elements (pagebreak divs) are tracked
  {
    const char* html = R"(<html><body><div id="p1"/><div>text</div><div id="p2"/></body></html>)";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors);
    runner.expectEq(static_cast<size_t>(3), points.size(), "self_closing_tracked");
    runner.expectEq(static_cast<size_t>(2), anchors.size(), "self_closing_anchors");
  }

  // Test 10: Nested headings at different levels all detected
  {
    const char* html = "<html><body><div><h2>Sub</h2></div><div><h3>Sub2</h3></div></body></html>";
    std::vector<SectionRange> points;
    std::unordered_map<std::string, uint32_t> anchors;
    scanHtml(html, points, anchors);
    runner.expectEq(static_cast<size_t>(2), points.size(), "nested_headings_count");
    runner.expectTrue((points[0].startOffset & 0x80000000u) != 0, "h2_detected");
    runner.expectTrue((points[1].startOffset & 0x80000000u) != 0, "h3_detected");
  }

  return runner.allPassed() ? 0 : 1;
}
