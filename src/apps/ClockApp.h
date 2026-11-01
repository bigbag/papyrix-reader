#pragma once

#include "ClockPower.h"
#include "MiniApp.h"

class GfxRenderer;

namespace papyrix {
namespace clock_app {

void enter(Core& core);
bool update(Core& core);
bool render(Core& core);
void exit(Core& core);
void renderMenu(Core& core);
void onMenuButton(Core& core, Button btn);
bool saveSettings(Core& core);
bool onMenuTap(Core& core, int16_t x, int16_t y);
ClockFrame captureFrame(Core& core, int64_t utcSeconds, bool interactive);
void renderFrame(GfxRenderer& renderer, const ClockFrame& frame);
enum class SyncResult : uint8_t { Success, Unavailable, Failed, Cancelled, TimeError };
SyncResult synchronize(Core& core, const ClockSettings& settings, bool interactive);
bool prepareStandby(Core& core, ResumeRecord& record);
bool standbyDue(Core& core, uint32_t nowMs, bool operationPending);
void resumeInteractive(Core& core, const ResumeRecord* record);

}  // namespace clock_app
}  // namespace papyrix
