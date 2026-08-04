#include "ReadingStatsStore.h"
#include "SDCardManager.h"
#include "test_utils.h"

#include <climits>
#include <string>
#include <vector>

using papyrix::ReadingStatsRecord;
using papyrix::ReadingStatsStore;

int main() {
  TestUtils::TestRunner runner("ReadingStatsStore");

  {
    std::vector<ReadingStatsRecord> records;
    records = ReadingStatsStore::applySession(std::move(records), "/books/a.epub", 125, true, 18);
    runner.expectEq(size_t(1), records.size(), "new record created");
    runner.expectEq(uint32_t(125), records[0].totalSeconds, "time recorded");
    runner.expectEq(uint32_t(1), records[0].sessionCount, "session recorded");
    runner.expectTrue(records[0].hasProgress, "progress marked known");
    runner.expectEq(uint8_t(18), records[0].progressPercent, "progress recorded");
  }

  {
    std::vector<ReadingStatsRecord> records = {{"/a", 10, 1, 5, true}, {"/b", 20, 2, 8, true}};
    records = ReadingStatsStore::applySession(std::move(records), "/b", 30, true, 40);
    runner.expectEq(size_t(2), records.size(), "update keeps size");
    runner.expectEq(std::string("/b"), records[0].path, "updated record moves to front");
    runner.expectEq(uint32_t(50), records[0].totalSeconds, "time accumulates");
    runner.expectEq(uint32_t(3), records[0].sessionCount, "sessions accumulate");
    runner.expectEq(uint8_t(40), records[0].progressPercent, "progress refreshes");
  }

  {
    std::vector<ReadingStatsRecord> records = {{"/a", UINT32_MAX - 2, UINT32_MAX, 1, true}};
    records = ReadingStatsStore::applySession(std::move(records), "/a", 10, true, 2);
    runner.expectEq(UINT32_MAX, records[0].totalSeconds, "time saturates");
    runner.expectEq(UINT32_MAX, records[0].sessionCount, "sessions saturate");
  }

  {
    std::vector<ReadingStatsRecord> records;
    records = ReadingStatsStore::applySession(std::move(records), "/a", 0, true, 55);
    runner.expectEq(uint32_t(0), records[0].sessionCount, "zero-time update is not a session");
    runner.expectEq(uint8_t(55), records[0].progressPercent, "zero-time update keeps progress");
  }

  {
    std::vector<ReadingStatsRecord> records;
    for (size_t i = 0; i < ReadingStatsStore::MAX_RECORDS + 3; ++i) {
      records = ReadingStatsStore::applySession(std::move(records), "/" + std::to_string(i), 1, false, 0);
    }
    runner.expectEq(ReadingStatsStore::MAX_RECORDS, records.size(), "record count bounded");
    runner.expectEq(std::string("/66"), records.front().path, "newest retained");
    runner.expectEq(std::string("/3"), records.back().path, "oldest overflow evicted");
  }

  {
    const std::vector<ReadingStatsRecord> records = {{"/books/a.epub", 3601, 7, 62, true},
                                                     {"/books/b.txt", 0, 0, 0, false}};
    const auto bytes = ReadingStatsStore::serializeRecords(records);
    std::vector<ReadingStatsRecord> out;
    runner.expectTrue(ReadingStatsStore::deserializeRecords(bytes.data(), bytes.size(), out), "round-trip valid");
    runner.expectEq(records.size(), out.size(), "round-trip count");
    runner.expectEq(records[0].path, out[0].path, "round-trip path");
    runner.expectEq(records[0].totalSeconds, out[0].totalSeconds, "round-trip time");
    runner.expectEq(records[0].sessionCount, out[0].sessionCount, "round-trip sessions");
    runner.expectEq(records[0].progressPercent, out[0].progressPercent, "round-trip progress");
    runner.expectEq(records[0].hasProgress, out[0].hasProgress, "round-trip progress flag");
  }

  {
    const uint8_t truncated[] = {ReadingStatsStore::FILE_VERSION, 1, 5, 0, '/'};
    std::vector<ReadingStatsRecord> out = {{"stale", 1, 1, 1, true}};
    runner.expectFalse(ReadingStatsStore::deserializeRecords(truncated, sizeof(truncated), out),
                       "truncated record rejected");
    runner.expectTrue(out.empty(), "failed decode clears output");
  }

  {
    const uint8_t unknown[] = {99, 0};
    std::vector<ReadingStatsRecord> out;
    runner.expectFalse(ReadingStatsStore::deserializeRecords(unknown, sizeof(unknown), out),
                       "unknown version rejected");
  }

  {
    SdMan.reset();
    auto& store = ReadingStatsStore::instance();
    runner.expectTrue(store.recordSession("/io.epub", 60, true, 10), "I/O record changes store");
    runner.expectTrue(store.save(), "atomic save succeeds");
    const std::string bytes = SdMan.getWrittenData("/.papyrix/reading-stats.bin");
    std::vector<ReadingStatsRecord> decoded;
    runner.expectTrue(ReadingStatsStore::deserializeRecords(reinterpret_cast<const uint8_t*>(bytes.data()),
                                                            bytes.size(), decoded),
                      "saved bytes decode");
    runner.expectEq(size_t(1), decoded.size(), "saved record count");
    runner.expectEq(std::string("/io.epub"), decoded[0].path, "saved path");
  }

  return runner.allPassed() ? 0 : 1;
}
