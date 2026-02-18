#include "SyncState.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "../core/Core.h"
#include "ThemeManager.h"

namespace papyrix {

SyncState::SyncState(GfxRenderer& renderer)
    : renderer_(renderer), needsRender_(true), goHome_(false), goNetwork_(false) {}

SyncState::~SyncState() = default;

void SyncState::enter(Core& core) {
  Serial.println("[SYNC] Entering");
  menuView_.selected = 0;
  menuView_.needsRender = true;
  needsRender_ = true;
  goHome_ = false;
  goNetwork_ = false;
}

void SyncState::exit(Core& core) { Serial.println("[SYNC] Exiting"); }

StateTransition SyncState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    switch (e.type) {
      case EventType::ButtonPress:
        switch (e.button) {
          case Button::Up:
            menuView_.moveUp();
            needsRender_ = true;
            break;

          case Button::Down:
            menuView_.moveDown();
            needsRender_ = true;
            break;

          case Button::Back:
            goHome_ = true;
            break;

          case Button::Center:
            if (menuView_.buttons.isActive(1)) {
              core.pendingSync = static_cast<SyncMode>(menuView_.selected + 1);
              goNetwork_ = true;
            }
            break;

          case Button::Right:
          case Button::Power:
            break;
        }
        break;

      default:
        break;
    }
  }

  if (goNetwork_) {
    goNetwork_ = false;
    return StateTransition::to(StateId::Network);
  }

  if (goHome_) {
    goHome_ = false;
    return StateTransition::to(StateId::Home);
  }

  return StateTransition::stay(StateId::Sync);
}

void SyncState::render(Core& core) {
  if (!needsRender_ && !menuView_.needsRender) {
    return;
  }

  ui::render(renderer_, THEME, menuView_);
  menuView_.needsRender = false;
  needsRender_ = false;
  core.display.markDirty();
}

}  // namespace papyrix
