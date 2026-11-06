#pragma once

#include "../core/Types.h"
#include "MiniApp.h"

namespace papyrix {

bool miniAppButton(const Core& core, const Event& event, Button& button);
miniapps::Action handleAppButtons(Core& core, const Event& event, bool menuOpen, void (*onButton)(Core&, Button),
                                  void (*onMenuButton)(Core&, Button));

}  // namespace papyrix
