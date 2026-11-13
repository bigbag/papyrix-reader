#include "LightsOut.h"

#include <limits>

namespace papyrix::lightsout {
namespace {
uint32_t mask(uint8_t index) {
  uint32_t result = uint32_t{1} << index;
  if (index >= kSide) result |= uint32_t{1} << (index - kSide);
  if (index + kSide < kCells) result |= uint32_t{1} << (index + kSide);
  if (index % kSide != 0) result |= uint32_t{1} << (index - 1);
  if (index % kSide != kSide - 1) result |= uint32_t{1} << (index + 1);
  return result;
}
}  // namespace

Game generate(uint32_t toggles) {
  Game game;
  for (uint8_t i = 0; i < kCells; ++i)
    if (toggles & (uint32_t{1} << i)) game.lights ^= mask(i);
  if (game.lights == 0) game.lights ^= mask(kCells / 2);
  return game;
}

bool valid(const Game& game) { return (game.lights >> kCells) == 0 && game.cursor < kCells; }
bool isWon(const Game& game) { return valid(game) && game.lights == 0; }

bool toggle(Game& game, uint8_t index) {
  if (!valid(game) || isWon(game) || index >= kCells) return false;
  game.lights ^= mask(index);
  game.cursor = index;
  if (game.moves != std::numeric_limits<uint32_t>::max()) ++game.moves;
  return true;
}
}  // namespace papyrix::lightsout
