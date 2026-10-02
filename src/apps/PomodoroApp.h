#pragma once

#include "../core/Types.h"

namespace papyrix {
struct Core;

namespace pomodoro_app {
void enter(Core& core);
bool update(Core& core);
void onButton(Core& core, Button btn);
bool render(Core& core);
void exit(Core& core);
void renderMenu(Core& core);
void onMenuButton(Core& core, Button btn);
}  // namespace pomodoro_app
}  // namespace papyrix
