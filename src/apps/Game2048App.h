#pragma once

#include "MiniApp.h"

namespace papyrix::game2048_app {
void enter(Core& core);
bool update(Core& core);
miniapps::Frame render(Core& core, bool menuOpen);
void exit(Core& core);
miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen);
miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target);
}  // namespace papyrix::game2048_app
