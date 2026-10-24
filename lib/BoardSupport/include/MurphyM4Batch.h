#pragma once

#include <cstdint>

namespace papyrix::board {
enum class MurphyM4Batch : uint8_t { First = 1, Second = 2 };
enum class MurphyM4BatchSaveResult : uint8_t { Unchanged, Saved, Failed };
void loadMurphyM4Batch();
MurphyM4Batch murphyM4Batch();
MurphyM4BatchSaveResult saveMurphyM4Batch(MurphyM4Batch value);
uint8_t murphyM4HalfTemperature(MurphyM4Batch batch);
uint16_t mapMurphyM4ShortAxis(uint16_t raw, MurphyM4Batch batch, uint16_t outMax);
}  // namespace papyrix::board
