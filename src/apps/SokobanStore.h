#pragma once

#include "Sokoban.h"

namespace papyrix::hal {
class Storage;
}

namespace papyrix::sokoban {
enum class LoadResult : uint8_t { Loaded, Missing, Invalid, IOError };
LoadResult load(hal::Storage& storage, Game& out);
bool save(hal::Storage& storage, const Game& game);
bool clear(hal::Storage& storage);
}  // namespace papyrix::sokoban
