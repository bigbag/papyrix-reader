// ContentOpfParser unit tests
// Tests OPF metadata parsing behavior: title/author extraction, multiple
// authors, and truncation limits.
//
// This test validates the parsing logic from ContentOpfParser by reimplementing
// the key metadata parsing rules in a test-friendly way, without needing to
// mock SD card, BookMetadataCache, or other infrastructure.

#include "test_utils.h"

#include <Utf8Nfc.h>
#include <expat.h>

#include <cstring>
#include <string>

constexpr size_t MAX_TITLE_LENGTH = 256;
constexpr size_t MAX_AUTHOR_LENGTH = 128;
constexpr size_t MAX_LANGUAGE_LENGTH = 32;

namespace {
size_t findUtf8Boundary(const char* s, size_t maxLen) {
  if (maxLen == 0) return 0;
  size_t pos = maxLen;
  while (pos > 0) {
    const unsigned char c = static_cast<unsigned char>(s[pos - 1]);
    if (c < 0x80 || c >= 0xC0) {
      if (c < 0x80) {
        return pos;
      }
      size_t charLen = 1;
      if ((c & 0xE0) == 0xC0)
        charLen = 2;
      else if ((c & 0xF0) == 0xE0)
        charLen = 3;
      else if ((c & 0xF8) == 0xF0)
        charLen = 4;

      if (pos - 1 + charLen <= maxLen) {
        return pos - 1 + charLen;
      }
      pos--;
      continue;
    }
    pos--;
  }
  return 0;
}
}  // namespace

// Lightweight OPF metadata parser that mirrors ContentOpfParser's
// title/author extraction without SD card or cache dependencies.
class TestOpfParser {
  enum ParserState {
    START,
    IN_PACKAGE,
    IN_METADATA,
    IN_BOOK_TITLE,
    IN_BOOK_AUTHOR,
    IN_BOOK_LANGUAGE,
    IN_MANIFEST,
    IN_OTHER,
  };

  ParserState state = START;

  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
    auto* self = static_cast<TestOpfParser*>(userData);

