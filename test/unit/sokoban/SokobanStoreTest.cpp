#include "apps/SokobanStore.h"
#include "sokoban_storage.h"
#include "test_utils.h"

using namespace papyrix::sokoban;
using namespace sokoban_test;

static bool equal(const Game& a, const Game& b) {
  if (a.level != b.level || a.player != b.player || a.boxCount != b.boxCount || a.boxes != b.boxes ||
      a.moves != b.moves || a.pushes != b.pushes || a.undoCount != b.undoCount || a.undoNext != b.undoNext)
    return false;
  for (unsigned i = 0; i < kUndoLimit; ++i)
    if (a.history[i].player != b.history[i].player || a.history[i].box != b.history[i].box ||
        a.history[i].countedMove != b.history[i].countedMove || a.history[i].countedPush != b.history[i].countedPush)
      return false;
  return true;
}

int main() {
  TestUtils::TestRunner runner("Sokoban storage");
  papyrix::hal::Storage storage;
  Game original;
  start(original, 0);
  move(original, Direction::Right);
  move(original, Direction::Right);
  move(original, Direction::Down);
  Game loaded;
  start(loaded, 1);
  const Game sentinel = loaded;
  runner.expectTrue(load(storage, loaded) == LoadResult::Missing && equal(loaded, sentinel),
                    "missing save preserves output");
  runner.expectTrue(save(storage, original), "save game");
  runner.expectTrue(load(storage, loaded) == LoadResult::Loaded && equal(loaded, original), "board and history reload");
  runner.expectTrue(isWon(loaded) && undo(loaded) && !isWon(loaded) && loaded.pushes == 0,
                    "loaded undo reverses winning push");
  const std::string record = *files.at(finalPath);
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Preserve, Failure::Publish,
                           Failure::Rollback, Failure::PreserveAfter, Failure::PublishAfter}) {
    reset();
    put(finalPath, record);
    Game changed = original;
    undo(changed);
    failure = fault;
    runner.expectFalse(save(storage, changed), "publication fault is reported");
    failure = Failure::None;
    runner.expectTrue(load(storage, loaded) == LoadResult::Loaded && equal(loaded, original),
                      "publication fault keeps previous checkpoint");
  }
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Publish, Failure::PublishAfter}) {
    reset();
    failure = fault;
    runner.expectFalse(save(storage, original), "first publication fault is reported");
    failure = Failure::None;
    loaded = sentinel;
    runner.expectTrue(load(storage, loaded) == LoadResult::Missing && equal(loaded, sentinel),
                      "failed first publication leaves no checkpoint");
  }
  for (unsigned corruption = 0; corruption < 14; ++corruption) {
    reset();
    std::string broken = record;
    switch (corruption) {
      case 0:
        broken[0] = '?';
        break;
      case 1:
        broken[4] = 2;
        break;
      case 2:
        broken.resize(broken.size() - 1);
        break;
      case 3:
        broken += 'x';
        break;
      case 4:
        broken[5] = kLevelCount;
        break;
      case 5:
        broken[6] = 0;
        break;
      case 6:
        broken[7] = 9;
        break;
      case 7:
        broken[8] = broken[6];
        break;
      case 8:
        broken[24] = char(129);
        break;
      case 9:
        broken[25] = char(128);
        break;
      case 10:
        broken[26 + 2 * 3] = broken[6];
        break;
      case 11:
        broken[26 + 2 * 3 + 1] = char(254);
        break;
      case 12:
        broken[26 + 2 * 3 + 2] = 4;
        break;
      case 13:
        broken[26 + 2 * 3 + 2] = 0;
        break;
    }
    put(finalPath, broken);
    loaded = sentinel;
    runner.expectTrue(load(storage, loaded) == LoadResult::Invalid && equal(loaded, sentinel),
                      "malformed record preserves output");
    runner.expectFalse(save(storage, original), "invalid checkpoint is not replaced implicitly");
    put(backupPath, record);
    runner.expectTrue(load(storage, loaded) == LoadResult::Loaded && equal(loaded, original),
                      "valid backup recovers invalid primary");
    runner.expectTrue(save(storage, loaded), "recovered checkpoint can be saved");
  }
  reset();
  put(tempPath, record);
  runner.expectTrue(load(storage, loaded) == LoadResult::Missing, "temporary file is not a checkpoint");
  put(backupPath, record);
  runner.expectTrue(load(storage, loaded) == LoadResult::Loaded && equal(loaded, original),
                    "missing primary recovers backup");
  runner.expectTrue(save(storage, original), "save after interrupted rename");
  failure = Failure::Read;
  loaded = sentinel;
  runner.expectTrue(load(storage, loaded) == LoadResult::IOError && equal(loaded, sentinel),
                    "read failure preserves output");
  failure = Failure::None;
  Game invalid = original;
  invalid.history[2].box = kMaxBoxes;
  runner.expectFalse(save(storage, invalid), "invalid history cannot replace checkpoint");
  runner.expectTrue(load(storage, loaded) == LoadResult::Loaded && equal(loaded, original),
                    "invalid save keeps checkpoint");
  reset();
  Game wrapped;
  start(wrapped, 0);
  for (unsigned i = 0; i < 140; ++i) move(wrapped, i % 2 ? Direction::Left : Direction::Right);
  runner.expectTrue(save(storage, wrapped) && load(storage, loaded) == LoadResult::Loaded && equal(wrapped, loaded),
                    "wrapped undo history survives reload");
  for (unsigned i = 0; i < kUndoLimit; ++i) runner.expectTrue(undo(loaded), "reloaded ring entry undoes");
  runner.expectTrue(!undo(loaded) && loaded.moves == 12, "reloaded ring has exact retained boundary");
  put(backupPath, record);
  put(tempPath, record);
  failure = Failure::Clear;
  runner.expectFalse(clear(storage), "clear failure reported");
  failure = Failure::None;
  runner.expectTrue(clear(storage) && files.empty() && clear(storage), "clear removes all records and is idempotent");
  return runner.allPassed() ? 0 : 1;
}
