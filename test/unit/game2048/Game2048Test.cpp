#include <cstring>
#include <limits>

#include "apps/Game2048.h"
#include "test_utils.h"

using namespace papyrix::game2048;

int main() {
  TestUtils::TestRunner runner("2048 board");
  const uint8_t rows[][4] = {{1, 1, 1, 1}, {1, 1, 2, 0}, {1, 0, 1, 1}};
  const uint8_t expected[][4] = {{2, 2, 0, 0}, {2, 2, 0, 0}, {2, 1, 0, 0}};
  const uint32_t scores[] = {8, 4, 4};
  const int positions[][4] = {{0, 1, 2, 3}, {0, 4, 8, 12}, {12, 8, 4, 0}, {3, 2, 1, 0}};
  for (int direction = 0; direction < 4; ++direction) {
    for (int row = 0; row < 3; ++row) {
      Game game;
      for (int i = 0; i < 4; ++i) game.cells[positions[direction][i]] = rows[row][i];
      runner.expectTrue(slide(game, static_cast<Direction>(direction)), "merge changes board");
      for (int i = 0; i < 4; ++i)
        runner.expectEq(expected[row][i], game.cells[positions[direction][i]], "merge once in move order");
      runner.expectEq(scores[row], game.score, "score adds merged values");
    }
  }
  Game game{{1, 2, 3, 4}, 24, false};
  const Game original = game;
  runner.expectFalse(slide(game, Direction::Left), "packed unequal row does not move");
  runner.expectTrue(std::memcmp(game.cells, original.cells, 16) == 0 && game.score == original.score,
                    "unchanged move preserves board and score");
  for (uint32_t random = 0; random < 10; ++random) {
    game = {};
    game.cells[0] = 3;
    runner.expectTrue(spawn(game, 1, random), "spawn into empty cell");
    runner.expectEq(uint8_t{3}, game.cells[0], "spawn preserves occupied cell");
    runner.expectEq(uint8_t(random == 0 ? 2 : 1), game.cells[2], "spawn value and empty-cell index");
  }
  game = {{1, 2, 1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1}, 0, false};
  runner.expectTrue(isStuck(game), "full checkerboard ends game");
  runner.expectFalse(spawn(game, 0, 0), "full board cannot spawn");
  game.cells[1] = 1;
  runner.expectFalse(isStuck(game), "adjacent pair can move");
  game.cells[1] = 0;
  runner.expectFalse(isStuck(game), "empty cell allows play");
  game.cells[1] = 11;
  runner.expectTrue(isWon(game), "2048 wins");
  game.cells[1] = 12;
  game.wonAcknowledged = true;
  runner.expectTrue(valid(game) && isWon(game), "continued game remains valid");
  game = {{1, 1}, 0, true};
  runner.expectFalse(valid(game), "cannot acknowledge nonexistent win");
  game.wonAcknowledged = false;
  game.cells[0] = 18;
  runner.expectFalse(valid(game), "reject out-of-range exponent");
  runner.expectFalse(slide(game, Direction::Left), "invalid exponent never shifts");
  game = {{17, 17}, 0, false};
  runner.expectFalse(slide(game, Direction::Left), "maximum tiles do not overflow");
  game = {{1, 1}, std::numeric_limits<uint32_t>::max() - 2, false};
  runner.expectFalse(slide(game, Direction::Left), "score overflow rejects entire move");
  runner.expectEq(uint8_t{1}, game.cells[0], "overflow leaves original tiles");
  runner.expectEq(uint8_t{1}, game.cells[1], "overflow does not consume tile");
  return runner.allPassed() ? 0 : 1;
}
