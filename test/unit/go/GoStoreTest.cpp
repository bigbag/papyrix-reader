#include <uzlib.h>

#include <cstring>
#include <type_traits>

#include "GoStoreMock.h"
#include "apps/GoStore.h"
#include "test_utils.h"

using namespace papyrix::go_store;
using namespace go_store_test;

static_assert(sizeof(HistoryReader) <= 2048, "HistoryReader exceeds the memory budget");
static_assert(!std::is_copy_constructible<HistoryReader>::value, "HistoryReader owns its file");

namespace {
void checksum(std::string& data) {
  uint32_t crc = uzlib_crc32(data.data(), 20, UINT32_MAX);
  crc = uzlib_crc32(data.data() + 24, data.size() - 24, crc);
  crc = ~crc;
  for (int i = 0; i < 4; ++i) data[20 + i] = char(crc >> (8 * i));
}

bool same(const Game& a, const Game& b) {
  return a.seed == b.seed && a.position.cells == b.position.cells && a.position.toMove == b.position.toMove &&
         a.position.passes == b.position.passes && a.position.lastMove == b.position.lastMove &&
         a.position.outcome == b.position.outcome && a.position.turns == b.position.turns;
}

uint32_t hash(const gogame::Key& key) {
  uint32_t h = 2166136261u;
  for (auto b : key) h = (h ^ b) * 16777619u;
  return h;
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("Go storage");
  papyrix::hal::Storage storage;
  Game game, loaded;
  reset();
  runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "missing save");
  runner.expectTrue(startNew(storage, game, 0x12345678), "publish new game");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "load empty game");
  runner.expectTrue(same(game, loaded), "empty game round trip");

  const gogame::Move ko[] = {11, 20, 19, 12, 29, 22, 80, 30, 21};
  bool movesOk = true;
  for (auto move : ko) movesOk &= play(storage, game, move) == gogame::MoveResult::Ok;
  runner.expectTrue(movesOk, "publish ko setup");
  const Game before = game;
  const std::string saved = *files.at(finalPath);
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "load ko game");
  runner.expectTrue(same(before, loaded), "ko game round trip");
  runner.expectEq(int(gogame::MoveResult::Repetition), int(play(storage, loaded, 20)), "superko survives resume");
  runner.expectTrue(same(before, loaded), "repetition does not change game");

  HistoryReader reader;
  runner.expectTrue(reader.open(storage, game), "open root history");
  auto history = reader.history();
  runner.expectEq(int(gogame::HistoryResult::Present), int(history.contains(history.context, gogame::Key{})),
                  "history includes empty board");
  const auto currentKey = gogame::key(game.position);
  runner.expectEq(int(gogame::HistoryResult::Present), int(history.contains(history.context, currentKey)),
                  "history includes current board");
  gogame::Key collision{};
  bool found = false;
  for (uint32_t n = 1; n < 200000 && !found; ++n) {
    collision = {};
    uint32_t v = n;
    for (int p = 0; p < 12; ++p) {
      collision[p / 4] |= uint8_t(v % 3) << (2 * (p % 4));
      v /= 3;
    }
    bool recorded = collision == gogame::Key{};
    for (size_t i = 0; i < sizeof(ko); ++i)
      recorded |= std::memcmp(saved.data() + 25 + i * 22, collision.data(), collision.size()) == 0;
    found = !recorded && (hash(collision) & 1023) == (hash(currentKey) & 1023);
  }
  runner.expectTrue(found, "make a filter collision");
  runner.expectEq(int(gogame::HistoryResult::Absent), int(history.contains(history.context, collision)),
                  "filter collision uses exact comparison");
  runner.expectTrue(reader.close(), "close history");

  reads = 0;
  failure = Failure::HistoryRead;
  runner.expectTrue(reader.open(storage, game), "open history before read fault");
  history = reader.history();
  runner.expectEq(int(gogame::HistoryResult::IOError), int(history.contains(history.context, currentKey)),
                  "history read failure is not absence");
  reader.close();
  failure = Failure::None;
  bool abortHistory = false;
  runner.expectTrue(reader.open(storage, game, [&] { return abortHistory; }), "open cancellable history");
  history = reader.history();
  abortHistory = true;
  runner.expectEq(int(gogame::HistoryResult::Cancelled), int(history.contains(history.context, currentKey)),
                  "history scan propagates cancellation");
  reader.close();
  runner.expectEq(int(LoadResult::Cancelled), int(load(storage, loaded, [] { return true; })), "cancel load");
  runner.expectEq(int(gogame::MoveResult::Cancelled), int(play(storage, loaded, 2, [] { return true; })),
                  "cancel move");

  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Preserve, Failure::Publish,
                           Failure::Rollback, Failure::PreserveAfter, Failure::PublishAfter}) {
    reset();
    put(finalPath, saved);
    Game attempt = before;
    failure = fault;
    runner.expectEq(int(gogame::MoveResult::IOError), int(play(storage, attempt, 2)), "failed move reports error");
    runner.expectTrue(same(before, attempt), "failed move keeps live game");
    failure = Failure::None;
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "failed move retains complete game");
    runner.expectTrue(same(before, loaded), "failed move retains old position");
    runner.expectEq(int(gogame::MoveResult::Repetition), int(play(storage, loaded, 20)),
                    "failed move retains repetition history");
    runner.expectEq(int(gogame::MoveResult::Ok), int(play(storage, loaded, 2)), "move retries after recovery");
  }
  for (const auto fault : {Failure::Write, Failure::Sync, Failure::Close, Failure::Publish, Failure::PublishAfter}) {
    reset();
    failure = fault;
    Game attempt = before;
    runner.expectFalse(startNew(storage, attempt, 7), "failed first publication reports error");
    runner.expectTrue(same(before, attempt), "failed new game retains live game");
    failure = Failure::None;
    runner.expectEq(int(LoadResult::Missing), int(load(storage, loaded)), "failed first publication leaves no game");
  }
  reset();
  put(finalPath, saved);
  failure = Failure::Write;
  Game attempt = before;
  runner.expectFalse(startNew(storage, attempt, 7), "failed restart reports error");
  runner.expectTrue(same(before, attempt), "failed restart retains live game");
  failure = Failure::None;
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "failed restart retains save");
  runner.expectTrue(same(before, loaded), "failed restart retains old game");
  failure = Failure::Write;
  attempt = before;
  runner.expectEq(int(gogame::MoveResult::IOError), int(play(storage, attempt, gogame::kPass)),
                  "failed pass reports error");
  runner.expectTrue(same(before, attempt), "failed pass keeps live game");
  runner.expectFalse(resign(storage, attempt, gogame::Color::White), "failed resignation reports error");
  runner.expectTrue(same(before, attempt), "failed resignation keeps live game");
  failure = Failure::None;
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "failed pass and resignation retain save");
  runner.expectTrue(same(before, loaded), "failed pass and resignation retain old position");
  for (int layout = 0; layout < 3; ++layout) {
    reset();
    if (layout != 2) put(finalPath, "bad primary");
    put(backupPath, layout == 0 ? saved : "bad backup");
    attempt = before;
    failure = Failure::Write;
    runner.expectFalse(startNew(storage, attempt, 77), "failed restart retains prior state");
    runner.expectTrue(same(before, attempt), "failed restart does not change live state");
    failure = Failure::None;
    if (layout == 0) {
      runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "failed restart retains valid backup");
      runner.expectTrue(same(before, loaded), "failed restart retains complete backup game");
    } else {
      runner.expectEq(int(LoadResult::Invalid), int(load(storage, loaded)), "failed restart retains invalid bytes");
    }
    runner.expectTrue(startNew(storage, attempt, 77), "restart replaces invalid-only save");
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "restart publishes empty save");
    runner.expectTrue(same(attempt, loaded) && loaded.seed == 77 && loaded.position.turns == 0,
                      "restart round trips new game");
  }

  for (int kind = 0; kind < 12; ++kind) {
    reset();
    std::string bad = saved;
    switch (kind) {
      case 0:
        bad[0] = '?';
        break;
      case 1:
        bad[4] = 2;
        break;
      case 2:
        bad.resize(23);
        break;
      case 3:
        bad.pop_back();
        break;
      case 4:
        bad += 'x';
        break;
      case 5:
        bad[20] ^= 1;
        break;
      case 6:
        bad[12] = char(0xff);
        bad[13] = char(0xff);
        bad[14] = char(0xff);
        bad[15] = char(0xff);
        break;
      case 7:
        bad[24] = char(99);
        checksum(bad);
        break;
      case 8:
        bad[25] = char(3);
        checksum(bad);
        break;
      case 9:
        bad[45] = char(0xfc);
        checksum(bad);
        break;
      case 10:
        bad[16] = char(gogame::Outcome::Scored);
        checksum(bad);
        break;
      case 11:
        bad[12] = 10;
        bad.push_back(char(20));
        bad.append(saved.data() + 25 + 7 * 22, 21);
        checksum(bad);
        break;
    }
    put(finalPath, bad);
    loaded = before;
    runner.expectEq(int(LoadResult::Invalid), int(load(storage, loaded)), "reject corrupt or illegal record");
    runner.expectTrue(same(before, loaded), "invalid load leaves game unchanged");
    runner.expectTrue(*files.at(finalPath) == bad, "invalid save remains intact");
    runner.expectFalse(play(storage, loaded, 2) == gogame::MoveResult::Ok, "invalid save cannot be silently replaced");
    runner.expectTrue(*files.at(finalPath) == bad, "failed move preserves invalid save");
    runner.expectTrue(startNew(storage, loaded, 9), "explicit restart can replace invalid save");
  }
  for (const auto fault : {Failure::Remove, Failure::Recovery, Failure::RecoveryAfter, Failure::Write}) {
    reset();
    put(finalPath, "invalid primary");
    put(backupPath, saved);
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "read backup with invalid primary");
    failure = fault;
    runner.expectEq(int(gogame::MoveResult::IOError), int(play(storage, loaded, 2)),
                    "failed recovery or publication reports error");
    runner.expectTrue(same(before, loaded), "failed recovery keeps live game");
    failure = Failure::None;
    runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "failed recovery retains valid save");
    runner.expectTrue(same(before, loaded), "failed recovery retains complete position");
    runner.expectEq(int(gogame::MoveResult::Repetition), int(play(storage, loaded, 20)),
                    "failed recovery retains superko");
    runner.expectEq(int(gogame::MoveResult::Ok), int(play(storage, loaded, 2)), "recovery retries and plays");
  }
  reset();
  put(finalPath, "invalid primary");
  put(backupPath, saved);
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "load valid backup");
  runner.expectEq(int(gogame::MoveResult::Ok), int(play(storage, loaded, 2)), "promote valid backup and publish move");
  reset();
  put(backupPath, saved);
  put(tempPath, "incomplete");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "recover missing primary from backup");
  runner.expectTrue(same(before, loaded), "backup retains complete state");
  runner.expectEq(int(gogame::MoveResult::Repetition), int(play(storage, loaded, 20)), "backup retains superko");
  runner.expectEq(int(gogame::MoveResult::Ok), int(play(storage, loaded, 2)), "publish after backup recovery");
  failure = Failure::Read;
  runner.expectEq(int(LoadResult::IOError), int(load(storage, loaded)), "read error is not missing");
  failure = Failure::Exists;
  runner.expectEq(int(LoadResult::IOError), int(load(storage, loaded)), "existence error is not missing");
  failure = Failure::None;

  reset();
  runner.expectTrue(startNew(storage, game, 13), "start long game");
  bool longOk = true;
  for (int turn = 0; turn < 36; ++turn) {
    bool played = false;
    for (int point = 0; point < gogame::kPoints && !played; ++point)
      played = play(storage, game, point) == gogame::MoveResult::Ok;
    if (!played) {
      longOk = false;
      break;
    }
  }
  runner.expectTrue(longOk && game.position.turns == 36, "publish history larger than copy buffer");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "load long history");
  runner.expectTrue(same(game, loaded), "long history round trip");
  runner.expectTrue(reader.open(storage, loaded), "open long history");
  history = reader.history();
  gogame::Key firstKey{};
  std::memcpy(firstKey.data(), files.at(finalPath)->data() + 25, firstKey.size());
  runner.expectEq(int(gogame::HistoryResult::Present), int(history.contains(history.context, firstKey)),
                  "long history retains its first coloring");
  reader.close();
  runner.expectEq(int(gogame::MoveResult::Ok), int(play(storage, game, gogame::kPass)), "publish pass");
  runner.expectEq(int(gogame::MoveResult::Ok), int(play(storage, game, gogame::kPass)), "publish second pass");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "load scored game");
  runner.expectTrue(same(game, loaded) && loaded.position.outcome == gogame::Outcome::Scored, "passes finish game");
  runner.expectEq(int(gogame::MoveResult::GameOver), int(play(storage, loaded, 0)), "finished game rejects moves");
  runner.expectTrue(startNew(storage, game, 14), "restart after score");
  runner.expectTrue(resign(storage, game, gogame::Color::Black), "publish resignation");
  runner.expectEq(int(LoadResult::Loaded), int(load(storage, loaded)), "load resignation");
  runner.expectTrue(same(game, loaded) && loaded.position.outcome == gogame::Outcome::BlackResigned,
                    "resignation survives resume");
  return runner.allPassed() ? 0 : 1;
}
