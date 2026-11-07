#include "GoSearch.h"

#include <cmath>

namespace gogame {
namespace {

struct SimHistory {
  const History* base;
  const SearchControl* control;
  Key* keys;
  uint16_t count;
};

bool cancelled(const SearchControl& control) { return control.abort && control.abort(control.context); }

HistoryResult contains(void* context, const Key& value) {
  auto& h = *static_cast<SimHistory*>(context);
  if (cancelled(*h.control)) return HistoryResult::Cancelled;
  for (uint16_t i = 0; i < h.count; ++i)
    if (h.keys[i] == value) return HistoryResult::Present;
  return h.base->contains(h.base->context, value);
}

uint32_t random(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

SearchStatus errorStatus(MoveResult result) {
  if (result == MoveResult::Cancelled) return SearchStatus::Cancelled;
  if (result == MoveResult::IOError) return SearchStatus::IOError;
  return SearchStatus::Invalid;
}

bool fatal(MoveResult result) {
  return result == MoveResult::Cancelled || result == MoveResult::IOError || result == MoveResult::Invalid;
}

int preference(const Position& before, const Position& after, Move move) {
  if (move == kPass) return -1;
  const Color opponent = other(before.toMove);
  int captured = 0;
  for (int i = 0; i < kPoints; ++i)
    if (before.cells[i] == opponent && after.cells[i] == Color::Empty) ++captured;
  int liberties = 0;
  if (move >= kSide && after.cells[move - kSide] == Color::Empty) ++liberties;
  if (move < kPoints - kSide && after.cells[move + kSide] == Color::Empty) ++liberties;
  if (move % kSide && after.cells[move - 1] == Color::Empty) ++liberties;
  if (move % kSide != kSide - 1 && after.cells[move + 1] == Color::Empty) ++liberties;
  return captured * 8 + liberties - (after.cells[move] == Color::Empty ? 8 : 0);
}

}  // namespace

SearchResult Search::run(const Position& root, const History& history, uint32_t seed, const SearchControl& control) {
  SearchResult result;
  if (!history.contains || root.outcome != Outcome::Playing ||
      (root.toMove != Color::Black && root.toMove != Color::White))
    return result;
  if (cancelled(control)) {
    result.status = SearchStatus::Cancelled;
    return result;
  }
  if (root.passes == 1) {
    Position finished;
    if (play(root, kPass, finished, history) == MoveResult::Ok && winner(finished) == root.toMove) {
      if (cancelled(control)) {
        result.status = SearchStatus::Cancelled;
        return result;
      }
      result.status = SearchStatus::Ready;
      result.move = kPass;
      return result;
    }
  }
  if (!seed) seed = 0x9e3779b9u;
  nodes_[0] = Node{};
  nodes_[0].offset = random(seed) % (kPoints + 1);
  uint16_t used = 1;
  for (int simulation = 0; simulation < 512; ++simulation) {
    if (cancelled(control)) {
      result.status = SearchStatus::Cancelled;
      return result;
    }
    if (result.simulations && control.budgetExpired && control.budgetExpired(control.context)) break;
    Position position = root;
    history_[0] = key(root);
    SimHistory simulated{&history, &control, history_, 1};
    History combined{&simulated, contains};
    uint16_t path[257];
    uint16_t depth = 0, node = 0, plies = 0;
    path[0] = 0;
    while (position.outcome == Outcome::Playing && plies < 256) {
      if (cancelled(control)) {
        result.status = SearchStatus::Cancelled;
        return result;
      }
      Node& current = nodes_[node];
      if (current.next < kPoints + 1 && used < 512) {
        bool expanded = false;
        while (current.next < kPoints + 1) {
          if (cancelled(control)) {
            result.status = SearchStatus::Cancelled;
            return result;
          }
          const Move move = (current.offset + current.next++) % (kPoints + 1);
          Position next;
          const MoveResult played = play(position, move, next, combined);
          if (fatal(played)) {
            result.status = errorStatus(played);
            return result;
          }
          if (played != MoveResult::Ok) continue;
          const uint16_t child = used++;
          nodes_[child] = Node{};
          nodes_[child].move = move;
          nodes_[child].offset = random(seed) % (kPoints + 1);
          nodes_[child].sibling = current.child;
          current.child = child;
          position = next;
          if (move != kPass) history_[simulated.count++] = key(position);
          node = child;
          path[++depth] = child;
          ++plies;
          expanded = true;
          break;
        }
        if (expanded) break;
      }
      if (!current.child) break;
      uint16_t best = current.child;
      double bestValue = -1.0;
      const double logVisits = std::log(static_cast<double>(current.visits) + 1.0);
      for (uint16_t child = current.child; child; child = nodes_[child].sibling) {
        const Node& candidate = nodes_[child];
        const double value = candidate.visits == 0 ? 1e30
                                                   : static_cast<double>(candidate.wins) / (2.0 * candidate.visits) +
                                                         1.41421356237 * std::sqrt(logVisits / candidate.visits);
        if (value > bestValue) {
          bestValue = value;
          best = child;
        }
      }
      Position next;
      const Move move = nodes_[best].move;
      const MoveResult played = play(position, move, next, combined);
      if (played != MoveResult::Ok) {
        result.status = errorStatus(played);
        return result;
      }
      position = next;
      if (move != kPass) history_[simulated.count++] = key(position);
      node = best;
      path[++depth] = best;
      ++plies;
    }
    while (position.outcome == Outcome::Playing && plies < 256) {
      if (cancelled(control)) {
        result.status = SearchStatus::Cancelled;
        return result;
      }
      Position chosen;
      Move chosenMove = kPass;
      int best = -1000, legal = 0;
      const int start = random(seed) % kPoints;
      for (int i = 0; i < kPoints && legal < 6; ++i) {
        if (cancelled(control)) {
          result.status = SearchStatus::Cancelled;
          return result;
        }
        const Move move = (start + i) % kPoints;
        if (position.cells[move] != Color::Empty) continue;
        Position next;
        const MoveResult played = play(position, move, next, combined);
        if (fatal(played)) {
          result.status = errorStatus(played);
          return result;
        }
        if (played != MoveResult::Ok) continue;
        ++legal;
        const int value = preference(position, next, move);
        if (value > best) {
          best = value;
          chosen = next;
          chosenMove = move;
        }
      }
      if (!legal || (random(seed) % 32 == 0)) {
        chosenMove = kPass;
        const MoveResult played = play(position, kPass, chosen, combined);
        if (played != MoveResult::Ok) {
          result.status = errorStatus(played);
          return result;
        }
      }
      position = chosen;
      if (chosenMove != kPass) history_[simulated.count++] = key(position);
      ++plies;
    }
    const Score estimate = score(position);
    const Color winning = estimate.blackHalf > estimate.whiteHalf ? Color::Black : Color::White;
    Color player = root.toMove;
    for (uint16_t i = 0; i <= depth; ++i) {
      Node& visited = nodes_[path[i]];
      ++visited.visits;
      if (i != 0) {
        if (estimate.blackHalf == estimate.whiteHalf)
          ++visited.wins;
        else if (player == winning)
          visited.wins += 2;
        player = other(player);
      }
    }
    ++result.simulations;
    if (control.yield) control.yield(control.context);
  }
  if (cancelled(control)) {
    result.status = SearchStatus::Cancelled;
    return result;
  }
  uint16_t best = nodes_[0].child;
  if (!best) return result;
  for (uint16_t child = nodes_[best].sibling; child; child = nodes_[child].sibling)
    if (nodes_[child].visits > nodes_[best].visits ||
        (nodes_[child].visits == nodes_[best].visits && nodes_[child].wins > nodes_[best].wins))
      best = child;
  Position validated;
  const Move move = nodes_[best].move;
  const MoveResult played = play(root, move, validated, history);
  if (played != MoveResult::Ok) {
    result.status = errorStatus(played);
    return result;
  }
  result.status = SearchStatus::Ready;
  result.move = move;
  return result;
}

}  // namespace gogame
