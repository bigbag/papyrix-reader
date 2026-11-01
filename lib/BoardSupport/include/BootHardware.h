#pragma once

#include "BoardProfile.h"

namespace papyrix::board {

enum class BootPurpose : uint8_t { Interactive, ClockTimer };

void earlyInit(const BoardProfile& profile, BootPurpose purpose);
void preparePanelProbe(const BoardProfile& profile);

}  // namespace papyrix::board
