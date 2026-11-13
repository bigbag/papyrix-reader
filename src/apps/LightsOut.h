#pragma once

#include <cstdint>

namespace papyrix::lightsout {
constexpr uint8_t kSide = 5;
constexpr uint8_t kCells = 25;
struct Game {
  uint32_t lights = 0;
  uint32_t moves = 0;
  uint8_t cursor = kCells / 2;
};
Game generate(uint32_t toggles);
bool toggle(Game& game, uint8_t index);
bool valid(const Game& game);
bool isWon(const Game& game);
}  // namespace papyrix::lightsout
