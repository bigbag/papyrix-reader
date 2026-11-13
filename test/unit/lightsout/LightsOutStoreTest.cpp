#include "apps/LightsOutStore.h"
#include "lightsout_storage.h"
#include "test_utils.h"

using namespace papyrix::lightsout;
using namespace lightsout_test;

static bool equal(const Game& a, const Game& b) {
  return a.lights == b.lights && a.moves == b.moves && a.cursor == b.cursor;
}

int main() {
  TestUtils::TestRunner runner("Lights Out storage");
  papyrix::hal::Storage storage;
  const Game original{0x123456, 123456, 24};
  Game loaded{7, 8, 9};
  const Game sentinel = loaded;
  runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "missing game starts fresh");
  runner.expectTrue(equal(loaded, sentinel), "missing load preserves output");
  runner.expectTrue(save(storage, original), "save game");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "saved game loads");
  runner.expectTrue(equal(original, loaded), "board moves and cursor survive reopen");
  const std::string record = *files.at(finalPath);
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Preserve, Failure::Publish,
                           Failure::Rollback, Failure::PreserveAfter, Failure::PublishAfter}) {
    reset();
    put(finalPath, record);
    Game changed = original;
    ++changed.moves;
    failure = fault;
    runner.expectFalse(save(storage, changed), "failed publication reports failure");
    failure = Failure::None;
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "old checkpoint survives failed publication");
    runner.expectTrue(equal(original, loaded), "failed publication preserves checkpoint");
  }
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Publish, Failure::PublishAfter}) {
    reset();
    failure = fault;
    runner.expectFalse(save(storage, original), "failed first save reports failure");
    failure = Failure::None;
    loaded = sentinel;
    runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "failed first save publishes no game");
    runner.expectTrue(equal(loaded, sentinel), "missing load preserves output");
  }
  for (int corruption = 0; corruption < 6; ++corruption) {
    reset();
    std::string broken = record;
    if (corruption == 0) broken[0] = '?';
    if (corruption == 1) broken[4] = 2;
    if (corruption == 2) broken.resize(13);
    if (corruption == 3) broken += 'x';
    if (corruption == 4) broken[8] = static_cast<char>(0x80);
    if (corruption == 5) broken[13] = kCells;
    put(finalPath, broken);
    loaded = sentinel;
    runner.expectEq(int(LoadResult::Invalid), int(load(storage, loaded)), "invalid record is reported");
    runner.expectTrue(equal(loaded, sentinel), "invalid load preserves output");
    put(backupPath, record);
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "backup recovers invalid primary");
    runner.expectTrue(equal(original, loaded), "backup restores all fields");
    runner.expectTrue(save(storage, loaded), "recovered game replaces invalid primary");
  }
  reset();
  put(tempPath, record);
  runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "temporary file never loads");
  put(backupPath, record);
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "backup recovers missing primary");
  runner.expectTrue(save(storage, original), "save after interrupted rename");
  loaded = sentinel;
  failure = Failure::Read;
  runner.expectEq(int(LoadResult::IOError), int(load(storage, loaded)), "read error is reported");
  runner.expectTrue(equal(loaded, sentinel), "read error preserves output");
  failure = Failure::None;
  Game invalid = original;
  invalid.cursor = kCells;
  runner.expectFalse(save(storage, invalid), "invalid game cannot replace checkpoint");
  invalid = original;
  invalid.lights |= 1u << 25;
  runner.expectFalse(save(storage, invalid), "high bits cannot be saved");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "checkpoint remains after invalid save");
  runner.expectTrue(equal(original, loaded), "invalid save preserves checkpoint");
  const Game solved{0, 99, 3};
  runner.expectTrue(save(storage, solved), "solved game saves");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "solved game loads");
  runner.expectTrue(equal(solved, loaded) && isWon(loaded), "solved game remains solved");
  put(backupPath, record);
  put(tempPath, record);
  failure = Failure::Clear;
  runner.expectFalse(clear(storage), "clear failure is reported");
  failure = Failure::None;
  runner.expectTrue(clear(storage), "clear saved game");
  runner.expectTrue(files.count(finalPath) == 0 && files.count(backupPath) == 0 && files.count(tempPath) == 0,
                    "clear removes all records");
  runner.expectTrue(clear(storage), "clear absent game");
  return runner.allPassed() ? 0 : 1;
}
