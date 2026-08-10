#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "SDCardManager.h"
#include "test_utils.h"

using papyrix::ReadingStatsRecord;
using papyrix::ReadingStatsStore;

int main() {
  TestUtils::TestRunner runner("ReadingStatsStoreLoadFailure");

  SdMan.reset();
  const std::vector<ReadingStatsRecord> seeded = {{"/short.epub", 30, 1, 4, true}};
  const auto bytes = ReadingStatsStore::serializeRecords(seeded);
  SdMan.registerFile("/.papyrix/reading-stats.bin",
                     std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  SdMan.setReadLimit(bytes.size() - 1);

  auto& store = ReadingStatsStore::instance();
  runner.expectFalse(store.load(), "short stats read fails load");
  runner.expectTrue(store.records().empty(), "short stats read clears records");
  runner.expectTrue(store.find("/short.epub") == nullptr, "short stats read exposes no partial record");

  return runner.allPassed() ? 0 : 1;
}
