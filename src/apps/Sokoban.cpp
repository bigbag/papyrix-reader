#include "Sokoban.h"

#include <limits>

namespace papyrix::sokoban {
namespace {
constexpr uint32_t kMaxCounter = std::numeric_limits<uint32_t>::max();

bool adjacent(const Level& map, uint8_t from, uint8_t to) {
  const int dx = int(from % map.width) - int(to % map.width);
  const int dy = int(from / map.width) - int(to / map.width);
  return (dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1));
}

bool basic(const Game& game) {
  const Level* map = level(game.level);
  if (!map || game.undoCount > kUndoLimit || game.undoNext >= kUndoLimit || game.pushes > game.moves ||
      game.boxCount == 0 || game.boxCount > kMaxBoxes || terrain(game, game.player) == '#')
    return false;
  uint8_t expected = 0;
  for (unsigned i = 0; i < unsigned(map->width) * map->height; ++i)
    if (map->cells[i] == '$' || map->cells[i] == '*') ++expected;
  if (game.boxCount != expected) return false;
  for (uint8_t i = 0; i < game.boxCount; ++i) {
    if (terrain(game, game.boxes[i]) == '#' || game.boxes[i] == game.player) return false;
    for (uint8_t j = 0; j < i; ++j)
      if (game.boxes[i] == game.boxes[j]) return false;
  }
  return true;
}

bool reverse(Game& game, const Undo& entry) {
  const Level& map = *level(game.level);
  if (terrain(game, entry.player) == '#' || hasBox(game, entry.player) || !adjacent(map, entry.player, game.player) ||
      (entry.countedPush && entry.box == kNoBox))
    return false;
  if ((entry.countedMove && game.moves == 0) || (!entry.countedMove && game.moves != kMaxCounter) ||
      (entry.box != kNoBox &&
       ((entry.countedPush && game.pushes == 0) || (!entry.countedPush && game.pushes != kMaxCounter))))
    return false;
  if (entry.box != kNoBox) {
    if (entry.box >= game.boxCount) return false;
    const int destination = int(game.player) + (int(game.player) - int(entry.player));
    if (destination < 0 || destination >= int(map.width) * map.height ||
        !adjacent(map, game.player, static_cast<uint8_t>(destination)) || game.boxes[entry.box] != destination)
      return false;
  }
  const uint32_t moves = game.moves - unsigned(entry.countedMove);
  const uint32_t pushes = game.pushes - unsigned(entry.countedPush);
  if (pushes > moves) return false;
  bool solved = true;
  for (uint8_t i = 0; i < game.boxCount; ++i)
    if (terrain(game, i == entry.box ? game.player : game.boxes[i]) != '.') solved = false;
  if (solved) return false;
  if (entry.box != kNoBox) game.boxes[entry.box] = game.player;
  game.player = entry.player;
  game.moves = moves;
  game.pushes = pushes;
  return true;
}
}  // namespace

char terrain(const Game& game, uint8_t cell) {
  const Level* map = level(game.level);
  if (!map || cell >= unsigned(map->width) * map->height) return '#';
  const char c = map->cells[cell];
  return c == '#' ? '#' : (c == '.' || c == '*' || c == '+') ? '.' : ' ';
}

bool hasBox(const Game& game, uint8_t cell) {
  for (uint8_t i = 0; i < game.boxCount && i < kMaxBoxes; ++i)
    if (game.boxes[i] == cell) return true;
  return false;
}

bool start(Game& out, uint8_t index) {
  const Level* map = level(index);
  if (!map) return false;
  Game game;
  game.level = index;
  for (unsigned i = 0; i < unsigned(map->width) * map->height; ++i) {
    const char c = map->cells[i];
    if (c == '@' || c == '+') game.player = static_cast<uint8_t>(i);
    if (c == '$' || c == '*') game.boxes[game.boxCount++] = static_cast<uint8_t>(i);
  }
  out = game;
  return true;
}

bool isWon(const Game& game) {
  if (!game.boxCount || game.boxCount > kMaxBoxes) return false;
  for (uint8_t i = 0; i < game.boxCount; ++i)
    if (terrain(game, game.boxes[i]) != '.') return false;
  return true;
}

bool valid(const Game& game) {
  if (!basic(game)) return false;
  Game previous = game;
  for (unsigned i = 0; i < game.undoCount; ++i) {
    const unsigned slot = (unsigned(game.undoNext) + kUndoLimit - 1 - i) % kUndoLimit;
    if (!reverse(previous, game.history[slot])) return false;
  }
  return true;
}

bool move(Game& game, Direction direction) {
  if (!basic(game) || isWon(game)) return false;
  const Level& map = *level(game.level);
  int delta;
  switch (direction) {
    case Direction::Up:
      delta = -int(map.width);
      break;
    case Direction::Down:
      delta = map.width;
      break;
    case Direction::Left:
      delta = -1;
      break;
    case Direction::Right:
      delta = 1;
      break;
    default:
      return false;
  }
  const int next = int(game.player) + delta;
  if (next < 0 || next >= int(map.width) * map.height || !adjacent(map, game.player, static_cast<uint8_t>(next)) ||
      terrain(game, next) == '#')
    return false;
  uint8_t box = kNoBox;
  for (uint8_t i = 0; i < game.boxCount; ++i)
    if (game.boxes[i] == next) {
      box = i;
      break;
    }
  if (box != kNoBox) {
    const int target = next + delta;
    if (target < 0 || target >= int(map.width) * map.height || !adjacent(map, next, static_cast<uint8_t>(target)) ||
        terrain(game, target) == '#' || hasBox(game, target))
      return false;
    game.boxes[box] = static_cast<uint8_t>(target);
  }
  Undo entry;
  entry.player = game.player;
  entry.box = box;
  entry.countedMove = game.moves != kMaxCounter;
  entry.countedPush = box != kNoBox && game.pushes != kMaxCounter;
  if (entry.countedMove) ++game.moves;
  if (entry.countedPush) ++game.pushes;
  game.history[game.undoNext] = entry;
  game.undoNext = (unsigned(game.undoNext) + 1) % kUndoLimit;
  if (game.undoCount < kUndoLimit) ++game.undoCount;
  game.player = static_cast<uint8_t>(next);
  return true;
}

bool undo(Game& game) {
  if (!basic(game) || !game.undoCount) return false;
  const uint8_t slot = (unsigned(game.undoNext) + kUndoLimit - 1) % kUndoLimit;
  if (!reverse(game, game.history[slot])) return false;
  game.undoNext = slot;
  --game.undoCount;
  return true;
}
}  // namespace papyrix::sokoban
