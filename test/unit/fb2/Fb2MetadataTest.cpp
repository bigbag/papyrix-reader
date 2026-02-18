// Fb2 metadata and TOC unit tests
//
// Tests FB2 XML parsing logic for metadata extraction (title, author)
// and TOC building by reimplementing the key parsing rules from Fb2.cpp
// in a test-friendly way, without needing SD card or Serial dependencies.

#include "test_utils.h"

#include <expat.h>

#include <climits>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

// Mirrors Fb2::TocItem
struct TocItem {
  std::string title;
  int sectionIndex;
};

// Lightweight FB2 metadata/TOC parser that mirrors Fb2.cpp parsing logic
// without SD card, Serial, or file I/O dependencies.
class TestFb2Parser {
  int depth = 0;
  int skipUntilDepth = INT_MAX;

  // Metadata state
  bool inTitleInfo = false;
  bool inBookTitle = false;
  bool inFirstName = false;
  bool inLastName = false;
  bool inAuthor = false;
  std::string currentAuthorFirst;
  std::string currentAuthorLast;

  // TOC state
  bool inBody = false;
  int bodyCount_ = 0;
  int sectionCounter_ = 0;
  bool inSectionTitle_ = false;
  int sectionTitleDepth_ = 0;
  std::string currentSectionTitle_;

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* self = static_cast<TestFb2Parser*>(userData);
    (void)atts;

    self->depth++;

    if (self->skipUntilDepth < self->depth) {
      return;
    }

    // Strip namespace prefix
    const char* tag = strrchr(name, ':');
    if (tag) {
      tag++;
    } else {
      tag = name;
    }

    if (strcmp(tag, "title-info") == 0) {
      self->inTitleInfo = true;
    }

    if (strcmp(tag, "book-title") == 0 && self->inTitleInfo) {
      self->inBookTitle = true;
    } else if (strcmp(tag, "author") == 0 && self->inTitleInfo) {
      self->inAuthor = true;
      self->currentAuthorFirst.clear();
      self->currentAuthorLast.clear();
    } else if (strcmp(tag, "first-name") == 0 && self->inAuthor) {
      self->inFirstName = true;
    } else if (strcmp(tag, "last-name") == 0 && self->inAuthor) {
      self->inLastName = true;
    } else if (strcmp(tag, "body") == 0) {
      self->bodyCount_++;
      self->inBody = (self->bodyCount_ == 1);
    } else if (strcmp(tag, "section") == 0 && self->inBody) {
      self->sectionCounter_++;
    } else if (strcmp(tag, "title") == 0 && self->inBody && self->sectionCounter_ > 0) {
      self->inSectionTitle_ = true;
      self->sectionTitleDepth_ = self->depth;
      self->currentSectionTitle_.clear();
    }
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<TestFb2Parser*>(userData);

    const char* tag = strrchr(name, ':');
    if (tag) {
      tag++;
    } else {
      tag = name;
    }

    if (strcmp(tag, "title-info") == 0) {
      self->inTitleInfo = false;
    }

