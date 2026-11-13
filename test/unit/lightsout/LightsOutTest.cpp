#include <initializer_list>
#include <limits>

#include "apps/LightsOut.h"
#include "test_utils.h"

using namespace papyrix::lightsout;

int main() {
  TestUtils::TestRunner runner("Lights Out board");
  const uint8_t cells[] = {0, 4, 5, 12, 24};
  const uint32_t masks[] = {(1u << 0) | (1u << 1) | (1u << 5), (1u << 3) | (1u << 4) | (1u << 9),
                            (1u << 0) | (1u << 5) | (1u << 6) | (1u << 10),
                            (1u << 7) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 17),
                            (1u << 19) | (1u << 23) | (1u << 24)};
  for (unsigned i = 0; i < sizeof(cells); ++i) {
    Game game{1u << 20, 0, 12};
    runner.expectTrue(toggle(game, cells[i]), "toggle accepts unfinished game");
    runner.expectEq((1u << 20) ^ masks[i], game.lights, "toggle affects exact neighbours");
    runner.expectEq(cells[i], game.cursor, "toggle selects cell");
    runner.expectTrue(toggle(game, cells[i]), "toggle same cell again");
    runner.expectEq(1u << 20, game.lights, "repeated toggle reverses board");
    runner.expectEq(uint32_t{2}, game.moves, "moves count toggles");
  }
  for (uint32_t seed : {0u, 1u, 1u << 12, (1u << 0) | (1u << 4) | (1u << 24), 0xfe000001u}) {
    Game game = generate(seed);
    runner.expectTrue(valid(game) && !isWon(game), "generated board is valid and unfinished");
    runner.expectEq(uint32_t{0}, game.moves, "generation does not count moves");
    const uint32_t choices = seed & ((1u << kCells) - 1);
    if (choices == 0) {
      runner.expectTrue(toggle(game, kCells / 2), "zero seed has center inverse");
    } else {
      for (uint8_t i = 0; i < kCells; ++i)
        if (choices & (1u << i)) runner.expectTrue(toggle(game, i), "generated choice has legal inverse");
    }
    runner.expectTrue(isWon(game), "inverse choices solve board");
  }
  Game solved;
  runner.expectTrue(valid(solved) && isWon(solved), "solved state is valid");
  runner.expectFalse(toggle(solved, 0), "solved state cannot be toggled");
  Game game{1, 7, 12};
  runner.expectFalse(toggle(game, kCells), "out of range cell is ignored");
  runner.expectTrue(game.lights == 1 && game.moves == 7 && game.cursor == 12, "invalid cell preserves state");
  game.lights |= 1u << 25;
  runner.expectFalse(valid(game), "high light bits are invalid");
  runner.expectFalse(toggle(game, 0), "invalid board is ignored");
  runner.expectTrue(game.lights == ((1u << 25) | 1) && game.moves == 7 && game.cursor == 12,
                    "invalid board preserves state");
  game = {1, 7, kCells};
  runner.expectFalse(valid(game), "out of range cursor is invalid");
  runner.expectFalse(toggle(game, 0), "invalid cursor is ignored");
  game = {1, std::numeric_limits<uint32_t>::max(), 12};
  runner.expectTrue(toggle(game, 4), "toggle at maximum move count");
  runner.expectEq(std::numeric_limits<uint32_t>::max(), game.moves, "move count saturates");
  return runner.allPassed() ? 0 : 1;
}
