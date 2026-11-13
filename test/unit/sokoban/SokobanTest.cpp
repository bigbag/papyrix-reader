#include <cstring>
#include <limits>

#include "apps/Sokoban.h"
#include "test_utils.h"

using namespace papyrix::sokoban;

static Direction direction(char c) {
  switch (c) {
    case 'U':
      return Direction::Up;
    case 'D':
      return Direction::Down;
    case 'L':
      return Direction::Left;
    default:
      return Direction::Right;
  }
}

int main() {
  TestUtils::TestRunner runner("Sokoban board");
  const char* solutions[] = {
      "RRD",        "URRUL",         "UUURDDRDL",       "LDLDRRUU",       "DDDLLUDLLUURR",   "LLDDLDRRURD",
      "UULLDRURDD", "LULLRRDLLLUUU", "UULLLDDRDRURULL", "LLLRDDDDRURULL", "DLDDLUDLUDRRRUU", "LUUULDRDLDUULDD"};
  for (uint8_t i = 0; i < kLevelCount; ++i) {
    Game game;
    runner.expectTrue(start(game, i) && valid(game) && !isWon(game), "level starts valid and unsolved");
    const Level& map = *level(i);
    runner.expectTrue(
        map.width <= kMaxSide && map.height <= kMaxSide && std::strlen(map.cells) == unsigned(map.width) * map.height,
        "level dimensions match cells");
    unsigned goals = 0;
    unsigned players = 0;
    for (const char* cell = map.cells; *cell; ++cell) {
      if (*cell == '.' || *cell == '*' || *cell == '+') ++goals;
      if (*cell == '@' || *cell == '+') ++players;
    }
    runner.expectTrue(goals == game.boxCount && players == 1, "each level has one player and one target per box");
    for (unsigned cell = 0; cell < unsigned(map.width) * map.height; ++cell) {
      if (cell < map.width || cell >= unsigned(map.width) * (map.height - 1) || cell % map.width == 0 ||
          cell % map.width == map.width - 1)
        runner.expectTrue(map.cells[cell] == '#', "outer boundary is walled");
    }
    for (const char* p = solutions[i]; *p; ++p) {
      runner.expectTrue(move(game, direction(*p)), "solution move succeeds");
      runner.expectTrue(valid(game), "solution history stays valid");
    }
    runner.expectTrue(isWon(game), "solution completes level");
    runner.expectFalse(move(game, Direction::Up), "solved game stops movement");
    runner.expectTrue(undo(game) && !isWon(game), "undo reopens solved board");
  }
  Game game;
  runner.expectTrue(start(game, 0), "start first level");
  const Game initial = game;
  runner.expectFalse(start(game, kLevelCount), "invalid level rejected");
  runner.expectTrue(game.player == initial.player && game.boxes == initial.boxes, "invalid start preserves game");
  runner.expectFalse(move(game, Direction::Up), "wall blocks movement");
  runner.expectTrue(game.moves == 0 && game.undoCount == 0, "blocked movement does not count");
  runner.expectTrue(move(game, Direction::Right) && move(game, Direction::Right), "walk behind box");
  runner.expectTrue(move(game, Direction::Down) && isWon(game), "push box onto goal");
  runner.expectTrue(game.moves == 3 && game.pushes == 1, "moves and pushes counted");
  runner.expectTrue(undo(game) && game.pushes == 0 && game.moves == 2, "undo restores pushed box and counters");
  runner.expectTrue(game.boxes[0] == initial.boxes[0], "undo restores box position");
  runner.expectTrue(undo(game) && undo(game) && !undo(game), "undo stops at start");
  runner.expectTrue(game.player == initial.player && game.boxes == initial.boxes, "undo restores initial board");
  game = initial;
  game.player = 24;
  runner.expectTrue(move(game, Direction::Up), "push box towards wall");
  runner.expectFalse(move(game, Direction::Up), "wall blocks box push");
  const uint8_t boxAtWall = game.boxes[0];
  runner.expectTrue(move(game, Direction::Down) && game.boxes[0] == boxAtWall, "walking away does not pull box");
  game = initial;
  for (unsigned i = 0; i < 140; ++i)
    runner.expectTrue(move(game, i % 2 ? Direction::Left : Direction::Right), "walking fills circular history");
  runner.expectTrue(game.undoCount == kUndoLimit && valid(game), "wrapped history remains valid");
  for (unsigned i = 0; i < kUndoLimit; ++i) runner.expectTrue(undo(game), "wrapped entry undoes");
  runner.expectFalse(undo(game), "discarded history cannot be undone");
  runner.expectTrue(game.moves == 12 && valid(game), "ring retains oldest available state");
  start(game, 0);
  game.moves = std::numeric_limits<uint32_t>::max();
  game.pushes = std::numeric_limits<uint32_t>::max();
  runner.expectTrue(move(game, Direction::Right) && move(game, Direction::Right) && move(game, Direction::Down),
                    "saturated game can push");
  runner.expectTrue(valid(game) && undo(game) && undo(game) && undo(game), "saturated history reverses");
  runner.expectTrue(
      game.moves == std::numeric_limits<uint32_t>::max() && game.pushes == std::numeric_limits<uint32_t>::max(),
      "undo preserves saturated counters");
  start(game, 0);
  move(game, Direction::Right);
  Game bad = game;
  bad.history[0].player = bad.player;
  runner.expectFalse(valid(bad), "nonadjacent reverse move rejected");
  bad = game;
  bad.history[0].box = 0;
  runner.expectFalse(valid(bad), "false push rejected");
  bad = game;
  bad.history[0].countedMove = false;
  runner.expectFalse(valid(bad), "false saturation flag rejected");
  bad = game;
  bad.undoNext = kUndoLimit;
  runner.expectFalse(valid(bad), "ring index out of bounds rejected");
  bad = game;
  bad.boxes[0] = bad.player;
  runner.expectFalse(valid(bad), "box cannot overlap player");
  bad = game;
  bad.pushes = bad.moves;
  const Game unchanged = bad;
  runner.expectFalse(undo(bad), "invalid reverse counters reject undo");
  runner.expectTrue(bad.player == unchanged.player && bad.boxes == unchanged.boxes && bad.moves == unchanged.moves &&
                        bad.pushes == unchanged.pushes && bad.undoCount == unchanged.undoCount &&
                        bad.undoNext == unchanged.undoNext,
                    "failed undo preserves the current game");
  return runner.allPassed() ? 0 : 1;
}
