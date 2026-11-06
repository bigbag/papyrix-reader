#include "apps/Game2048Store.h"
#include "game2048_storage.h"
#include "test_utils.h"

using namespace papyrix::game2048;
using namespace game2048_test;

int main() {
  TestUtils::TestRunner runner("2048 storage");
  papyrix::hal::Storage storage;
  Game original{{11, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0, 0, 0}, 123456, true};
  Game loaded;
  runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "missing game starts fresh");
  runner.expectTrue(save(storage, original), "save unfinished game");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "saved game loads");
  runner.expectTrue(
      std::memcmp(original.cells, loaded.cells, 16) == 0 && original.score == loaded.score && loaded.wonAcknowledged,
      "complete game survives reopen");
  const std::string record = *files.at(finalPath);
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Preserve, Failure::Publish,
                           Failure::Rollback, Failure::PreserveAfter, Failure::PublishAfter}) {
    reset();
    put(finalPath, record);
    Game changed = original;
    changed.score += 4;
    failure = fault;
    runner.expectFalse(save(storage, changed), "failed publication reports failure");
    failure = Failure::None;
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "old game survives failed publication");
    runner.expectEq(original.score, loaded.score, "failed publication preserves old score");
  }
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Publish, Failure::PublishAfter}) {
    reset();
    failure = fault;
    runner.expectFalse(save(storage, original), "failed first save reports failure");
    failure = Failure::None;
    runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "failed first save does not publish a game");
  }
  for (int corruption = 0; corruption < 7; ++corruption) {
    reset();
    std::string broken = record;
    if (corruption == 0) broken[0] = '?';
    if (corruption == 1) broken[4] = 2;
    if (corruption == 2) broken.resize(12);
    if (corruption == 3) broken += 'x';
    if (corruption == 4) broken[5] = 32;
    if (corruption == 5) broken[25] = 2;
    if (corruption == 6)
      for (int i = 5; i < 21; ++i) broken[i] = 0;
    put(finalPath, broken);
    loaded = original;
    runner.expectEq(int(LoadResult::Invalid), int(load(storage, loaded)), "invalid record is not a fresh game");
    runner.expectEq(original.score, loaded.score, "invalid load does not publish state");
    put(backupPath, record);
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "valid backup recovers invalid primary");
    runner.expectTrue(save(storage, loaded), "recovered game can replace invalid primary safely");
  }
  reset();
  put(tempPath, record);
  runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "uncommitted temporary game never loads");
  put(backupPath, record);
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "backup recovers missing primary");
  runner.expectTrue(save(storage, original), "save after interrupted rename");
  failure = Failure::Read;
  runner.expectEq(int(LoadResult::IOError), int(load(storage, loaded)), "read error is not missing game");
  failure = Failure::None;
  put(backupPath, record);
  put(tempPath, record);
  failure = Failure::Clear;
  runner.expectFalse(clear(storage), "failed clear stays visible");
  failure = Failure::None;
  runner.expectTrue(clear(storage), "clear saved game");
  runner.expectTrue(files.count(finalPath) == 0 && files.count(backupPath) == 0 && files.count(tempPath) == 0,
                    "clear removes every recovery record");
  runner.expectTrue(clear(storage), "already absent game clears successfully");
  std::string ended = record;
  for (int i = 0; i < 16; ++i) ended[5 + i] = (i / 4 + i % 4) % 2 + 1;
  ended[25] = 0;
  put(finalPath, ended);
  put(backupPath, record);
  runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)),
                  "finished primary clears even older unfinished backup");
  runner.expectTrue(files.count(backupPath) == 0, "finished game cannot resurrect from backup");
  return runner.allPassed() ? 0 : 1;
}
