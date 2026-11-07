#pragma once

#include "MiniApp.h"

namespace papyrix::go_app {
void enter(Core& core);
bool update(Core& core);
miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen);
miniapps::Frame render(Core& core, bool menuOpen);
miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target);
void exit(Core& core);
}  // namespace papyrix::go_app
