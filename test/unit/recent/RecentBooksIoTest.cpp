#include "RecentBooksStore.h"
#include "SDCardManager.h"
#include "test_utils.h"

#include <string>
#include <vector>

int main() {
  TestUtils::TestRunner runner("RecentBooksIo");
  SdMan.reset();

  std::vector<papyrix::RecentBook> legacyBooks;
  for (int i = 0; i < 12; ++i) {
    legacyBooks.push_back({"/legacy-" + std::to_string(i), "Legacy", ""});
  }
  SdMan.setFileData("/.papyrix/recent.bin", papyrix::RecentBooksStore::serializeList(legacyBooks));

  auto& store = papyrix::RecentBooksStore::instance();
  runner.expectTrue(store.load(), "legacy recent file loads");
  runner.expectEq(size_t(10), store.books().size(), "legacy recent file migrates to ten entries");
  const std::string migrated = SdMan.getWrittenData("/.papyrix/recent.bin");
  std::vector<papyrix::RecentBook> migratedBooks;
  runner.expectTrue(papyrix::RecentBooksStore::deserializeList(reinterpret_cast<const uint8_t*>(migrated.data()),
                                                               migrated.size(), migratedBooks),
                    "migrated recent file remains valid");
  runner.expectEq(size_t(10), migratedBooks.size(), "migration is persisted");

  runner.expectTrue(store.clearAndSave(), "migration setup clears");
  SdMan.setFileExists("/a.epub", true);
  SdMan.setFileExists("/b.epub", true);
  store.add("/a.epub", "A", "");
  store.add("/b.epub", "B", "");
  SdMan.setFileExists("/a.epub", false);
  runner.expectEq(size_t(1), store.pruneMissing(), "missing file pruned");
  runner.expectEq(size_t(1), store.books().size(), "existing file remains");
  runner.expectEq(std::string("/b.epub"), store.books()[0].path, "correct file remains");

  runner.expectTrue(store.clearAndSave(), "clear recent persists");
  runner.expectTrue(store.books().empty(), "clear recent empties memory");
  const std::string bytes = SdMan.getWrittenData("/.papyrix/recent.bin");
  std::vector<papyrix::RecentBook> decoded;
  runner.expectTrue(papyrix::RecentBooksStore::deserializeList(reinterpret_cast<const uint8_t*>(bytes.data()),
                                                               bytes.size(), decoded),
                    "cleared file remains valid");
  runner.expectTrue(decoded.empty(), "cleared file has zero entries");
  return runner.allPassed() ? 0 : 1;
}
