#pragma once

#include "GoGame.h"

namespace gogame {

struct SearchControl {
  void* context = nullptr;
  bool (*abort)(void*) = nullptr;
  void (*yield)(void*) = nullptr;
  bool (*budgetExpired)(void*) = nullptr;
};

enum class SearchStatus : uint8_t { Ready, Cancelled, IOError, Invalid };

struct SearchResult {
  SearchStatus status = SearchStatus::Invalid;
  Move move = kNoMove;
  uint16_t simulations = 0;
};

class Search {
 public:
  SearchResult run(const Position& root, const History& history, uint32_t seed, const SearchControl& control);

 private:
  struct Node {
    uint16_t child = 0;
    uint16_t sibling = 0;
    uint16_t visits = 0;
    uint16_t wins = 0;
    uint8_t move = kNoMove;
    uint8_t next = 0;
    uint8_t offset = 0;
  };
  Node nodes_[512]{};
  Key history_[257]{};
};

}  // namespace gogame
