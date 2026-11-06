#pragma once

#include "MiniApp.h"

namespace papyrix::printer_app {
void enter(Core& core);
bool update(Core& core);
miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen);
miniapps::Frame render(Core& core, bool menuOpen);
void exit(Core& core);
}  // namespace papyrix::printer_app
