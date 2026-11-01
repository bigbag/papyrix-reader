#include "Game2048.h"

#include <cstring>
#include <limits>

namespace papyrix::game2048 {
namespace {
bool mergeable(uint8_t a, uint8_t b) { return a != 0 && a < 17 && a == b; }
int index(Direction direction, int row, int column) {
  switch (direction) {
    case Direction::Left:
      return row * 4 + column;
    case Direction::Up:
      return column * 4 + row;
    case Direction::Down:
      return (3 - column) * 4 + row;
    case Direction::Right:
      return row * 4 + 3 - column;
  }
  return 0;
}
}  // namespace

bool isWon(const Game& game) {
  for (const auto cell : game.cells)
    if (cell >= 11 && cell <= 17) return true;
  return false;
}

bool valid(const Game& game) {
  for (const auto cell : game.cells)
    if (cell > 17) return false;
  return !game.wonAcknowledged || isWon(game);
}

bool slide(Game& game, Direction direction) {
  if (!valid(game) || static_cast<unsigned>(direction) > static_cast<unsigned>(Direction::Right)) return false;
  Game next = game;
  for (int row = 0; row < 4; ++row) {
    uint8_t packed[4]{};
    int count = 0;
    for (int column = 0; column < 4; ++column) {
      const auto cell = game.cells[index(direction, row, column)];
      if (cell) packed[count++] = cell;
    }
    uint8_t merged[4]{};
    int output = 0;
    for (int column = 0; column < count; ++column) {
      auto cell = packed[column];
      if (column + 1 < count && mergeable(cell, packed[column + 1])) {
        ++cell;
        ++column;
        const uint32_t points = uint32_t{1} << cell;
        if (next.score > std::numeric_limits<uint32_t>::max() - points) return false;
        next.score += points;
      }
      merged[output++] = cell;
    }
    for (int column = 0; column < 4; ++column) next.cells[index(direction, row, column)] = merged[column];
  }
  if (std::memcmp(next.cells, game.cells, sizeof(game.cells)) == 0) return false;
  game = next;
  return true;
}

bool spawn(Game& game, uint32_t cellRandom, uint32_t valueRandom) {
  unsigned empty = 0;
  for (const auto cell : game.cells)
    if (!cell) ++empty;
  if (!empty) return false;
  unsigned selected = cellRandom % empty;
  for (auto& cell : game.cells) {
    if (cell) continue;
    if (selected-- == 0) {
      cell = valueRandom % 10 == 0 ? 2 : 1;
      return true;
    }
  }
  return false;
}

bool isStuck(const Game& game) {
  for (int i = 0; i < 16; ++i) {
    if (!game.cells[i]) return false;
    if (i % 4 < 3 && mergeable(game.cells[i], game.cells[i + 1])) return false;
    if (i < 12 && mergeable(game.cells[i], game.cells[i + 4])) return false;
  }
  return true;
}
}  // namespace papyrix::game2048
