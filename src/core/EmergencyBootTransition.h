#pragma once

#include "BootMode.h"

namespace papyrix {

void saveEmergencyUiTransition(ReturnTo returnTo);
bool consumeEmergencyUiTransition(ModeTransition& transition);

}  // namespace papyrix
