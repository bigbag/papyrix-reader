#include <GoSearch.h>

#include "test_utils.h"

using namespace gogame;

struct HistoryState {
  HistoryResult result = HistoryResult::Absent;
  Key forbidden{};
  bool hasForbidden = false;
  static HistoryResult contains(void* context, const Key& value) {
    auto& state = *static_cast<HistoryState*>(context);
    if (state.result != HistoryResult::Absent) return state.result;
    return state.hasForbidden && value == state.forbidden ? HistoryResult::Present : HistoryResult::Absent;
  }
  History adapter() { return {this, contains}; }
};

struct ControlState {
  unsigned yields = 0;
  unsigned stopAfter = 0;
  unsigned budgetAfter = 0;
  bool expired = false;
  static bool abort(void* context) {
    auto& state = *static_cast<ControlState*>(context);
    return state.stopAfter && state.yields >= state.stopAfter;
  }
  static void yield(void* context) { ++static_cast<ControlState*>(context)->yields; }
  static bool budgetExpired(void* context) {
    auto& state = *static_cast<ControlState*>(context);
    return state.expired || (state.budgetAfter && state.yields >= state.budgetAfter);
  }
  SearchControl adapter() { return {this, abort, yield, budgetExpired}; }
};

int main() {
  TestUtils::TestRunner runner("Go search");
  static Search search;
  static_assert(sizeof(Search) <= 20 * 1024, "search workspace exceeds budget");
  HistoryState state;
  History history = state.adapter();
  ControlState control;
  Position full;
  full.cells.fill(Color::White);
  full.toMove = Color::Black;
  full.passes = 1;
  const Key original = key(full);
  SearchResult result = search.run(full, history, 7, control.adapter());
  runner.expectTrue(result.status == SearchStatus::Ready && result.move == kPass && result.simulations == 512,
                    "search returns legal pass on full board");
  runner.expectTrue(key(full) == original && full.toMove == Color::Black && full.passes == 1,
                    "search leaves root unchanged");
  runner.expectTrue(control.yields == 512, "search yields once per simulation");
  Position winningPass;
  winningPass.toMove = Color::White;
  winningPass.passes = 1;
  result = search.run(winningPass, history, 42, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::Ready && result.move == kPass && result.simulations == 0,
                    "proven terminal win takes priority over rollout estimates");
  runner.expectTrue(winningPass.outcome == Outcome::Playing && winningPass.passes == 1,
                    "winning pass search leaves root unchanged");
  ControlState immediateStop;
  immediateStop.stopAfter = 1;
  immediateStop.yields = 1;
  result = search.run(winningPass, history, 42, immediateStop.adapter());
  runner.expectTrue(result.status == SearchStatus::Cancelled && result.move == kNoMove,
                    "cancellation takes priority over a winning pass");
  Position blackWinningPass;
  blackWinningPass.passes = 1;
  blackWinningPass.cells[40] = Color::Black;
  result = search.run(blackWinningPass, history, 42, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::Ready && result.move == kPass && result.simulations == 0,
                    "proven terminal win works for either color");

  Position opening, chosen;
  ControlState timed;
  timed.budgetAfter = 3;
  result = search.run(opening, history, 7, timed.adapter());
  runner.expectTrue(result.status == SearchStatus::Ready && result.simulations == 3,
                    "budget expiry returns the completed search instead of cancelling it");
  runner.expectTrue(play(opening, result.move, chosen, history) == MoveResult::Ok,
                    "budget-limited search returns a legal root move");
  timed = {};
  timed.expired = true;
  result = search.run(opening, history, 7, timed.adapter());
  runner.expectTrue(result.status == SearchStatus::Ready && result.simulations == 1 &&
                        play(opening, result.move, chosen, history) == MoveResult::Ok,
                    "an expired budget still completes one simulation to select a validated move");
  state.result = HistoryResult::IOError;
  result = search.run(opening, history, 7, timed.adapter());
  runner.expectTrue(result.status == SearchStatus::IOError && result.move == kNoMove,
                    "budget expiry cannot bypass a history read failure");
  state.result = HistoryResult::Absent;

  Position tactical = full;
  tactical.cells[0] = Color::Black;
  tactical.cells[1] = Color::Empty;
  result = search.run(tactical, history, 123, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::Ready && result.move == 1,
                    "search captures a group instead of passing to lose");
  timed = {};
  timed.budgetAfter = 2;
  result = search.run(tactical, history, 11, timed.adapter());
  runner.expectTrue(result.status == SearchStatus::Ready && result.simulations == 2 && result.move == 1,
                    "equal-visit root choices prefer the winning capture over a losing pass");
  Position captured;
  runner.expectTrue(play(tactical, 1, captured, history) == MoveResult::Ok, "capture is legal");
  state.hasForbidden = true;
  state.forbidden = key(captured);
  result = search.run(tactical, history, 123, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::Ready && result.move == kPass,
                    "search respects complete supplied history");
  state.hasForbidden = false;
  state.result = HistoryResult::IOError;
  result = search.run(tactical, history, 123, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::IOError && result.move == kNoMove, "lookup failure stops search");
  state.result = HistoryResult::Cancelled;
  result = search.run(tactical, history, 123, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::Cancelled && result.move == kNoMove,
                    "history cancellation stops search");
  state.result = HistoryResult::Absent;
  ControlState stop;
  stop.stopAfter = 1;
  stop.budgetAfter = 1;
  result = search.run(full, history, 7, stop.adapter());
  runner.expectTrue(result.status == SearchStatus::Cancelled && result.simulations == 1 && result.move == kNoMove,
                    "cancellation takes priority over budget expiry and discards the move");
  result = search.run(full, History{}, 7, SearchControl{});
  runner.expectTrue(result.status == SearchStatus::Invalid, "missing history is invalid");
  return runner.allPassed() ? 0 : 1;
}
