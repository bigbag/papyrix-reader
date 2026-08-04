#include "test_utils.h"

#include <FileIndex.h>
#include <FsHelpers.h>
#include <SDCardManager.h>
#include <platform_stubs.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

bool acceptBook(const char* name, bool isDir) {
  if (!name || name[0] == '.' || FsHelpers::isHiddenFsItem(name)) return false;
  return isDir || FsHelpers::isSupportedBookFile(name);
}

std::vector<MockDirectoryEntry> makeEntries(size_t bookCount) {
  std::vector<MockDirectoryEntry> entries;
  entries.push_back({"Series 10", true});
  entries.push_back({"Series 2", true});
  entries.push_back({"cover.jpg", false});
  entries.push_back({".hidden.epub", false});
  entries.push_back({"System Volume Information", true});

  for (size_t i = bookCount; i > 0; i--) {
    char name[32];
    snprintf(name, sizeof(name), "Book %zu.epub", i);
    entries.push_back({name, false});
  }
  return entries;
}

std::string indexPath() {
  for (const std::string& path : SdMan.writtenFilePaths()) {
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".idx") == 0) return path;
  }
  return {};
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("FileIndex");
  SdMan.reset();

  auto entries = makeEntries(300);
  SdMan.registerDirectory("/books", entries);

  FileIndex index;
  runner.expectTrue(index.open("/books", acceptBook), "builds index for large directory");
  runner.expectEq(size_t(302), index.size(), "indexes every accepted entry");

  FileIndex::Entry entry{};
  runner.expectTrue(index.entryAt(0, entry), "reads first row");
  runner.expectTrue(entry.isDir, "directories sort before files");
  runner.expectEqual("Series 2", entry.name, "directory names use natural order");
  runner.expectTrue(index.entryAt(1, entry), "reads second directory");
  runner.expectEqual("Series 10", entry.name, "second directory follows natural order");
  runner.expectTrue(index.entryAt(3, entry), "reads naturally sorted file");
  runner.expectEqual("Book 2.epub", entry.name, "Book 2 sorts before Book 10");
  runner.expectEq(size_t(3), index.findRowByName("Book 2.epub"), "finds exact filename row");
  runner.expectEq(size_t(3), index.findRowByName("book 2.epub"), "lookup preserves case-insensitive selection");
  runner.expectFalse(index.entryAt(index.size(), entry), "rejects out-of-range row");

  const std::string path = indexPath();
  runner.expectFalse(path.empty(), "commits an index file");
  const std::string originalIndex = SdMan.getWrittenData(path);
  runner.expectFalse(originalIndex.empty(), "committed index has content");

  index.close();
  runner.expectTrue(index.open("/books", acceptBook), "reopens valid cached index");
  runner.expectEq(size_t(302), index.size(), "cached index keeps count");
  runner.expectEqual(originalIndex, SdMan.getWrittenData(path), "valid index is reused unchanged");

  entries.push_back({"Book 301.epub", false});
  SdMan.registerDirectory("/books", entries);
  index.close();
  runner.expectTrue(index.open("/books", acceptBook), "rebuilds after adding a file");
  runner.expectEq(size_t(303), index.size(), "rebuilt index includes added file");
  runner.expectTrue(index.entryAt(index.size() - 1, entry), "reads added final row");
  runner.expectEqual("Book 301.epub", entry.name, "added file is naturally sorted");

  entries.erase(std::remove_if(entries.begin(), entries.end(), [](const MockDirectoryEntry& item) {
                  return item.name == "Book 150.epub";
                }),
                entries.end());
  SdMan.registerDirectory("/books", entries);
  index.close();
  runner.expectTrue(index.open("/books", acceptBook), "rebuilds after removing a file");
  runner.expectEq(size_t(302), index.size(), "rebuilt index removes deleted file");
  runner.expectEq(SIZE_MAX, index.findRowByName("Book 150.epub"), "deleted file is absent");

  for (MockDirectoryEntry& item : entries) {
    if (item.name == "Book 1.epub") item.isDirectory = true;
  }
  SdMan.registerDirectory("/books", entries);
  index.close();
  runner.expectTrue(index.open("/books", acceptBook), "rebuilds after entry type changes");
  runner.expectTrue(index.entryAt(0, entry) && entry.isDir, "changed directory remains in directory section");
  runner.expectEq(size_t(0), index.findRowByName("Book 1.epub"), "changed directory moves before other directories");

  const std::string rebuiltPath = indexPath();
  const std::string rebuiltIndex = SdMan.getWrittenData(rebuiltPath);
  SdMan.setFileData(rebuiltPath, rebuiltIndex.substr(0, 10));
  index.close();
  runner.expectTrue(index.open("/books", acceptBook), "rebuilds truncated cached index");
  runner.expectEq(size_t(302), index.size(), "truncated index rebuild restores all entries");
  runner.expectTrue(SdMan.getWrittenData(rebuiltPath).size() > 10, "replacement index is complete");

  index.close();
  SdMan.reset();
  SdMan.registerDirectory("/books", makeEntries(300));
  testSetLargestFreeBlock(1024);
  FileIndex lowHeapIndex;
  runner.expectFalse(lowHeapIndex.open("/books", acceptBook), "fails safely when build heap is unavailable");
  testResetLargestFreeBlock();

  return runner.allPassed() ? 0 : 1;
}
