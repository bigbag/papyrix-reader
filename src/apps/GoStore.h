#pragma once

#include <GoGame.h>
#include <SdFat.h>

#include <cstdint>
#include <functional>

#include "../hal/Storage.h"

namespace papyrix::go_store {

using AbortCallback = std::function<bool()>;

struct Game {
  gogame::Position position;
  uint32_t seed = 0;
};

enum class LoadResult { Loaded, Missing, Invalid, IOError, Cancelled };

class HistoryReader {
 public:
  HistoryReader() = default;
  ~HistoryReader();
  HistoryReader(const HistoryReader&) = delete;
  HistoryReader& operator=(const HistoryReader&) = delete;

  bool open(hal::Storage& storage, const Game& game, const AbortCallback& abort = {});
  gogame::History history();
  bool close();

 private:
  static gogame::HistoryResult contains(void* context, const gogame::Key& key);
  friend gogame::MoveResult play(hal::Storage&, Game&, gogame::Move, const AbortCallback&);
  FsFile file_;
  uint8_t filter_[128] = {};
  uint32_t count_ = 0;
  AbortCallback abort_;
  LoadResult status_ = LoadResult::Missing;
};

LoadResult load(hal::Storage& storage, Game& game, const AbortCallback& abort = {});
bool startNew(hal::Storage& storage, Game& game, uint32_t seed);
gogame::MoveResult play(hal::Storage& storage, Game& game, gogame::Move move, const AbortCallback& abort = {});
bool resign(hal::Storage& storage, Game& game, gogame::Color color);

}  // namespace papyrix::go_store
