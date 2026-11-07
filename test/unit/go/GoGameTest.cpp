#include <GoGame.h>

#include <vector>

#include "test_utils.h"

using namespace gogame;

struct MemoryHistory {
  std::vector<Key> keys;
  HistoryResult result = HistoryResult::Absent;
  static HistoryResult contains(void* context, const Key& value) {
    auto& self = *static_cast<MemoryHistory*>(context);
    if (self.result != HistoryResult::Absent) return self.result;
    for (const auto& entry : self.keys)
      if (entry == value) return HistoryResult::Present;
    return HistoryResult::Absent;
  }
  History adapter() { return {this, contains}; }
};

int main() {
  TestUtils::TestRunner runner("Go rules");
  MemoryHistory memory;
  History history = memory.adapter();
  Position empty, out;
  runner.expectTrue(key(empty) == Key{}, "empty key");
  runner.expectTrue(play(empty, 0, out, history) == MoveResult::Ok && out.cells[0] == Color::Black, "first placement");
  memory.keys.push_back(key(out));
  Position unchanged = out;
  runner.expectTrue(play(empty, 0, out, history) == MoveResult::Repetition && key(out) == key(unchanged),
                    "complete history forbids repeated coloring without changing output");
  memory.keys.clear();
  memory.result = HistoryResult::IOError;
  runner.expectTrue(play(empty, 1, out, history) == MoveResult::IOError && key(out) == key(unchanged),
                    "lookup error preserves output");
  memory.result = HistoryResult::Cancelled;
  runner.expectTrue(play(empty, 1, out, history) == MoveResult::Cancelled && key(out) == key(unchanged),
                    "cancelled lookup preserves output");
  Position alias = empty;
  runner.expectTrue(
      play(alias, 1, alias, history) == MoveResult::Cancelled && key(alias) == key(empty) && alias.turns == 0,
      "cancelled aliased move preserves position");
  runner.expectTrue(play(empty, 1, out, History{}) == MoveResult::IOError, "missing history fails closed");
  memory.result = HistoryResult::Absent;

  Position capture;
  capture.cells[0] = Color::White;
  capture.cells[1] = Color::Black;
  runner.expectTrue(play(capture, 9, out, history) == MoveResult::Ok && out.cells[0] == Color::Empty,
                    "capture removes opponent stones");

  Position chain;
  for (int i = 0; i < 8; ++i) chain.cells[i] = Color::White;
  for (int i = 9; i < 16; ++i) chain.cells[i] = Color::Black;
  runner.expectTrue(play(chain, 16, out, history) == MoveResult::Ok, "remote liberty permits adjacent placement");
  bool intact = true;
  for (int i = 0; i < 8; ++i) intact &= out.cells[i] == Color::White;
  runner.expectTrue(intact, "a liberty at the far end preserves the complete group");
  chain.cells[16] = Color::Black;
  runner.expectTrue(play(chain, 8, out, history) == MoveResult::Ok, "closing the final liberty permits capture");
  bool removed = true;
  for (int i = 0; i < 8; ++i) removed &= out.cells[i] == Color::Empty;
  runner.expectTrue(removed, "capture removes every stone of a group without liberties");

  Position snap;
  snap.toMove = Color::White;
  snap.cells[0] = Color::Black;
  snap.cells[9] = snap.cells[10] = Color::White;
  snap.cells[2] = snap.cells[11] = snap.cells[18] = snap.cells[19] = Color::Black;
  memory.keys.push_back(key(snap));
  Position captured;
  runner.expectTrue(play(snap, 1, captured, history) == MoveResult::Ok && captured.cells[0] == Color::Empty,
                    "snapback first capture");
  memory.keys.push_back(key(captured));
  runner.expectTrue(play(captured, 0, out, history) == MoveResult::Ok && out.cells[1] == Color::Empty &&
                        out.cells[9] == Color::Empty && out.cells[10] == Color::Empty,
                    "snapback recaptures a larger group");
  memory.keys.clear();

  Position suicide;
  suicide.cells[0] = Color::Black;
  suicide.cells[2] = suicide.cells[9] = suicide.cells[10] = suicide.cells[11] = suicide.cells[19] = Color::White;
  runner.expectTrue(
      play(suicide, 1, out, history) == MoveResult::Ok && out.cells[0] == Color::Empty && out.cells[1] == Color::Empty,
      "multi-stone suicide is legal when coloring is new");
  Position emptyRepeat;
  emptyRepeat.cells[1] = emptyRepeat.cells[9] = Color::White;
  memory.keys.clear();
  runner.expectTrue(play(emptyRepeat, 0, out, history) == MoveResult::Repetition,
                    "suicide repeating its current coloring is rejected");
  memory.keys.clear();

  Position passed;
  runner.expectTrue(play(empty, kPass, passed, history) == MoveResult::Ok && passed.passes == 1,
                    "pass bypasses repetition");
  runner.expectTrue(play(empty, kPass, passed, History{}) == MoveResult::Ok, "pass does not require history");
  Position afterPassMove;
  runner.expectTrue(play(passed, 0, afterPassMove, history) == MoveResult::Ok && afterPassMove.passes == 0,
                    "placement resets pass count");
  runner.expectTrue(play(passed, kPass, out, history) == MoveResult::Ok && out.outcome == Outcome::Scored,
                    "two passes finish game");
  runner.expectTrue(play(out, kPass, passed, history) == MoveResult::GameOver, "finished game rejects pass");
  Score emptyScore = score(empty);
  runner.expectTrue(emptyScore.blackHalf == 0 && emptyScore.whiteHalf == 15, "empty board and komi");
  Position surrounded;
  surrounded.cells.fill(Color::Black);
  surrounded.cells[40] = Color::Empty;
  Score surroundedScore = score(surrounded);
  runner.expectTrue(surroundedScore.blackHalf == 162 && surroundedScore.whiteHalf == 15,
                    "exclusive empty region counts as area");
  Position neutral;
  for (int i = 0; i < kPoints; ++i) neutral.cells[i] = Color::Black;
  neutral.cells[0] = Color::White;
  neutral.cells[1] = Color::Empty;
  Score neutralScore = score(neutral);
  runner.expectTrue(neutralScore.blackHalf == 158 && neutralScore.whiteHalf == 17,
                    "mixed region is neutral and stones count once");
  Position resigned;
  runner.expectTrue(
      resign(resigned, Color::Black) && winner(resigned) == Color::White && !resign(resigned, Color::White),
      "resignation finishes game");
  Position offTurn;
  offTurn.toMove = Color::White;
  runner.expectTrue(resign(offTurn, Color::Black) && offTurn.outcome == Outcome::BlackResigned &&
                        winner(offTurn) == Color::White && offTurn.toMove == Color::White,
                    "black may resign during white turn");
  return runner.allPassed() ? 0 : 1;
}
