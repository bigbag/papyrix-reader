#pragma once

#include "ClockPower.h"

namespace papyrix {
struct Core;
namespace clock_app {

bool readResume(ResumeRecord& out);
void clearResume();
StandbyResult enterStandby(Core& core);
StandbyResult runTimerWake(Core& core, ResumeRecord& record);
bool powerWakeRequested();
const char* standbyErrorMessage(StandbyResult result);

}  // namespace clock_app
}  // namespace papyrix
