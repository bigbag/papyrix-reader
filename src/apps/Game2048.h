#pragma once

#include <cstdint>

namespace papyrix::game2048 {
enum class Direction : uint8_t { Left, Up, Down, Right };
struct Game {
  uint8_t cells[16]{};
  uint32_t score = 0;
  bool wonAcknowledged = false;
};
bool slide(Game& game, Direction direction);
bool spawn(Game& game, uint32_t cellRandom, uint32_t valueRandom);
bool isWon(const Game& game);
bool isStuck(const Game& game);
bool valid(const Game& game);
}  // namespace papyrix::game2048
