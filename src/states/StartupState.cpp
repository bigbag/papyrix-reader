#include "StartupState.h"

#include <Arduino.h>
#include <Logging.h>

#include "../core/Core.h"

#define TAG "STARTUP"

namespace papyrix {

void StartupState::enter(Core& core) {
  LOG_INF(TAG, "Entering");
  initialized_ = false;
}

void StartupState::exit(Core& core) { LOG_INF(TAG, "Exiting"); }

StateTransition StartupState::update(Core& core) {
  if (!initialized_) {
    initialized_ = true;
    // First frame - just entered
    // In future: show boot animation
    return StateTransition::stay(StateId::Startup);
  }

  // Transition to FileList (or LegacyState during migration)
  // For now, stay in startup - main.cpp will handle legacy activities
  return StateTransition::stay(StateId::Startup);
}

}  // namespace papyrix
