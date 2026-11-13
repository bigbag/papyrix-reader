#pragma once

#include "LightsOut.h"

namespace papyrix::hal {
class Storage;
}
namespace papyrix::lightsout {
enum class LoadResult : uint8_t { Missing, Loaded, Invalid, IOError };
LoadResult load(hal::Storage& storage, Game& out);
bool save(hal::Storage& storage, const Game& game);
bool clear(hal::Storage& storage);
}  // namespace papyrix::lightsout
