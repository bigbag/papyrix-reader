#include <esp_heap_caps.h>

#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "SDCardManager.h"
#include "test_utils.h"

using papyrix::RecentBook;
using papyrix::RecentBooksStore;

namespace {

bool listContains(const std::vector<RecentBook>& books, const std::string& path) {
  for (const auto& book : books) {
    if (book.path == path) return true;
  }
  return false;
}

bool writtenContains(const std::string& recentFile, const std::string& path) {
  const std::string bytes = SdMan.getWrittenData(recentFile);
  std::vector<RecentBook> books;
  if (!RecentBooksStore::deserializeList(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), books)) {
    return false;
  }
  return listContains(books, path);
}

std::vector<RecentBook> legacyList() {
  std::vector<RecentBook> books;
  for (int i = 0; i < 12; ++i) {
    books.push_back({"/legacy-" + std::to_string(i), "Legacy", ""});
  }
  return books;
}

}  // namespace

int main() {
  TestUtils::TestRunner runner("RecentBooksIo");
  const std::string recentFile = "/.papyrix/recent.bin";
  const std::string recentTmp = "/.papyrix/recent.bin.tmp";

  auto& store = RecentBooksStore::instance();

  SdMan.reset();
  runner.expectFalse(store.save(), "save before any load is refused");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "refused save writes nothing");

  runner.expectTrue(store.load(), "absent recent file loads empty");
  runner.expectTrue(store.books().empty(), "first use starts with an empty list");
  SdMan.setFileExists("/first.epub", true);
  store.add("/first.epub", "First", "");
  runner.expectTrue(writtenContains(recentFile, "/first.epub"), "first use persists the new book");

  for (int i = 0; i < 12; ++i) SdMan.setFileExists("/legacy-" + std::to_string(i), true);
  SdMan.setFileExists("/blocked.epub", true);
  SdMan.setFileData(recentFile, RecentBooksStore::serializeList(legacyList()));
  SdMan.setOpenFileForReadFailCount(4);
  runner.expectFalse(store.load(), "open failure of existing file fails load");
  runner.expectTrue(listContains(store.books(), "/first.epub"), "failed reload keeps the last good list");
  store.add("/blocked.epub", "Blocked", "");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "add after failed load writes nothing");
  store.remove("/legacy-0");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "remove after failed load writes nothing");
  runner.expectFalse(store.clearAndSave(), "clear after failed load is refused");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "refused clear writes nothing");
  runner.expectFalse(SdMan.exists(recentTmp), "blocked mutations leave no temp file");

  runner.expectTrue(store.load(), "retry after open failure loads the legacy file");
  runner.expectEq(size_t(10), store.books().size(), "legacy recent file migrates to ten entries");
  const std::string migrated = SdMan.getWrittenData(recentFile);
  std::vector<RecentBook> migratedBooks;
  runner.expectTrue(RecentBooksStore::deserializeList(reinterpret_cast<const uint8_t*>(migrated.data()),
                                                      migrated.size(), migratedBooks),
                    "migrated recent file remains valid");
  runner.expectEq(size_t(10), migratedBooks.size(), "migration is persisted");

  std::vector<RecentBook> titledBooks = legacyList();
  titledBooks.resize(10);
  for (auto& book : titledBooks) {
    book.title.assign(251, 't');
    book.author.assign(128, 'a');
  }
  SdMan.setFileData(recentFile, RecentBooksStore::serializeList(titledBooks));
  testSetHeapPool(MALLOC_CAP_INTERNAL, 65536, 8192);
  testSetHeapPool(MALLOC_CAP_SPIRAM, 0, 0);
  runner.expectTrue(store.load(), "a fragmented heap loads a valid ten-book list with long text");
  runner.expectTrue(store.books().size() == 10 && store.books().front().title == std::string(251, 't') &&
                        store.books().back().author == std::string(128, 'a'),
                    "a fragmented-heap load preserves the book text");
  testResetLargestFreeBlock();

  SdMan.setReadLimit(1);
  runner.expectFalse(store.load(), "an incomplete header fails the recent-list load");
  runner.expectFalse(store.save(), "an incomplete header does not permit a save");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "an incomplete header leaves saved bytes unchanged");
  SdMan.setReadLimit(100000);

  std::vector<uint8_t> corrupt = RecentBooksStore::serializeList(legacyList());
  corrupt[0] = 0xFF;  // unknown version
  SdMan.setFileData(recentFile, corrupt);
  runner.expectFalse(store.load(), "corrupt recent file fails load");
  runner.expectTrue(listContains(store.books(), "/legacy-0"), "corrupt load keeps the last good list");
  store.add("/blocked.epub", "Blocked", "");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "add after corrupt load writes nothing");
  runner.expectFalse(store.clearAndSave(), "clear after corrupt load is refused");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "corrupt file stays untouched");

  std::vector<uint8_t> truncated = RecentBooksStore::serializeList(legacyList());
  truncated.resize(truncated.size() - 3);
  SdMan.setFileData(recentFile, truncated);
  runner.expectFalse(store.load(), "truncated recent file fails load");
  runner.expectTrue(listContains(store.books(), "/legacy-0"), "truncated load keeps the last good list");

  SdMan.setFileData(recentFile, RecentBooksStore::serializeList(legacyList()));
  testSetLargestFreeBlock(128);
  runner.expectFalse(store.load(), "a low heap defers loading the recent list");
  runner.expectFalse(store.save(), "a low-heap load cannot replace the saved list");
  runner.expectTrue(SdMan.getWrittenData(recentFile).empty(), "a low-heap refusal leaves saved bytes unchanged");
  runner.expectTrue(listContains(store.books(), "/legacy-0"), "a low-heap refusal keeps the last valid list");
  testResetLargestFreeBlock();

  SdMan.setFileData(recentFile, RecentBooksStore::serializeList(legacyList()));
  runner.expectTrue(store.load(), "recovery load succeeds");
  runner.expectEq(size_t(10), store.books().size(), "recovery migrates to ten entries");
  SdMan.setFileExists("/recovered.epub", true);
  store.add("/recovered.epub", "Recovered", "");
  const std::string recoveredBytes = SdMan.getWrittenData(recentFile);
  std::vector<RecentBook> recovered;
  runner.expectTrue(RecentBooksStore::deserializeList(reinterpret_cast<const uint8_t*>(recoveredBytes.data()),
                                                      recoveredBytes.size(), recovered),
                    "recovered save stays valid");
  runner.expectEq(size_t(10), recovered.size(), "recovery keeps the ten book limit");
  runner.expectEq(std::string("/recovered.epub"), recovered.front().path, "recovery adds the new book first");
  runner.expectTrue(listContains(recovered, "/legacy-0"), "recovery keeps old books");

  const std::string committed = SdMan.getWrittenData(recentFile);
  SdMan.setFileExists("/shortwrite.epub", true);
  SdMan.setWriteLimit(3);
  store.add("/shortwrite.epub", "Short", "");
  runner.expectEq(committed, SdMan.getWrittenData(recentFile), "short write keeps the committed bytes");
  runner.expectFalse(SdMan.exists(recentTmp), "short write removes the temp file");
  SdMan.setWriteLimit(100000);

  SdMan.setSyncResult(false);
  SdMan.setFileExists("/syncfail.epub", true);
  store.add("/syncfail.epub", "Sync", "");
  runner.expectEq(committed, SdMan.getWrittenData(recentFile), "failed sync keeps the committed bytes");
  runner.expectFalse(SdMan.exists(recentTmp), "failed sync removes the temp file");
  SdMan.setSyncResult(true);

  runner.expectTrue(store.clearAndSave(), "clear after successful load persists");
  runner.expectTrue(store.books().empty(), "clear recent empties memory");
  SdMan.setFileExists("/a.epub", true);
  SdMan.setFileExists("/b.epub", true);
  store.add("/a.epub", "A", "");
  store.add("/b.epub", "B", "");
  SdMan.setFileExists("/a.epub", false);
  runner.expectEq(size_t(1), store.pruneMissing(), "missing file pruned");
  runner.expectEq(size_t(1), store.books().size(), "existing file remains");
  runner.expectEq(std::string("/b.epub"), store.books()[0].path, "correct file remains");
  store.remove("/b.epub");
  runner.expectTrue(store.load(), "removed recent entry reloads");
  runner.expectTrue(store.books().empty(), "removed book stays out of recent");
  runner.expectTrue(SdMan.exists("/b.epub"), "removing recent entry keeps book file");

  runner.expectTrue(store.clearAndSave(), "clear recent persists");
  runner.expectTrue(store.books().empty(), "clear recent empties memory");
  const std::string bytes = SdMan.getWrittenData(recentFile);
  std::vector<RecentBook> decoded;
  runner.expectTrue(RecentBooksStore::deserializeList(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
                                                      decoded),
                    "cleared file remains valid");
  runner.expectTrue(decoded.empty(), "cleared file has zero entries");
  return runner.allPassed() ? 0 : 1;
}
