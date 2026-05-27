#include "test_utils.h"

#include <cstring>

#include "ContentTypes.h"

namespace papyrix {
const char* errorToString(Error) { return ""; }
}  // namespace papyrix

int main() {
  TestUtils::TestRunner runner("ContentTypeDetection");

  // === Basic extension detection ===

  runner.expectEq(uint8_t(papyrix::ContentType::Epub), uint8_t(papyrix::detectContentType("/books/test.epub")),
                  "epub detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Fb2), uint8_t(papyrix::detectContentType("/books/test.fb2")),
                  "fb2 detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Txt), uint8_t(papyrix::detectContentType("/books/test.txt")),
                  "txt detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Markdown), uint8_t(papyrix::detectContentType("/books/test.md")),
                  "md detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Html), uint8_t(papyrix::detectContentType("/books/test.html")),
                  "html detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Xtc), uint8_t(papyrix::detectContentType("/books/test.xtc")),
                  "xtc detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Xtc), uint8_t(papyrix::detectContentType("/books/test.xtch")),
                  "xtch detected");

  // === Case insensitive ===

  runner.expectEq(uint8_t(papyrix::ContentType::Epub), uint8_t(papyrix::detectContentType("/books/TEST.EPUB")),
                  "EPUB uppercase");
  runner.expectEq(uint8_t(papyrix::ContentType::Fb2), uint8_t(papyrix::detectContentType("/books/book.Fb2")),
                  "Fb2 mixed case");

  // === Invalid / missing extension ===

  runner.expectEq(uint8_t(papyrix::ContentType::None), uint8_t(papyrix::detectContentType("/books/noext")),
                  "no extension");
  runner.expectEq(uint8_t(papyrix::ContentType::None), uint8_t(papyrix::detectContentType("/books/file.xyz")),
                  "unknown extension");
  runner.expectEq(uint8_t(papyrix::ContentType::None), uint8_t(papyrix::detectContentType(nullptr)), "null path");

  // === Long UTF-8 Cyrillic path (the bug scenario) ===

  {
    // ~202 bytes: the exact path from issue #106
    const char* longPath =
        "/\xd0\x9a\xd0\xbd\xd0\xb8\xd0\xb3\xd0\xb8"  // Книги
        "/[\xd0\x9d\xd0\xbe\xd0\xb2\xd0\xbe\xd0\xb5]"  // [Новое]
        "/\xd0\x92\xd1\x82\xd0\xbe\xd1\x80\xd0\xb0\xd1\x8f"
        " \xd0\xb6\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"
        " \xd0\xb8\xd0\xb7\xd0\xb2\xd0\xb5\xd1\x81\xd1\x82\xd0\xbd\xd1\x8b\xd1\x85"
        " \xd0\xba\xd0\xbd\xd0\xb8\xd0\xb3, \xd0\xb8\xd0\xbb\xd0\xb8"
        " \xd0\xb8\xd1\x81\xd1\x82\xd0\xbe\xd1\x80\xd0\xb8\xd0\xb8"
        " \xd0\xbf\xd1\x80\xd0\xbe"  // Вторая жизнь известных книг, или истории про
        "/\xd0\x9f\xd1\x80\xd0\xbe"
        " \xd0\xa0\xd0\xbe\xd0\xb1\xd0\xb8\xd0\xbd\xd0\xb0"
        " \xd0\x93\xd1\x83\xd0\xb4\xd0\xb0"  // Про Робина Гуда
        "/\xd0\x9d\xd0\xb0"
        " \xd0\xb0\xd0\xbd\xd0\xb3\xd0\xbb\xd0\xb8\xd0\xb9\xd1\x81\xd0\xba\xd0\xbe\xd0\xbc"
        " \xd1\x8f\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb5"  // На английском языке
        "/Nicky Raven - Robin Hood.epub";

    size_t pathLen = strlen(longPath);
    runner.expectTrue(pathLen > 200, "long Cyrillic path exceeds old 200-byte limit");
    runner.expectTrue(pathLen < 512, "long Cyrillic path fits in new 512-byte buffer");
    runner.expectEq(uint8_t(papyrix::ContentType::Epub), uint8_t(papyrix::detectContentType(longPath)),
                    "epub detected with long Cyrillic path");
  }

  // === Truncated path simulates the old bug ===

  {
    // Build a path that when truncated at 200 bytes loses the extension
    char truncatedPath[201];
    const char* longPath =
        "/\xd0\x9a\xd0\xbd\xd0\xb8\xd0\xb3\xd0\xb8"
        "/[\xd0\x9d\xd0\xbe\xd0\xb2\xd0\xbe\xd0\xb5]"
        "/\xd0\x92\xd1\x82\xd0\xbe\xd1\x80\xd0\xb0\xd1\x8f"
        " \xd0\xb6\xd0\xb8\xd0\xb7\xd0\xbd\xd1\x8c"
        " \xd0\xb8\xd0\xb7\xd0\xb2\xd0\xb5\xd1\x81\xd1\x82\xd0\xbd\xd1\x8b\xd1\x85"
        " \xd0\xba\xd0\xbd\xd0\xb8\xd0\xb3, \xd0\xb8\xd0\xbb\xd0\xb8"
        " \xd0\xb8\xd1\x81\xd1\x82\xd0\xbe\xd1\x80\xd0\xb8\xd0\xb8"
        " \xd0\xbf\xd1\x80\xd0\xbe"
        "/\xd0\x9f\xd1\x80\xd0\xbe"
        " \xd0\xa0\xd0\xbe\xd0\xb1\xd0\xb8\xd0\xbd\xd0\xb0"
        " \xd0\x93\xd1\x83\xd0\xb4\xd0\xb0"
        "/\xd0\x9d\xd0\xb0"
        " \xd0\xb0\xd0\xbd\xd0\xb3\xd0\xbb\xd0\xb8\xd0\xb9\xd1\x81\xd0\xba\xd0\xbe\xd0\xbc"
        " \xd1\x8f\xd0\xb7\xd1\x8b\xd0\xba\xd0\xb5"
        "/Nicky Raven - Robin Hood.epub";

    // Simulate old bookPath[200] truncation via strncpy
    strncpy(truncatedPath, longPath, sizeof(truncatedPath) - 1);
    truncatedPath[sizeof(truncatedPath) - 1] = '\0';

    runner.expectEq(uint8_t(papyrix::ContentType::None), uint8_t(papyrix::detectContentType(truncatedPath)),
                    "truncated path loses extension -> None (the bug)");
  }

  // === Path with dots in directory names ===

  runner.expectEq(uint8_t(papyrix::ContentType::Epub),
                  uint8_t(papyrix::detectContentType("/books/v2.0/chapter.1/book.epub")),
                  "dots in directory names");
  runner.expectEq(uint8_t(papyrix::ContentType::None),
                  uint8_t(papyrix::detectContentType("/books/v2.0/chapter.1/book")), "no ext after dotted dirs");

  // === Leading whitespace in filenames (Issue #135) ===

  runner.expectEq(uint8_t(papyrix::ContentType::Txt), uint8_t(papyrix::detectContentType("/ book.txt")),
                  "leading space: txt detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Epub), uint8_t(papyrix::detectContentType("/Books/ novel.epub")),
                  "leading space: epub detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Fb2), uint8_t(papyrix::detectContentType("/  story.fb2")),
                  "multiple leading spaces: fb2 detected");
  runner.expectEq(uint8_t(papyrix::ContentType::Markdown), uint8_t(papyrix::detectContentType("/ notes.md")),
                  "leading space: md detected");

  runner.printSummary();
  return runner.allPassed() ? 0 : 1;
}