    if (strcmp(tag, "book-title") == 0) {
      self->inBookTitle = false;
    } else if (strcmp(tag, "first-name") == 0) {
      self->inFirstName = false;
    } else if (strcmp(tag, "last-name") == 0) {
      self->inLastName = false;
    } else if (strcmp(tag, "author") == 0 && self->inAuthor) {
      std::string fullAuthor;
      if (!self->currentAuthorFirst.empty()) {
        fullAuthor = self->currentAuthorFirst;
        if (!self->currentAuthorLast.empty()) {
          fullAuthor += " ";
        }
      }
      fullAuthor += self->currentAuthorLast;

      if (!fullAuthor.empty()) {
        if (!self->author.empty()) {
          self->author += ", ";
        }
        self->author += fullAuthor;
      }

      self->inAuthor = false;
      self->currentAuthorFirst.clear();
      self->currentAuthorLast.clear();
    } else if (strcmp(tag, "body") == 0) {
      self->inBody = false;
    } else if (strcmp(tag, "title") == 0 && self->inSectionTitle_ && self->depth == self->sectionTitleDepth_) {
      self->inSectionTitle_ = false;

      std::string& t = self->currentSectionTitle_;
      for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '\n' || t[i] == '\r') {
          t[i] = ' ';
        }
      }
      // Trim leading whitespace
      size_t start = 0;
      while (start < t.size() && isspace(static_cast<unsigned char>(t[start]))) {
        start++;
      }
      // Trim trailing whitespace
      size_t end = t.size();
      while (end > start && isspace(static_cast<unsigned char>(t[end - 1]))) {
        end--;
      }
      if (start > 0 || end < t.size()) {
        t = t.substr(start, end - start);
      }

      if (!t.empty()) {
        self->tocItems.push_back({t, self->sectionCounter_ - 1});
      }
    }

    self->depth--;
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, int len) {
    auto* self = static_cast<TestFb2Parser*>(userData);

    if (self->skipUntilDepth < self->depth) {
      return;
    }

    if (self->inSectionTitle_) {
      self->currentSectionTitle_.append(s, len);
    }

    if (self->inBookTitle) {
      self->title.append(s, len);
    } else if (self->inFirstName) {
      self->currentAuthorFirst.append(s, len);
    } else if (self->inLastName) {
      self->currentAuthorLast.append(s, len);
    }
  }

 public:
  std::string title;
  std::string author;
  std::vector<TocItem> tocItems;

  bool parse(const std::string& xml) {
    XML_Parser parser = XML_ParserCreate("UTF-8");
    if (!parser) return false;

    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);

    if (XML_Parse(parser, xml.c_str(), static_cast<int>(xml.size()), 1) == XML_STATUS_ERROR) {
      XML_ParserFree(parser);
      return false;
    }

    // Post-process title (same as Fb2::parseXmlContent)
    while (!title.empty() && isspace(static_cast<unsigned char>(title.back()))) {
      title.pop_back();
    }
    while (!title.empty() && isspace(static_cast<unsigned char>(title.front()))) {
      title.erase(title.begin());
    }
    for (size_t i = 0; i < title.size(); i++) {
      if (title[i] == '\n' || title[i] == '\r') {
        title[i] = ' ';
      }
    }

    XML_ParserFree(parser);
    return true;
  }
};

// Pure logic: title extraction from filepath (mirrors Fb2 constructor)
static std::string extractTitle(const std::string& filepath) {
  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');

  if (lastSlash == std::string::npos) {
    lastSlash = 0;
  } else {
    lastSlash++;
  }

  if (lastDot == std::string::npos || lastDot <= lastSlash) {
    return filepath.substr(lastSlash);
  } else {
    return filepath.substr(lastSlash, lastDot - lastSlash);
  }
}

// Pure logic: cache path generation (mirrors Fb2 constructor)
static std::string generateCachePath(const std::string& cacheDir, const std::string& filepath) {
  return cacheDir + "/fb2_" + std::to_string(std::hash<std::string>{}(filepath));
}

// Helper to build minimal FB2 XML
static std::string makeFb2(const std::string& descriptionContent, const std::string& bodyContent = "") {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" "
         "xmlns:l=\"http://www.w3.org/1999/xlink\">"
         "<description><title-info>" +
         descriptionContent +
         "</title-info></description>"
         "<body>" +
         bodyContent +
         "</body>"
         "</FictionBook>";
}

