#pragma once

#include "../core/EventQueue.h"

namespace papyrix {
struct Core;
}
namespace papyrix::game2048_app {
enum class Action : uint8_t { None, Redraw, OpenMenu, CloseMenu, Exit, RetryExit, CancelExit, DiscardExit };
void enter(Core& core);
bool update(Core& core);
bool render(Core& core);
void renderMenu(Core& core);
void exit(Core& core);
Action handleEvent(Core& core, const Event& event, bool menuOpen);
bool prepareExit(Core& core);
void cancelExitPrompt();
void cancelGameInput(Core& core);
}  // namespace papyrix::game2048_app
