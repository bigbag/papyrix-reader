#include "test_utils.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// ============================================
// Pure functions extracted from Txt.cpp for testing
// (Title extraction and path manipulation)
// ============================================

// Extract title from filepath (logic from Txt constructor)
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

// Generate cache path (logic from Txt constructor)
static std::string generateCachePath(const std::string& cacheDir, const std::string& filepath) {
  return cacheDir + "/txt_" + std::to_string(std::hash<std::string>{}(filepath));
}

// Extract directory from path (logic from findCoverImage)
static std::string extractDirectory(const std::string& filepath) {
  size_t lastSlash = filepath.find_last_of('/');
  std::string dirPath = (lastSlash == std::string::npos) ? "/" : filepath.substr(0, lastSlash);
  if (dirPath.empty()) dirPath = "/";
  return dirPath;
}

// Check if file has specific extension (common pattern)
static bool hasExtension(const std::string& filename, const std::string& ext) {
  if (filename.length() < ext.length() + 1) return false;
  size_t pos = filename.rfind('.');
  if (pos == std::string::npos) return false;
  std::string fileExt = filename.substr(pos + 1);
  // Case-insensitive comparison
  if (fileExt.length() != ext.length()) return false;
  for (size_t i = 0; i < ext.length(); i++) {
    char c1 = fileExt[i];
    char c2 = ext[i];
    if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    if (c1 != c2) return false;
  }
  return true;
}

