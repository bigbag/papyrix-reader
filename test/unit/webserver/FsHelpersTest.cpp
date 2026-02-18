// Tests for FsHelpers file type detection utilities.

#include "test_utils.h"

#include <FsHelpers.h>

int main() {
  TestUtils::TestRunner runner("FsHelpers");

  // --- isFb2File ---
  runner.expectTrue(FsHelpers::isFb2File("book.fb2"), "fb2 lowercase");
  runner.expectTrue(FsHelpers::isFb2File("book.FB2"), "fb2 uppercase");
  runner.expectTrue(FsHelpers::isFb2File("/path/to/book.fb2"), "fb2 with path");
  runner.expectFalse(FsHelpers::isFb2File("book.epub"), "fb2 rejects epub");
  runner.expectFalse(FsHelpers::isFb2File("fb2"), "fb2 rejects no dot");

  // --- isXtcFile with .xtg and .xth ---
  runner.expectTrue(FsHelpers::isXtcFile("file.xtc"), "xtc basic");
  runner.expectTrue(FsHelpers::isXtcFile("file.xtch"), "xtch basic");
  runner.expectTrue(FsHelpers::isXtcFile("file.xtg"), "xtg recognized");
  runner.expectTrue(FsHelpers::isXtcFile("file.xth"), "xth recognized");
  runner.expectTrue(FsHelpers::isXtcFile("file.XTG"), "xtg case insensitive");
  runner.expectTrue(FsHelpers::isXtcFile("file.XTH"), "xth case insensitive");
  runner.expectFalse(FsHelpers::isXtcFile("file.txt"), "xtc rejects txt");

  // --- isSupportedBookFile includes all formats ---
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.epub"), "supported: epub");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.fb2"), "supported: fb2");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.xtc"), "supported: xtc");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.xtg"), "supported: xtg");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.xth"), "supported: xth");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.txt"), "supported: txt");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.md"), "supported: md");
  runner.expectTrue(FsHelpers::isSupportedBookFile("book.markdown"), "supported: markdown");
  runner.expectFalse(FsHelpers::isSupportedBookFile("file.pdf"), "unsupported: pdf");
  runner.expectFalse(FsHelpers::isSupportedBookFile("file.doc"), "unsupported: doc");
  runner.expectFalse(FsHelpers::isSupportedBookFile("file.jpg"), "unsupported: jpg (image, not book)");

  // --- isImageFile ---
  runner.expectTrue(FsHelpers::isImageFile("photo.jpg"), "image: jpg");
  runner.expectTrue(FsHelpers::isImageFile("photo.jpeg"), "image: jpeg");
  runner.expectTrue(FsHelpers::isImageFile("photo.png"), "image: png");
  runner.expectTrue(FsHelpers::isImageFile("photo.bmp"), "image: bmp");
  runner.expectFalse(FsHelpers::isImageFile("book.epub"), "image rejects epub");

  // --- hasExtension edge cases ---
  runner.expectFalse(FsHelpers::hasExtension(static_cast<const char*>(nullptr), ".epub"), "null path");
  runner.expectFalse(FsHelpers::hasExtension("book.epub", nullptr), "null ext");
  runner.expectFalse(FsHelpers::hasExtension("noext", ".epub"), "no extension in path");

  return runner.allPassed() ? 0 : 1;
}
