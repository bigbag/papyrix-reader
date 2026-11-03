#include "test_utils.h"

#include <cctype>
#include <cstring>
#include <functional>
#include <string>

// ============================================
// Pure functions extracted from Html.cpp for testing
// ============================================

// Extract title from filepath (logic from Html constructor)
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

// Generate cache path (logic from Html constructor)
static std::string generateCachePath(const std::string& cacheDir, const std::string& filepath) {
  return cacheDir + "/html_" + std::to_string(std::hash<std::string>{}(filepath));
}

// Extract <title> tag content from HTML buffer (logic from Html::load())
static std::string extractTitleFromHtml(const char* buf, size_t len) {
  auto* lower = new char[len + 1];
  for (size_t i = 0; i < len; i++) {
    lower[i] = static_cast<char>(tolower(static_cast<unsigned char>(buf[i])));
  }
  lower[len] = '\0';

  std::string result;

  const char* titleStart = strstr(lower, "<title>");
  if (titleStart) {
    size_t startOffset = static_cast<size_t>(titleStart - lower) + 7;
    const char* titleEnd = strstr(lower + startOffset, "</title>");
    if (titleEnd) {
      size_t endOffset = static_cast<size_t>(titleEnd - lower);
      if (endOffset > startOffset) {
        size_t titleLen = endOffset - startOffset;
        if (titleLen > 255) titleLen = 255;
        std::string extracted(buf + startOffset, titleLen);
        size_t first = extracted.find_first_not_of(" \t\r\n");
        size_t last = extracted.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
          result = extracted.substr(first, last - first + 1);
        }
      }
    }
  }

  delete[] lower;
  return result;
}

// Extract directory from filepath (logic from Html::findCoverImage())
static std::string extractDirectory(const std::string& filepath) {
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";
  return dirPath;
}

// FsHelpers::isHtmlFile reimplemented for testing
static bool isHtmlFile(const char* path) {
  if (!path) return false;
  const char* ext = strrchr(path, '.');
  if (!ext) return false;
  return strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0;
}