int main() {
  TestUtils::TestRunner runner("Txt Functions");

  // ============================================
  // extractTitle() tests
  // ============================================

  // Test 1: Simple filename with extension
  {
    std::string title = extractTitle("/books/novel.txt");
    runner.expectEqual("novel", title, "extractTitle: simple filename");
  }

  // Test 2: Filename without extension
  {
    std::string title = extractTitle("/books/readme");
    runner.expectEqual("readme", title, "extractTitle: no extension");
  }

  // Test 3: Filename with multiple dots
  {
    std::string title = extractTitle("/books/my.book.name.txt");
    runner.expectEqual("my.book.name", title, "extractTitle: multiple dots");
  }

  // Test 4: Root directory file
  {
    std::string title = extractTitle("/file.txt");
    runner.expectEqual("file", title, "extractTitle: root directory");
  }

  // Test 5: No slash in path
  {
    std::string title = extractTitle("file.txt");
    runner.expectEqual("file", title, "extractTitle: no directory");
  }

  // Test 6: Deep nested path
  {
    std::string title = extractTitle("/a/b/c/d/e/book.txt");
    runner.expectEqual("book", title, "extractTitle: deep nested");
  }

  // Test 7: Hidden file (starts with dot)
  {
    std::string title = extractTitle("/books/.hidden.txt");
    runner.expectEqual(".hidden", title, "extractTitle: hidden file");
  }

  // Test 8: Just extension - ".txt" filename means extension after dot at position 0
  // lastSlash=6, lastDot=7, lastDot > lastSlash so we get substr(7, 0) = ""
  // Actually: lastSlash points to position after /, and .txt has no chars before dot
  // The logic: lastSlash=7 (after /), lastDot=7 (the dot), lastDot <= lastSlash is false (7 <= 7 is true)
  // So it returns substr(7) = ".txt"
  {
    std::string title = extractTitle("/books/.txt");
    // The dot IS at lastSlash position, so lastDot <= lastSlash is true, returns substr(lastSlash)
    runner.expectEqual(".txt", title, "extractTitle: just extension");
  }

  // Test 9: Empty string
  {
    std::string title = extractTitle("");
    runner.expectTrue(title.empty(), "extractTitle: empty string");
  }

  // Test 10: Just slash
  {
    std::string title = extractTitle("/");
    runner.expectTrue(title.empty(), "extractTitle: just slash");
  }

  // Test 11: Trailing slash
  {
    std::string title = extractTitle("/books/");
    runner.expectTrue(title.empty(), "extractTitle: trailing slash");
  }

  // Test 12: Filename with spaces
  {
    std::string title = extractTitle("/books/My Book Title.txt");
    runner.expectEqual("My Book Title", title, "extractTitle: spaces in name");
  }

  // Test 13: Unicode in filename
  {
    std::string title = extractTitle("/books/日本語.txt");
    runner.expectEqual("日本語", title, "extractTitle: unicode filename");
  }

  // ============================================
  // generateCachePath() tests
  // ============================================

  // Test 14: Basic cache path
  {
    std::string path = generateCachePath("/.papyrix", "/books/novel.txt");
    runner.expectTrue(path.find("/.papyrix/txt_") == 0, "generateCachePath: has prefix");
    runner.expectTrue(path.length() > 14, "generateCachePath: has hash");
  }

  // Test 15: Same file produces same hash
  {
    std::string path1 = generateCachePath("/.cache", "/books/novel.txt");
    std::string path2 = generateCachePath("/.cache", "/books/novel.txt");
    runner.expectEqual(path1, path2, "generateCachePath: deterministic");
  }

  // Test 16: Different files produce different hashes
  {
    std::string path1 = generateCachePath("/.cache", "/books/novel1.txt");
    std::string path2 = generateCachePath("/.cache", "/books/novel2.txt");
    runner.expectTrue(path1 != path2, "generateCachePath: different files different hashes");
  }

  // Test 17: Different cache dirs
  {
    std::string path1 = generateCachePath("/cache1", "/books/novel.txt");
    std::string path2 = generateCachePath("/cache2", "/books/novel.txt");
    runner.expectTrue(path1 != path2, "generateCachePath: different dirs different paths");
    runner.expectTrue(path1.find("/cache1/") == 0, "generateCachePath: uses cache dir 1");
    runner.expectTrue(path2.find("/cache2/") == 0, "generateCachePath: uses cache dir 2");
  }

  // ============================================
  // extractDirectory() tests
  // ============================================

  // Test 18: Simple path
  {
    std::string dir = extractDirectory("/books/novel.txt");
    runner.expectEqual("/books", dir, "extractDirectory: simple path");
  }

  // Test 19: Root file
  {
    std::string dir = extractDirectory("/file.txt");
    runner.expectTrue(dir.empty() || dir == "/", "extractDirectory: root file");
  }

  // Test 20: No slash
  {
    std::string dir = extractDirectory("file.txt");
    runner.expectEqual("/", dir, "extractDirectory: no slash");
  }

  // Test 21: Deep path
  {
    std::string dir = extractDirectory("/a/b/c/d/file.txt");
    runner.expectEqual("/a/b/c/d", dir, "extractDirectory: deep path");
  }

  // Test 22: Just directory
  {
    std::string dir = extractDirectory("/books/");
    runner.expectEqual("/books", dir, "extractDirectory: trailing slash");
  }

  // ============================================
  // hasExtension() tests
  // ============================================

  // Test 23: Matching extension lowercase
  {
    runner.expectTrue(hasExtension("file.txt", "txt"), "hasExtension: lowercase match");
  }

  // Test 24: Matching extension uppercase
  {
    runner.expectTrue(hasExtension("file.TXT", "txt"), "hasExtension: uppercase match");
  }

  // Test 25: Mixed case
  {
    runner.expectTrue(hasExtension("file.Txt", "TXT"), "hasExtension: mixed case");
  }

  // Test 26: No extension
  {
    runner.expectFalse(hasExtension("file", "txt"), "hasExtension: no extension");
  }

  // Test 27: Wrong extension
  {
    runner.expectFalse(hasExtension("file.pdf", "txt"), "hasExtension: wrong extension");
  }

  // Test 28: Partial match (longer)
  {
    runner.expectFalse(hasExtension("file.txt", "tx"), "hasExtension: partial longer");
  }

  // Test 29: Partial match (shorter)
  {
    runner.expectFalse(hasExtension("file.tx", "txt"), "hasExtension: partial shorter");
  }

  // Test 30: Empty filename
  {
    runner.expectFalse(hasExtension("", "txt"), "hasExtension: empty filename");
  }

  // Test 31: Just dot
  {
    runner.expectFalse(hasExtension(".", "txt"), "hasExtension: just dot");
  }

  // Test 32: Multiple extensions
  {
    runner.expectTrue(hasExtension("file.tar.gz", "gz"), "hasExtension: multiple extensions");
    runner.expectFalse(hasExtension("file.tar.gz", "tar"), "hasExtension: inner extension no match");
  }

  // Test 33: Hidden file with extension
  {
    runner.expectTrue(hasExtension(".hidden.txt", "txt"), "hasExtension: hidden file");
  }

  // Test 34: Extension with numbers
  {
    runner.expectTrue(hasExtension("file.mp3", "mp3"), "hasExtension: numeric extension");
  }

  // Test 35: Path with extension
  {
    runner.expectTrue(hasExtension("/path/to/file.epub", "epub"), "hasExtension: full path");
  }

  // ============================================
  // Cover image detection patterns
  // ============================================

  // Test 36-40: Common cover image patterns
  {
    runner.expectTrue(hasExtension("cover.jpg", "jpg"), "Cover: cover.jpg");
    runner.expectTrue(hasExtension("cover.jpeg", "jpeg"), "Cover: cover.jpeg");
    runner.expectTrue(hasExtension("cover.png", "png"), "Cover: cover.png");
    runner.expectTrue(hasExtension("cover.bmp", "bmp"), "Cover: cover.bmp");
    runner.expectTrue(hasExtension("COVER.JPG", "jpg"), "Cover: uppercase COVER.JPG");
  }

  // ============================================
  // Hash collision resistance
  // ============================================

  // Test 41: Similar filenames have different hashes
  {
    std::string path1 = generateCachePath("/cache", "/books/a.txt");
    std::string path2 = generateCachePath("/cache", "/books/b.txt");
    std::string path3 = generateCachePath("/cache", "/books/ab.txt");
    runner.expectTrue(path1 != path2, "Hash: a.txt != b.txt");
    runner.expectTrue(path1 != path3, "Hash: a.txt != ab.txt");
    runner.expectTrue(path2 != path3, "Hash: b.txt != ab.txt");
  }

  // Test 42: Case sensitivity in paths
  {
    std::string path1 = generateCachePath("/cache", "/Books/Novel.txt");
    std::string path2 = generateCachePath("/cache", "/books/novel.txt");
    // These should be different (filesystem is case-sensitive on Linux)
    runner.expectTrue(path1 != path2, "Hash: case-sensitive paths");
  }

  return runner.allPassed() ? 0 : 1;
}
