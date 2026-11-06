#pragma once

#include "MiniApp.h"

namespace papyrix::pomodoro_app {
void enter(Core& core);
bool update(Core& core);
miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen);
miniapps::Frame render(Core& core, bool menuOpen);
void exit(Core& core);
miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target);
}  // namespace papyrix::pomodoro_app
