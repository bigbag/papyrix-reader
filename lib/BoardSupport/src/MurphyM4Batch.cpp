#include "MurphyM4Batch.h"

#include <TargetConfig.h>

#if PAPYRIX_TARGET_MURPHY_M4
#include <Preferences.h>

namespace papyrix::board {
namespace {
MurphyM4Batch activeBatch = MurphyM4Batch::Second;
constexpr const char* kNamespace = "papyrix_hw";
constexpr const char* kKey = "m4_batch";
}  // namespace

void loadMurphyM4Batch() {
  activeBatch = MurphyM4Batch::Second;
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) return;
  const uint8_t stored = preferences.getUChar(kKey, static_cast<uint8_t>(MurphyM4Batch::Second));
  preferences.end();
  if (stored == static_cast<uint8_t>(MurphyM4Batch::First)) activeBatch = MurphyM4Batch::First;
}

MurphyM4Batch murphyM4Batch() { return activeBatch; }

MurphyM4BatchSaveResult saveMurphyM4Batch(MurphyM4Batch value) {
  if (value != MurphyM4Batch::First && value != MurphyM4Batch::Second) return MurphyM4BatchSaveResult::Failed;
  if (value == activeBatch) return MurphyM4BatchSaveResult::Unchanged;
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) return MurphyM4BatchSaveResult::Failed;
  const bool saved = preferences.putUChar(kKey, static_cast<uint8_t>(value)) == 1;
  preferences.end();
  return saved ? MurphyM4BatchSaveResult::Saved : MurphyM4BatchSaveResult::Failed;
}

uint8_t murphyM4HalfTemperature(MurphyM4Batch batch) { return batch == MurphyM4Batch::First ? 0x3C : 0x50; }

uint16_t mapMurphyM4ShortAxis(uint16_t raw, MurphyM4Batch batch, uint16_t outMax) {
  const int32_t minimum = batch == MurphyM4Batch::First ? -52 : -47;
  const int32_t maximum = batch == MurphyM4Batch::First ? 553 : 514;
  if (raw >= maximum) return outMax;
  const int32_t mapped = (static_cast<int32_t>(raw) - minimum) * outMax / (maximum - minimum);
  return static_cast<uint16_t>(mapped);
}
}  // namespace papyrix::board
#endif