int main() {
  TestUtils::TestRunner runner("Html Functions");

  // ============================================
  // extractTitle() tests
  // ============================================

  {
    std::string title = extractTitle("/books/page.html");
    runner.expectEqual("page", title, "extractTitle: simple .html filename");
  }

  {
    std::string title = extractTitle("/books/page.htm");
    runner.expectEqual("page", title, "extractTitle: .htm extension");
  }

  {
    std::string title = extractTitle("/books/readme");
    runner.expectEqual("readme", title, "extractTitle: no extension");
  }

  {
    std::string title = extractTitle("/books/my.page.name.html");
    runner.expectEqual("my.page.name", title, "extractTitle: multiple dots");
  }

  {
    std::string title = extractTitle("/index.html");
    runner.expectEqual("index", title, "extractTitle: root directory");
  }

  {
    std::string title = extractTitle("file.html");
    runner.expectEqual("file", title, "extractTitle: no directory");
  }

  {
    std::string title = extractTitle("/a/b/c/d/e/page.html");
    runner.expectEqual("page", title, "extractTitle: deep nested");
  }

  {
    std::string title = extractTitle("/books/.hidden.html");
    runner.expectEqual(".hidden", title, "extractTitle: hidden file");
  }

  {
    std::string title = extractTitle("/books/.html");
    runner.expectEqual(".html", title, "extractTitle: just extension");
  }

  {
    std::string title = extractTitle("");
    runner.expectTrue(title.empty(), "extractTitle: empty string");
  }

  {
    std::string title = extractTitle("/");
    runner.expectTrue(title.empty(), "extractTitle: just slash");
  }

  {
    std::string title = extractTitle("/books/");
    runner.expectTrue(title.empty(), "extractTitle: trailing slash");
  }

  {
    std::string title = extractTitle("/books/\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.html");
    runner.expectEqual("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", title, "extractTitle: unicode filename");
  }

  // ============================================
  // generateCachePath() tests
  // ============================================

  {
    std::string path = generateCachePath("/.papyrix", "/books/page.html");
    runner.expectTrue(path.find("/.papyrix/html_") == 0, "generateCachePath: has html_ prefix");
    runner.expectTrue(path.length() > 15, "generateCachePath: has hash");
  }

  {
    std::string path1 = generateCachePath("/.cache", "/books/page.html");
    std::string path2 = generateCachePath("/.cache", "/books/page.html");
    runner.expectEqual(path1, path2, "generateCachePath: deterministic");
  }

  {
    std::string path1 = generateCachePath("/.cache", "/books/page1.html");
    std::string path2 = generateCachePath("/.cache", "/books/page2.html");
    runner.expectTrue(path1 != path2, "generateCachePath: different files different hashes");
  }

  {
    std::string path1 = generateCachePath("/cache1", "/books/page.html");
    std::string path2 = generateCachePath("/cache2", "/books/page.html");
    runner.expectTrue(path1 != path2, "generateCachePath: different dirs different paths");
  }

  {
    std::string pathHtml = generateCachePath("/.cache", "/books/page.html");
    std::string pathHtm = generateCachePath("/.cache", "/books/page.htm");
    runner.expectTrue(pathHtml != pathHtm, "generateCachePath: .html vs .htm different hashes");
  }

  // ============================================
  // extractTitleFromHtml() tests
  // ============================================

  {
    std::string html = "<html><head><title>My Page</title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("My Page", title, "extractTitleFromHtml: basic title");
  }

  {
    std::string html = "<html><head><TITLE>Upper Case</TITLE></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("Upper Case", title, "extractTitleFromHtml: uppercase tags");
  }

  {
    std::string html = "<html><head><Title>Mixed Case</Title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("Mixed Case", title, "extractTitleFromHtml: mixed case tags");
  }

  {
    std::string html = "<html><head><TiTlE>Weird Case</tItLe></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("Weird Case", title, "extractTitleFromHtml: weird case tags");
  }

  {
    std::string html = "<html><head><title>  \t\n Spaced Title \r\n  </title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("Spaced Title", title, "extractTitleFromHtml: whitespace trimmed");
  }

  {
    std::string html = "<html><head><title>Line1\nLine2</title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("Line1\nLine2", title, "extractTitleFromHtml: internal newlines preserved");
  }

  {
    std::string html = "<html><head><title>  \t\r\n  </title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectTrue(title.empty(), "extractTitleFromHtml: whitespace-only title is empty");
  }

  {
    // Build title longer than 255 chars
    std::string longTitle(300, 'A');
    std::string html = "<html><head><title>" + longTitle + "</title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEq(static_cast<size_t>(255), title.size(), "extractTitleFromHtml: truncated to 255");
  }

  {
    std::string html = "<html><head></head><body>No title here</body></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectTrue(title.empty(), "extractTitleFromHtml: missing title tag");
  }

  {
    std::string html = "<html><head><title></title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectTrue(title.empty(), "extractTitleFromHtml: empty title tag");
  }

  {
    std::string html =
        "<html><head><title>\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82</title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", title,
                       "extractTitleFromHtml: UTF-8 content");
  }

  {
    // <title lang="en"> won't match because the parser looks for literal "<title>"
    std::string html = "<html><head><title lang=\"en\">Attributed</title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectTrue(title.empty(), "extractTitleFromHtml: title with attributes not matched");
  }

  {
    std::string html = "<html><head><title>First</title><title>Second</title></head></html>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("First", title, "extractTitleFromHtml: first title wins");
  }

  {
    // Title tag right at end of buffer without closing tag
    std::string html = "<html><head><title>No Close";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectTrue(title.empty(), "extractTitleFromHtml: unclosed title tag");
  }

  {
    std::string html = "<title>Bare Title</title>";
    std::string title = extractTitleFromHtml(html.c_str(), html.size());
    runner.expectEqual("Bare Title", title, "extractTitleFromHtml: title without html/head wrapper");
  }

  // ============================================
  // isHtmlFile() tests
  // ============================================

  {
    runner.expectTrue(isHtmlFile("page.html"), "isHtmlFile: .html");
  }

  {
    runner.expectTrue(isHtmlFile("page.htm"), "isHtmlFile: .htm");
  }

  {
    runner.expectTrue(isHtmlFile("PAGE.HTML"), "isHtmlFile: uppercase .HTML");
  }

  {
    runner.expectTrue(isHtmlFile("/path/to/file.HtM"), "isHtmlFile: mixed case .HtM");
  }

  {
    runner.expectFalse(isHtmlFile("page.txt"), "isHtmlFile: rejects .txt");
  }

  {
    runner.expectFalse(isHtmlFile("page.epub"), "isHtmlFile: rejects .epub");
  }

  {
    runner.expectFalse(isHtmlFile("htmlfile"), "isHtmlFile: no extension");
  }

  {
    runner.expectFalse(isHtmlFile(nullptr), "isHtmlFile: null pointer");
  }

  // ============================================
  // extractDirectory() tests
  // ============================================

  {
    std::string dir = extractDirectory("/books/page.html");
    runner.expectEqual("/books", dir, "extractDirectory: simple path");
  }

  {
    std::string dir = extractDirectory("/file.html");
    runner.expectTrue(dir.empty() || dir == "/", "extractDirectory: root file");
  }

  {
    std::string dir = extractDirectory("file.html");
    runner.expectEqual("/", dir, "extractDirectory: no slash");
  }

  {
    std::string dir = extractDirectory("/a/b/c/d/file.html");
    runner.expectEqual("/a/b/c/d", dir, "extractDirectory: deep path");
  }

  return runner.allPassed() ? 0 : 1;
}