int main() {
  TestUtils::TestRunner runner("Fb2 Metadata and TOC");

  // ============================================
  // Metadata extraction (using Expat)
  // ============================================

  // Test 1: Extract title from <book-title>
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<book-title>Test Book</book-title>"));
    runner.expectTrue(ok, "basic_title: parses successfully");
    runner.expectEqual("Test Book", parser.title, "basic_title: correct title");
  }

  // Test 2: Extract author from <first-name> + <last-name>
  {
    TestFb2Parser parser;
    bool ok = parser.parse(
        makeFb2("<author><first-name>John</first-name><last-name>Doe</last-name></author>"
                "<book-title>Test</book-title>"));
    runner.expectTrue(ok, "basic_author: parses successfully");
    runner.expectEqual("John Doe", parser.author, "basic_author: correct author");
  }

  // Test 3: Multi-author: two <author> blocks -> comma-separated
  {
    TestFb2Parser parser;
    bool ok = parser.parse(
        makeFb2("<author><first-name>John</first-name><last-name>Doe</last-name></author>"
                "<author><first-name>Jane</first-name><last-name>Smith</last-name></author>"
                "<book-title>Collab Book</book-title>"));
    runner.expectTrue(ok, "multi_author: parses successfully");
    runner.expectEqual("John Doe, Jane Smith", parser.author, "multi_author: comma-separated");
  }

  // Test 4: Missing title -> empty string
  {
    TestFb2Parser parser;
    bool ok = parser.parse(
        makeFb2("<author><first-name>John</first-name><last-name>Doe</last-name></author>"));
    runner.expectTrue(ok, "missing_title: parses successfully");
    runner.expectEqual("", parser.title, "missing_title: empty string");
  }

  // Test 5: UTF-8 characters in title and author
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<author><first-name>\xD0\x9B\xD0\xB5\xD0\xB2</first-name>"
        "<last-name>\xD0\xA2\xD0\xBE\xD0\xBB\xD1\x81\xD1\x82\xD0\xBE\xD0\xB9</last-name></author>"
        "<book-title>\xD0\x92\xD0\xBE\xD0\xB9\xD0\xBD\xD0\xB0 \xD0\xB8 \xD0\xBC\xD0\xB8\xD1\x80</book-title>"));
    runner.expectTrue(ok, "utf8_metadata: parses successfully");
    runner.expectEqual("\xD0\x92\xD0\xBE\xD0\xB9\xD0\xBD\xD0\xB0 \xD0\xB8 \xD0\xBC\xD0\xB8\xD1\x80", parser.title,
                       "utf8_metadata: UTF-8 title");
    runner.expectEqual(
        "\xD0\x9B\xD0\xB5\xD0\xB2 \xD0\xA2\xD0\xBE\xD0\xBB\xD1\x81\xD1\x82\xD0\xBE\xD0\xB9", parser.author,
        "utf8_metadata: UTF-8 author");
  }

  // Test 6: Author with only first name (no last name)
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<author><first-name>Madonna</first-name></author>"));
    runner.expectTrue(ok, "first_name_only: parses successfully");
    runner.expectEqual("Madonna", parser.author, "first_name_only: just first name");
  }

  // Test 7: Author with only last name (no first name)
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<author><last-name>Anonymous</last-name></author>"));
    runner.expectTrue(ok, "last_name_only: parses successfully");
    runner.expectEqual("Anonymous", parser.author, "last_name_only: just last name");
  }

  // Test 8: No author element -> empty string
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<book-title>Orphan Book</book-title>"));
    runner.expectTrue(ok, "no_author: parses successfully");
    runner.expectEqual("", parser.author, "no_author: empty string");
  }

  // Test 9: Title with surrounding whitespace gets trimmed
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<book-title>  Spaced Title  </book-title>"));
    runner.expectTrue(ok, "title_trim: parses successfully");
    runner.expectEqual("Spaced Title", parser.title, "title_trim: whitespace trimmed");
  }

  // ============================================
  // TOC extraction (using Expat)
  // ============================================

  // Test 10: Three sections with titles -> 3 TocItems
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<book-title>Book</book-title>",
        "<section><title><p>Chapter 1</p></title><p>Text</p></section>"
        "<section><title><p>Chapter 2</p></title><p>More text</p></section>"
        "<section><title><p>Chapter 3</p></title><p>Even more</p></section>"));
    runner.expectTrue(ok, "toc_three_sections: parses successfully");
    runner.expectEq(static_cast<size_t>(3), parser.tocItems.size(), "toc_three_sections: 3 items");
    runner.expectEqual("Chapter 1", parser.tocItems[0].title, "toc_three_sections: first title");
    runner.expectEqual("Chapter 2", parser.tocItems[1].title, "toc_three_sections: second title");
    runner.expectEqual("Chapter 3", parser.tocItems[2].title, "toc_three_sections: third title");
    runner.expectEq(0, parser.tocItems[0].sectionIndex, "toc_three_sections: first index");
    runner.expectEq(1, parser.tocItems[1].sectionIndex, "toc_three_sections: second index");
    runner.expectEq(2, parser.tocItems[2].sectionIndex, "toc_three_sections: third index");
  }

  // Test 11: Section without title -> no TocItem for that section
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<book-title>Book</book-title>",
        "<section><title><p>Chapter 1</p></title><p>Text</p></section>"
        "<section><p>No title section</p></section>"
        "<section><title><p>Chapter 3</p></title><p>Text</p></section>"));
    runner.expectTrue(ok, "toc_no_title_section: parses successfully");
    runner.expectEq(static_cast<size_t>(2), parser.tocItems.size(), "toc_no_title_section: 2 items (skipped untitled)");
    runner.expectEqual("Chapter 1", parser.tocItems[0].title, "toc_no_title_section: first title");
    runner.expectEqual("Chapter 3", parser.tocItems[1].title, "toc_no_title_section: second title");
    runner.expectEq(0, parser.tocItems[0].sectionIndex, "toc_no_title_section: first index");
    runner.expectEq(2, parser.tocItems[1].sectionIndex, "toc_no_title_section: third section index");
  }

  // Test 12: Second body (notes) -> sections ignored
  {
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<description><title-info><book-title>Book</book-title></title-info></description>"
        "<body>"
        "<section><title><p>Chapter 1</p></title><p>Text</p></section>"
        "</body>"
        "<body name=\"notes\">"
        "<section><title><p>Note 1</p></title><p>Note text</p></section>"
        "</body>"
        "</FictionBook>";
    TestFb2Parser parser;
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "toc_second_body: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.tocItems.size(), "toc_second_body: only first body sections");
    runner.expectEqual("Chapter 1", parser.tocItems[0].title, "toc_second_body: correct title");
  }

  // Test 13: Nested elements inside <title> (e.g., <emphasis>) -> text still collected
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<book-title>Book</book-title>",
        "<section><title><p>Chapter <emphasis>One</emphasis></p></title><p>Text</p></section>"));
    runner.expectTrue(ok, "toc_emphasis_in_title: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.tocItems.size(), "toc_emphasis_in_title: 1 item");
    runner.expectEqual("Chapter One", parser.tocItems[0].title, "toc_emphasis_in_title: text collected through tags");
  }

  // Test 14: Section title with whitespace-only text -> no TocItem
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<book-title>Book</book-title>",
        "<section><title><p>   </p></title><p>Text</p></section>"
        "<section><title><p>Real Title</p></title><p>More</p></section>"));
    runner.expectTrue(ok, "toc_whitespace_title: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.tocItems.size(), "toc_whitespace_title: 1 item (whitespace skipped)");
    runner.expectEqual("Real Title", parser.tocItems[0].title, "toc_whitespace_title: correct title");
  }

  // Test 15: Multi-line title text gets newlines replaced with spaces
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<book-title>Book</book-title>",
        "<section><title><p>Part\nOne</p></title><p>Text</p></section>"));
    runner.expectTrue(ok, "toc_multiline_title: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.tocItems.size(), "toc_multiline_title: 1 item");
    runner.expectEqual("Part One", parser.tocItems[0].title, "toc_multiline_title: newline replaced");
  }

  // Test 16: No sections -> empty TOC
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<book-title>Book</book-title>", "<p>Just text, no sections</p>"));
    runner.expectTrue(ok, "toc_empty: parses successfully");
    runner.expectEq(static_cast<size_t>(0), parser.tocItems.size(), "toc_empty: no items");
  }

  // ============================================
  // Author filtering: only from <title-info>
  // ============================================

  // Test: <document-info><author> should NOT be included
  {
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<description>"
        "<title-info>"
        "<author><first-name>Robert</first-name><last-name>Heinlein</last-name></author>"
        "<book-title>Starship Troopers</book-title>"
        "</title-info>"
        "<document-info>"
        "<author><first-name>MCat78</first-name></author>"
        "</document-info>"
        "</description>"
        "<body><section><p>Text</p></section></body>"
        "</FictionBook>";
    TestFb2Parser parser;
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "doc_info_author_excluded: parses successfully");
    runner.expectEqual("Robert Heinlein", parser.author, "doc_info_author_excluded: only title-info author");
  }

  // Test: Multiple <title-info><author> entries still work
  {
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<description>"
        "<title-info>"
        "<author><first-name>Author</first-name><last-name>One</last-name></author>"
        "<author><first-name>Author</first-name><last-name>Two</last-name></author>"
        "<book-title>Collab</book-title>"
        "</title-info>"
        "<document-info>"
        "<author><first-name>Editor</first-name></author>"
        "</document-info>"
        "</description>"
        "<body><section><p>Text</p></section></body>"
        "</FictionBook>";
    TestFb2Parser parser;
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "multi_title_info_authors: parses successfully");
    runner.expectEqual("Author One, Author Two", parser.author, "multi_title_info_authors: both included, editor excluded");
  }

  // Test: <book-title> from <publish-info> should NOT override
  {
    std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\">"
        "<description>"
        "<title-info>"
        "<book-title>Real Title</book-title>"
        "</title-info>"
        "<publish-info>"
        "<book-name>Publisher Title</book-name>"
        "</publish-info>"
        "</description>"
        "<body><section><p>Text</p></section></body>"
        "</FictionBook>";
    TestFb2Parser parser;
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "publish_info_title_ignored: parses successfully");
    runner.expectEqual("Real Title", parser.title, "publish_info_title_ignored: only title-info title");
  }

  // ============================================
  // Pure logic tests
  // ============================================

  // Test 17: Title extraction from filepath
  {
    runner.expectEqual("novel", extractTitle("/books/novel.fb2"), "extractTitle: simple fb2 filename");
  }

  // Test 18: Title extraction with nested path
  {
    runner.expectEqual("War and Peace", extractTitle("/Library/Russian/War and Peace.fb2"),
                       "extractTitle: spaces in name");
  }

  // Test 19: Title extraction with no extension
  {
    runner.expectEqual("readme", extractTitle("/books/readme"), "extractTitle: no extension");
  }

  // Test 20: Title extraction with no directory
  {
    runner.expectEqual("book", extractTitle("book.fb2"), "extractTitle: no directory");
  }

  // Test 21: Cache path generation with fb2_ prefix
  {
    std::string path = generateCachePath("/.papyrix", "/books/novel.fb2");
    runner.expectTrue(path.find("/.papyrix/fb2_") == 0, "cachePath: has fb2_ prefix");
    runner.expectTrue(path.length() > 14, "cachePath: has hash suffix");
  }

  // Test 22: Same file produces same hash
  {
    std::string path1 = generateCachePath("/.cache", "/books/novel.fb2");
    std::string path2 = generateCachePath("/.cache", "/books/novel.fb2");
    runner.expectEqual(path1, path2, "cachePath: deterministic");
  }

  // Test 23: Different files produce different hashes
  {
    std::string path1 = generateCachePath("/.cache", "/books/novel1.fb2");
    std::string path2 = generateCachePath("/.cache", "/books/novel2.fb2");
    runner.expectTrue(path1 != path2, "cachePath: different files different hashes");
  }

  // Test 24: Three authors
  {
    TestFb2Parser parser;
    bool ok = parser.parse(
        makeFb2("<author><first-name>Alice</first-name><last-name>A</last-name></author>"
                "<author><first-name>Bob</first-name><last-name>B</last-name></author>"
                "<author><first-name>Charlie</first-name><last-name>C</last-name></author>"
                "<book-title>Three Authors</book-title>"));
    runner.expectTrue(ok, "three_authors: parses successfully");
    runner.expectEqual("Alice A, Bob B, Charlie C", parser.author, "three_authors: all comma-separated");
  }

  // Test 25: Empty author (both names empty) -> not added
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2("<author></author><book-title>Book</book-title>"));
    runner.expectTrue(ok, "empty_author_element: parses successfully");
    runner.expectEqual("", parser.author, "empty_author_element: empty author skipped");
  }

  // Test 26: UTF-8 TOC titles
  {
    TestFb2Parser parser;
    bool ok = parser.parse(makeFb2(
        "<book-title>Book</book-title>",
        "<section><title><p>\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1</p></title><p>Text</p></section>"));
    runner.expectTrue(ok, "toc_utf8: parses successfully");
    runner.expectEq(static_cast<size_t>(1), parser.tocItems.size(), "toc_utf8: 1 item");
    runner.expectEqual("\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0 1", parser.tocItems[0].title,
                       "toc_utf8: UTF-8 preserved");
  }

  return runner.allPassed() ? 0 : 1;
}