    if (self->state == START && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
      self->state = IN_PACKAGE;
      return;
    }
    if (self->state == IN_PACKAGE && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
      self->state = IN_METADATA;
      return;
    }
    if (self->state == IN_PACKAGE && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
      self->state = IN_MANIFEST;
      return;
    }
    if (self->state == IN_METADATA && strcmp(name, "dc:title") == 0) {
      self->state = IN_BOOK_TITLE;
      return;
    }
    if (self->state == IN_METADATA && strcmp(name, "dc:creator") == 0) {
      if (!self->author.empty()) {
        self->author.append(", ");
      }
      self->state = IN_BOOK_AUTHOR;
      return;
    }
    if (self->state == IN_METADATA && strcmp(name, "dc:language") == 0) {
      self->state = IN_BOOK_LANGUAGE;
      return;
    }
    // Mirror ContentOpfParser's EPUB 3 cover-image detection on <manifest>/<item>.
    // Production code: lib/Epub/src/Epub/parsers/ContentOpfParser.cpp (cover-image
    // block in startElement). First-match wins via the coverItemHref.empty() guard.
    if (self->state == IN_MANIFEST && (strcmp(name, "item") == 0 || strcmp(name, "opf:item") == 0)) {
      std::string href;
      std::string properties;
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "href") == 0) {
          href = atts[i + 1];
        } else if (strcmp(atts[i], "properties") == 0) {
          properties = atts[i + 1];
        }
      }
      if (!properties.empty() && self->coverItemHref.empty()) {
        if (properties == "cover-image" || properties.find("cover-image ") == 0 ||
            properties.find(" cover-image") != std::string::npos) {
          self->coverItemHref = href;
        }
      }
      return;
    }
    (void)atts;
  }

  static void XMLCALL characterData(void* userData, const XML_Char* s, int len) {
    auto* self = static_cast<TestOpfParser*>(userData);

    if (self->state == IN_BOOK_TITLE) {
      if (self->title.size() + static_cast<size_t>(len) <= MAX_TITLE_LENGTH) {
        self->title.append(s, len);
      } else if (self->title.size() < MAX_TITLE_LENGTH) {
        const size_t remaining = MAX_TITLE_LENGTH - self->title.size();
        const size_t safeLen = findUtf8Boundary(s, remaining);
        if (safeLen > 0) {
          self->title.append(s, safeLen);
        }
      }
      return;
    }

    if (self->state == IN_BOOK_AUTHOR) {
      if (self->author.size() + static_cast<size_t>(len) <= MAX_AUTHOR_LENGTH) {
        self->author.append(s, len);
      } else if (self->author.size() < MAX_AUTHOR_LENGTH) {
        const size_t remaining = MAX_AUTHOR_LENGTH - self->author.size();
        const size_t safeLen = findUtf8Boundary(s, remaining);
        if (safeLen > 0) {
          self->author.append(s, safeLen);
        }
      }
      return;
    }

    if (self->state == IN_BOOK_LANGUAGE) {
      if (self->language.size() + static_cast<size_t>(len) <= MAX_LANGUAGE_LENGTH) {
        self->language.append(s, len);
      }
      return;
    }
  }

  static void XMLCALL endElement(void* userData, const XML_Char* name) {
    auto* self = static_cast<TestOpfParser*>(userData);

    if (self->state == IN_BOOK_TITLE && strcmp(name, "dc:title") == 0) {
      self->title.resize(utf8NormalizeNfc(&self->title[0], self->title.size()));
      self->state = IN_METADATA;
      return;
    }
    if (self->state == IN_BOOK_AUTHOR && strcmp(name, "dc:creator") == 0) {
      self->author.resize(utf8NormalizeNfc(&self->author[0], self->author.size()));
      self->state = IN_METADATA;
      return;
    }
    if (self->state == IN_BOOK_LANGUAGE && strcmp(name, "dc:language") == 0) {
      auto& lang = self->language;
      const size_t start = lang.find_first_not_of(" \t\r\n");
      if (start == std::string::npos) {
        lang.clear();
      } else {
        const size_t end = lang.find_last_not_of(" \t\r\n");
        lang = lang.substr(start, end - start + 1);
      }
      self->state = IN_METADATA;
      return;
    }
    if (self->state == IN_METADATA && (strcmp(name, "metadata") == 0 || strcmp(name, "opf:metadata") == 0)) {
      self->state = IN_PACKAGE;
      return;
    }
    if (self->state == IN_MANIFEST && (strcmp(name, "manifest") == 0 || strcmp(name, "opf:manifest") == 0)) {
      self->state = IN_PACKAGE;
      return;
    }
    if (self->state == IN_PACKAGE && (strcmp(name, "package") == 0 || strcmp(name, "opf:package") == 0)) {
      self->state = START;
      return;
    }
  }

 public:
  std::string title;
  std::string author;
  std::string language;
  std::string coverItemHref;

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

// Helper to build a minimal OPF XML with given dc:creator elements
static std::string makeOpf(const std::string& metadataContent) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<package xmlns=\"http://www.idpf.org/2007/opf\">"
         "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">" +
         metadataContent +
         "</metadata>"
         "</package>";
}

// Helper to build an OPF XML containing both metadata and manifest. Used by
// the EPUB 3 cover-image tests below.
static std::string makeOpfWithManifest(const std::string& manifestContent) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
         "<package xmlns=\"http://www.idpf.org/2007/opf\">"
         "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
         "<dc:title>Test</dc:title>"
         "</metadata>"
         "<manifest>" +
         manifestContent +
         "</manifest>"
         "</package>";
}

