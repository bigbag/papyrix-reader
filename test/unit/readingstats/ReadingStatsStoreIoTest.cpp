#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "SDCardManager.h"
#include "test_utils.h"

using papyrix::ReadingStatsRecord;
using papyrix::ReadingStatsStore;

int main() {
  TestUtils::TestRunner runner("ReadingStatsStoreIo");
  SdMan.reset();

  const std::vector<ReadingStatsRecord> seeded = {{"/one.epub", 30, 1, 4, true}, {"/two.epub", 60, 2, 20, true}};
  const auto bytes = ReadingStatsStore::serializeRecords(seeded);
  SdMan.registerFile("/.papyrix/reading-stats.bin",
                     std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));

  auto& store = ReadingStatsStore::instance();
  runner.expectTrue(store.load(), "streaming load succeeds");
  runner.expectTrue(store.records() == seeded, "streaming load preserves records");

  runner.expectTrue(store.recordSession("/three.epub", 90, true, 30), "new session changes store");
  runner.expectTrue(store.save(), "streaming save succeeds");

  const std::string saved = SdMan.getWrittenData("/.papyrix/reading-stats.bin");
  std::vector<ReadingStatsRecord> decoded;
  runner.expectTrue(
      ReadingStatsStore::deserializeRecords(reinterpret_cast<const uint8_t*>(saved.data()), saved.size(), decoded),
      "streaming save keeps the wire format");
  runner.expectEq(size_t(3), decoded.size(), "streaming save writes all records");
  runner.expectEq(std::string("/three.epub"), decoded.front().path, "newest record remains first");

  return runner.allPassed() ? 0 : 1;
}
