#include "GoGame.h"

namespace gogame {
namespace {

int neighbors(int p, uint8_t out[4]) {
  int n = 0;
  if (p >= kSide) out[n++] = p - kSide;
  if (p < kPoints - kSide) out[n++] = p + kSide;
  if (p % kSide) out[n++] = p - 1;
  if (p % kSide != kSide - 1) out[n++] = p + 1;
  return n;
}

int group(const Position& p, int start, uint8_t stones[kPoints], bool& liberty) {
  bool seen[kPoints] = {};
  int count = 1;
  stones[0] = start;
  seen[start] = true;
  liberty = false;
  for (int i = 0; i < count; ++i) {
    uint8_t adjacent[4];
    const int n = neighbors(stones[i], adjacent);
    for (int j = 0; j < n; ++j) {
      const int q = adjacent[j];
      if (p.cells[q] == Color::Empty) {
        liberty = true;
        return count;
      } else if (p.cells[q] == p.cells[start] && !seen[q]) {
        seen[q] = true;
        stones[count++] = q;
      }
    }
  }
  return count;
}

}  // namespace

Color other(Color color) {
  if (color == Color::Black) return Color::White;
  if (color == Color::White) return Color::Black;
  return Color::Empty;
}

Key key(const Position& position) {
  Key result;
  for (int i = 0; i < kPoints / 4; ++i) {
    const auto* cells = position.cells.data() + i * 4;
    result[i] = static_cast<uint8_t>(cells[0]) | (static_cast<uint8_t>(cells[1]) << 2) |
                (static_cast<uint8_t>(cells[2]) << 4) | (static_cast<uint8_t>(cells[3]) << 6);
  }
  result.back() = static_cast<uint8_t>(position.cells.back());
  return result;
}

MoveResult play(const Position& before, Move move, Position& after, const History& history) {
  if (before.outcome != Outcome::Playing) return MoveResult::GameOver;
  if (before.toMove != Color::Black && before.toMove != Color::White) return MoveResult::Invalid;
  if (move > kPass) return MoveResult::Invalid;
  Position candidate = before;
  if (move == kPass) {
    candidate.passes++;
    if (candidate.passes >= 2) candidate.outcome = Outcome::Scored;
  } else {
    if (before.cells[move] != Color::Empty) return MoveResult::Occupied;
    if (!history.contains) return MoveResult::IOError;
    candidate.cells[move] = before.toMove;
    uint8_t adjacent[4], stones[kPoints];
    const int n = neighbors(move, adjacent);
    for (int i = 0; i < n; ++i) {
      const int q = adjacent[i];
      if (candidate.cells[q] != other(before.toMove)) continue;
      bool liberty;
      const int count = group(candidate, q, stones, liberty);
      if (!liberty)
        for (int j = 0; j < count; ++j) candidate.cells[stones[j]] = Color::Empty;
    }
    bool liberty;
    const int count = group(candidate, move, stones, liberty);
    if (!liberty)
      for (int i = 0; i < count; ++i) candidate.cells[stones[i]] = Color::Empty;
    const Key coloring = key(candidate);
    if (coloring == Key{} || candidate.cells == before.cells) return MoveResult::Repetition;
    switch (history.contains(history.context, coloring)) {
      case HistoryResult::Present:
        return MoveResult::Repetition;
      case HistoryResult::Cancelled:
        return MoveResult::Cancelled;
      case HistoryResult::IOError:
        return MoveResult::IOError;
      case HistoryResult::Absent:
        break;
    }
    candidate.passes = 0;
  }
  candidate.toMove = other(before.toMove);
  candidate.lastMove = move;
  ++candidate.turns;
  after = candidate;
  return MoveResult::Ok;
}

bool resign(Position& position, Color color) {
  if (position.outcome != Outcome::Playing || (color != Color::Black && color != Color::White)) return false;
  position.outcome = color == Color::Black ? Outcome::BlackResigned : Outcome::WhiteResigned;
  return true;
}

Score score(const Position& position) {
  Score result;
  bool seen[kPoints] = {};
  for (int p = 0; p < kPoints; ++p) {
    if (position.cells[p] == Color::Black) {
      result.blackHalf += 2;
      continue;
    }
    if (position.cells[p] == Color::White) {
      result.whiteHalf += 2;
      continue;
    }
    if (seen[p]) continue;
    uint8_t queue[kPoints];
    int count = 1;
    queue[0] = p;
    seen[p] = true;
    bool black = false, white = false;
    for (int i = 0; i < count; ++i) {
      uint8_t adjacent[4];
      const int n = neighbors(queue[i], adjacent);
      for (int j = 0; j < n; ++j) {
        int q = adjacent[j];
        if (position.cells[q] == Color::Black)
          black = true;
        else if (position.cells[q] == Color::White)
          white = true;
        else if (!seen[q]) {
          seen[q] = true;
          queue[count++] = q;
        }
      }
    }
    if (black && !white) result.blackHalf += 2 * count;
    if (white && !black) result.whiteHalf += 2 * count;
  }
  return result;
}

Color winner(const Position& position) {
  if (position.outcome == Outcome::BlackResigned) return Color::White;
  if (position.outcome == Outcome::WhiteResigned) return Color::Black;
  if (position.outcome != Outcome::Scored) return Color::Empty;
  const Score result = score(position);
  if (result.blackHalf == result.whiteHalf) return Color::Empty;
  return result.blackHalf > result.whiteHalf ? Color::Black : Color::White;
}

}  // namespace gogame
