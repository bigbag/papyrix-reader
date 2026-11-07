#pragma once

#include <array>
#include <cstdint>

namespace gogame {

constexpr int kSide = 9;
constexpr int kPoints = kSide * kSide;
using Move = uint8_t;
constexpr Move kPass = kPoints;
constexpr Move kNoMove = 255;
constexpr int16_t kKomiHalf = 15;
using Key = std::array<uint8_t, 21>;

enum class Color : uint8_t { Empty, Black, White };
enum class Outcome : uint8_t { Playing, Scored, BlackResigned, WhiteResigned };

struct Position {
  std::array<Color, kPoints> cells{};
  Color toMove = Color::Black;
  uint8_t passes = 0;
  Move lastMove = kNoMove;
  Outcome outcome = Outcome::Playing;
  uint32_t turns = 0;
};

enum class HistoryResult : uint8_t { Absent, Present, Cancelled, IOError };

struct History {
  void* context = nullptr;
  HistoryResult (*contains)(void*, const Key&) = nullptr;
};

enum class MoveResult : uint8_t { Ok, Occupied, Repetition, GameOver, Invalid, Cancelled, IOError };

struct Score {
  int16_t blackHalf = 0;
  int16_t whiteHalf = kKomiHalf;
};

Color other(Color color);
Key key(const Position& position);
MoveResult play(const Position& before, Move move, Position& after, const History& history);
bool resign(Position& position, Color color);
Score score(const Position& position);
Color winner(const Position& position);

}  // namespace gogame
