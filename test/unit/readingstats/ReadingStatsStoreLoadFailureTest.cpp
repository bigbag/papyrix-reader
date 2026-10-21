#include <string>
#include <vector>

#include "ReadingStatsStore.h"
#include "SDCardManager.h"
#include "test_utils.h"

using papyrix::ReadingStatsRecord;
using papyrix::ReadingStatsStore;

int main() {
  TestUtils::TestRunner runner("ReadingStatsStoreLoadFailure");
  const std::string statsFile = "/.papyrix/reading-stats.bin";
  const std::string statsTmp = "/.papyrix/reading-stats.bin.tmp";

  auto& store = ReadingStatsStore::instance();

  SdMan.reset();
  runner.expectFalse(store.save(), "save before any load is refused");
  runner.expectFalse(SdMan.exists(statsTmp), "refused save leaves no temp file");

  std::vector<uint8_t> corrupt = ReadingStatsStore::serializeRecords({{"/gone.epub", 30, 1, 4, true}});
  corrupt[0] = 0xFF;  // unknown version
  SdMan.setFileData(statsFile, corrupt);
  runner.expectFalse(store.load(), "corrupt stats file fails load");
  runner.expectTrue(store.records().empty(), "failed load exposes no records");
  runner.expectFalse(store.recordSession("/new.epub", 30, true, 10),
                     "session recording blocked after failed load");
  runner.expectFalse(store.save(), "save after failed load is refused");
  runner.expectFalse(SdMan.exists(statsTmp), "blocked save leaves no temp file");

  const std::vector<ReadingStatsRecord> seeded = {{"/short.epub", 30, 1, 4, true}};
  const auto bytes = ReadingStatsStore::serializeRecords(seeded);
  SdMan.setFileData(statsFile, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  SdMan.setReadLimit(bytes.size() - 1);
  runner.expectFalse(store.load(), "short stats read fails load");
  runner.expectTrue(store.records().empty(), "short stats read keeps records empty");
  runner.expectTrue(store.find("/short.epub") == nullptr, "short stats read exposes no partial record");
  runner.expectFalse(store.recordSession("/new.epub", 30, true, 10),
                     "session recording blocked after short read");
  runner.expectFalse(store.save(), "save after short read is refused");

  SdMan.setOpenFileForReadFailCount(1);
  runner.expectFalse(store.load(), "open failure of existing file fails load");
  runner.expectFalse(store.save(), "save after open failure is refused");

  SdMan.setReadLimit(bytes.size() + 64);
  runner.expectTrue(store.load(), "retry after failures succeeds");
  runner.expectTrue(store.records() == seeded, "recovered load restores seeded records");
  runner.expectTrue(store.recordSession("/new.epub", 60, true, 50), "new session applies after recovery");
  runner.expectTrue(store.save(), "save after recovery succeeds");

  const std::string saved = SdMan.getWrittenData(statsFile);
  std::vector<ReadingStatsRecord> decoded;
  runner.expectTrue(
      ReadingStatsStore::deserializeRecords(reinterpret_cast<const uint8_t*>(saved.data()), saved.size(), decoded),
      "recovered save keeps the wire format");
  runner.expectEq(size_t(2), decoded.size(), "recovered save writes old and new records");
  runner.expectEq(std::string("/new.epub"), decoded.front().path, "recovered save keeps the new session");
  runner.expectEq(std::string("/short.epub"), decoded.back().path, "recovered save keeps the old record");

  return runner.allPassed() ? 0 : 1;
}