int main() {
  TestUtils::TestRunner runner("ContentOpfParser");

  // Test 1: Single author
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>Jane Austen</dc:creator>"));
    runner.expectTrue(ok, "single_author: parses successfully");
    runner.expectEqual("Jane Austen", parser.author, "single_author: correct author");
  }

  // Test 2: Two authors separated by comma-space
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>Author One</dc:creator>"
                                   "<dc:creator>Author Two</dc:creator>"));
    runner.expectTrue(ok, "two_authors: parses successfully");
    runner.expectEqual("Author One, Author Two", parser.author, "two_authors: comma-separated");
  }

  // Test 3: Three authors
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>Alice</dc:creator>"
                                   "<dc:creator>Bob</dc:creator>"
                                   "<dc:creator>Charlie</dc:creator>"));
    runner.expectTrue(ok, "three_authors: parses successfully");
    runner.expectEqual("Alice, Bob, Charlie", parser.author, "three_authors: all separated");
  }

  // Test 4: Single title
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:title>Pride and Prejudice</dc:title>"));
    runner.expectTrue(ok, "single_title: parses successfully");
    runner.expectEqual("Pride and Prejudice", parser.title, "single_title: correct title");
  }

  // Test 5: Title and author together
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:title>Sense and Sensibility</dc:title>"
                                   "<dc:creator>Jane Austen</dc:creator>"));
    runner.expectTrue(ok, "title_and_author: parses successfully");
    runner.expectEqual("Sense and Sensibility", parser.title, "title_and_author: correct title");
    runner.expectEqual("Jane Austen", parser.author, "title_and_author: correct author");
  }

  // Test 6: Empty author element
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator></dc:creator>"));
    runner.expectTrue(ok, "empty_author: parses successfully");
    runner.expectEqual("", parser.author, "empty_author: empty string");
  }

  // Test 7: No dc:creator element at all
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:title>Untitled</dc:title>"));
    runner.expectTrue(ok, "no_author: parses successfully");
    runner.expectEqual("", parser.author, "no_author: empty string");
  }

  // Test 8: Author with UTF-8 characters
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>José García</dc:creator>"));
    runner.expectTrue(ok, "utf8_author: parses successfully");
    runner.expectEqual("José García", parser.author, "utf8_author: UTF-8 preserved");
  }

  // Test 9: Two authors with UTF-8 characters
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>José García</dc:creator>"
                                   "<dc:creator>François Müller</dc:creator>"));
    runner.expectTrue(ok, "utf8_two_authors: parses successfully");
    runner.expectEqual("José García, François Müller", parser.author,
                       "utf8_two_authors: UTF-8 with separator");
  }

  // Test 10: Author truncation at MAX_AUTHOR_LENGTH
  {
    // Create a string that's exactly MAX_AUTHOR_LENGTH
    std::string longName(MAX_AUTHOR_LENGTH, 'A');
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>" + longName + "</dc:creator>"));
    runner.expectTrue(ok, "author_at_limit: parses successfully");
    runner.expectEq(MAX_AUTHOR_LENGTH, parser.author.size(), "author_at_limit: exactly at limit");
  }

  // Test 11: Author exceeding MAX_AUTHOR_LENGTH is truncated
  {
    std::string longName(MAX_AUTHOR_LENGTH + 50, 'B');
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>" + longName + "</dc:creator>"));
    runner.expectTrue(ok, "author_over_limit: parses successfully");
    runner.expectTrue(parser.author.size() <= MAX_AUTHOR_LENGTH, "author_over_limit: truncated to limit");
  }

  // Test 12: Two authors where second would exceed limit
  {
    // First author takes up most of the budget
    std::string firstAuthor(MAX_AUTHOR_LENGTH - 10, 'C');
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>" + firstAuthor + "</dc:creator>"
                                   "<dc:creator>Second Author Name</dc:creator>"));
    runner.expectTrue(ok, "author_second_truncated: parses successfully");
    runner.expectTrue(parser.author.size() <= MAX_AUTHOR_LENGTH, "author_second_truncated: within limit");
    // Should start with first author and separator
    runner.expectTrue(parser.author.find(firstAuthor + ", ") == 0,
                      "author_second_truncated: first author + separator present");
  }

  // Test 13: Separator itself counts toward limit
  {
    // First author fills to exactly MAX_AUTHOR_LENGTH - 1
    // Separator ", " is 2 bytes, so it would push to MAX_AUTHOR_LENGTH + 1
    std::string firstAuthor(MAX_AUTHOR_LENGTH - 1, 'D');
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator>" + firstAuthor + "</dc:creator>"
                                   "<dc:creator>E</dc:creator>"));
    runner.expectTrue(ok, "separator_at_limit: parses successfully");
    // The separator ", " is appended in startElement before characterData,
    // so author = firstAuthor + ", " (already at MAX_AUTHOR_LENGTH+1 after separator)
    // Then characterData tries to add "E" but author.size() >= MAX_AUTHOR_LENGTH
    runner.expectTrue(parser.author.size() <= MAX_AUTHOR_LENGTH + 2,
                      "separator_at_limit: reasonable size");
  }

  // Test 14: opf: prefixed elements work
  {
    std::string xml =
        "<?xml version=\"1.0\"?>"
        "<opf:package xmlns:opf=\"http://www.idpf.org/2007/opf\">"
        "<opf:metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
        "<dc:creator>Author A</dc:creator>"
        "<dc:creator>Author B</dc:creator>"
        "</opf:metadata>"
        "</opf:package>";
    TestOpfParser parser;
    bool ok = parser.parse(xml);
    runner.expectTrue(ok, "opf_prefix: parses successfully");
    runner.expectEqual("Author A, Author B", parser.author, "opf_prefix: authors separated");
  }

  // ============================================
  // Language parsing tests
  // ============================================

  // Test: Simple language tag
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:language>en</dc:language>"));
    runner.expectTrue(ok, "language_simple: parses successfully");
    runner.expectEqual("en", parser.language, "language_simple: correct language");
  }

  // Test: Language with region subtag
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:language>en-US</dc:language>"));
    runner.expectTrue(ok, "language_region: parses successfully");
    runner.expectEqual("en-US", parser.language, "language_region: correct language");
  }

  // Test: No dc:language element
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:title>Test</dc:title>"));
    runner.expectTrue(ok, "language_absent: parses successfully");
    runner.expectEqual("", parser.language, "language_absent: empty string");
  }

  // Test: Empty dc:language element
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:language></dc:language>"));
    runner.expectTrue(ok, "language_empty: parses successfully");
    runner.expectEqual("", parser.language, "language_empty: empty string");
  }

  // Test: Language with title and author
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:title>Book</dc:title>"
                                   "<dc:creator>Author</dc:creator>"
                                   "<dc:language>fr</dc:language>"));
    runner.expectTrue(ok, "language_with_metadata: parses successfully");
    runner.expectEqual("Book", parser.title, "language_with_metadata: title correct");
    runner.expectEqual("Author", parser.author, "language_with_metadata: author correct");
    runner.expectEqual("fr", parser.language, "language_with_metadata: language correct");
  }

  // Test: Language truncation at MAX_LANGUAGE_LENGTH
  {
    std::string longLang(MAX_LANGUAGE_LENGTH + 10, 'x');
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:language>" + longLang + "</dc:language>"));
    runner.expectTrue(ok, "language_truncated: parses successfully");
    runner.expectTrue(parser.language.size() <= MAX_LANGUAGE_LENGTH, "language_truncated: within limit");
  }

  // Test: Language with surrounding whitespace (pretty-printed OPF)
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:language>\n    en\n  </dc:language>"));
    runner.expectTrue(ok, "language_whitespace: parses successfully");
    runner.expectEqual("en", parser.language, "language_whitespace: whitespace trimmed");
  }

  // Test: Language that is only whitespace
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:language>  \n  </dc:language>"));
    runner.expectTrue(ok, "language_only_whitespace: parses successfully");
    runner.expectEqual("", parser.language, "language_only_whitespace: empty after trim");
  }

  // Test 15: Author with leading/trailing whitespace (expat preserves it)
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator> Spaced Author </dc:creator>"));
    runner.expectTrue(ok, "author_whitespace: parses successfully");
    runner.expectEqual(" Spaced Author ", parser.author, "author_whitespace: whitespace preserved");
  }

  // Test 16: Empty first author, non-empty second
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpf("<dc:creator></dc:creator>"
                                   "<dc:creator>Real Author</dc:creator>"));
    runner.expectTrue(ok, "empty_first_author: parses successfully");
    // Empty first dc:creator produces empty string; second one sees non-empty
    // (empty string is still empty so separator is not added)
    runner.expectEqual("Real Author", parser.author, "empty_first_author: only real author");
  }

  // ============================================
  // EPUB 3 cover-image property on manifest items
  // ============================================
  // Verifies the rule applied in
  // lib/Epub/src/Epub/parsers/ContentOpfParser.cpp: a manifest <item> whose
  // properties attribute contains the token "cover-image" populates
  // coverItemHref. The test mirror in TestOpfParser uses the identical match
  // logic; first match wins.

  // Test: lone cover-image property
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"cover\" href=\"images/cover.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\"/>"));
    runner.expectTrue(ok, "cover_lone: parses successfully");
    runner.expectEqual("images/cover.jpg", parser.coverItemHref, "cover_lone: href captured");
  }

  // Test: cover-image as prefix in space-separated list
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"cover\" href=\"cover.png\" media-type=\"image/png\" properties=\"cover-image nav\"/>"));
    runner.expectTrue(ok, "cover_prefix: parses successfully");
    runner.expectEqual("cover.png", parser.coverItemHref, "cover_prefix: href captured");
  }

  // Test: cover-image as suffix in space-separated list
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"cover\" href=\"img/c.jpg\" media-type=\"image/jpeg\" properties=\"nav cover-image\"/>"));
    runner.expectTrue(ok, "cover_suffix: parses successfully");
    runner.expectEqual("img/c.jpg", parser.coverItemHref, "cover_suffix: href captured");
  }

  // Test: cover-image in the middle of a space-separated list
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"cover\" href=\"middle.jpg\" media-type=\"image/jpeg\" "
        "properties=\"nav cover-image scripted\"/>"));
    runner.expectTrue(ok, "cover_middle: parses successfully");
    runner.expectEqual("middle.jpg", parser.coverItemHref, "cover_middle: href captured");
  }

  // Test: no cover-image property at all
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"nav\" href=\"toc.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"));
    runner.expectTrue(ok, "cover_absent: parses successfully");
    runner.expectEqual("", parser.coverItemHref, "cover_absent: coverItemHref empty");
  }

  // Test: item without any properties attribute
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"chap1\" href=\"chapter1.xhtml\" media-type=\"application/xhtml+xml\"/>"));
    runner.expectTrue(ok, "cover_no_properties_attr: parses successfully");
    runner.expectEqual("", parser.coverItemHref, "cover_no_properties_attr: coverItemHref empty");
  }

  // Test: first item with cover-image wins over later ones
  {
    TestOpfParser parser;
    bool ok = parser.parse(makeOpfWithManifest(
        "<item id=\"first\" href=\"first.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\"/>"
        "<item id=\"second\" href=\"second.jpg\" media-type=\"image/jpeg\" properties=\"cover-image\"/>"));
    runner.expectTrue(ok, "cover_first_wins: parses successfully");
    runner.expectEqual("first.jpg", parser.coverItemHref, "cover_first_wins: first href captured");
  }

  return runner.allPassed() ? 0 : 1;
}
