#pragma once

#include <array>
#include <cstdint>

namespace papyrix::sokoban {
constexpr uint8_t kMaxSide = 12;
constexpr uint8_t kMaxBoxes = 8;
constexpr uint8_t kLevelCount = 12;
// shortcut: Keep 128 undo moves; increase this bound if longer undo is required.
constexpr uint8_t kUndoLimit = 128;
constexpr uint8_t kNoBox = 255;

enum class Direction : uint8_t { Up, Down, Left, Right };
struct Level {
  uint8_t width;
  uint8_t height;
  const char* cells;
};
struct Undo {
  uint8_t player = 0;
  uint8_t box = kNoBox;
  bool countedMove = false;
  bool countedPush = false;
};
struct Game {
  uint8_t level = 0;
  uint8_t player = 0;
  uint8_t boxCount = 0;
  std::array<uint8_t, kMaxBoxes> boxes{};
  uint32_t moves = 0;
  uint32_t pushes = 0;
  std::array<Undo, kUndoLimit> history{};
  uint8_t undoCount = 0;
  uint8_t undoNext = 0;
};

const Level* level(uint8_t index);
char terrain(const Game& game, uint8_t cell);
bool hasBox(const Game& game, uint8_t cell);
bool start(Game& out, uint8_t index);
bool valid(const Game& game);
bool isWon(const Game& game);
bool move(Game& game, Direction direction);
bool undo(Game& game);
}  // namespace papyrix::sokoban
