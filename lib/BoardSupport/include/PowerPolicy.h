#pragma once

#include <cstdint>

#include "BoardProfile.h"

namespace papyrix::board {

void releaseDeepSleepHolds();
void prepareDeepSleepPins(const BoardProfile& profile, bool externalPower);
void prepareClockSleepPins(const BoardProfile& profile);
uint64_t powerButtonWakeMask(const BoardProfile& profile);
void shutdownRecoveryRails(const BoardProfile& profile);
void restoreRecoveryStorage(const BoardProfile& profile);

}  // namespace papyrix::board
