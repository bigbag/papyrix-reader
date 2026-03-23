#include "AppLauncherState.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include "../apps/MiniApp.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

#define TAG "APP_LAUNCHER"

namespace papyrix {

AppLauncherState::AppLauncherState(GfxRenderer& renderer)
    : renderer_(renderer), mode_(Mode::Menu), activeApp_(-1), needsRender_(true), goNetwork_(false), menuView_{} {}

AppLauncherState::~AppLauncherState() = default;

void AppLauncherState::enter(Core& core) {
  LOG_INF(TAG, "Entering");
  mode_ = Mode::Menu;
  activeApp_ = -1;
  needsRender_ = true;
  goNetwork_ = false;

  menuView_.appCount = APP_COUNT;
  menuView_.itemCount = APP_COUNT + ui::AppMenuView::EXTRA_COUNT;
  if (menuView_.selected >= menuView_.itemCount) {
    menuView_.selected = 0;
  }
  menuView_.needsRender = true;

  if (core.pendingAppId >= 0 && static_cast<unsigned>(core.pendingAppId) < APP_COUNT) {
    core.pendingSync = SyncMode::None;
    activeApp_ = core.pendingAppId;
    core.pendingAppId = -1;
    launchApp(core);
    return;
  }
}

void AppLauncherState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  if (activeApp_ >= 0) {
    stopApp(core);
  }
}

StateTransition AppLauncherState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    // Power long-press goes to sleep in all modes
    if (e.type == EventType::ButtonLongPress && e.button == Button::Power) {
      if (activeApp_ >= 0) {
        stopApp(core);
      }
      return StateTransition::to(StateId::Sleep);
    }

    if (e.type != EventType::ButtonPress && e.type != EventType::ButtonRepeat) {
      continue;
    }

    switch (mode_) {
      case Mode::Menu:
        switch (e.button) {
          case Button::Up:
            menuView_.moveUp();
            needsRender_ = true;
            break;
          case Button::Down:
            menuView_.moveDown();
            needsRender_ = true;
            break;
          case Button::Center:
            if (menuView_.selected >= ui::AppMenuView::EXTRA_COUNT) {
              int appIdx = menuView_.selected - ui::AppMenuView::EXTRA_COUNT;
              if (appIdx == APP_CLOCK) {
                core.pendingSync = SyncMode::NtpSync;
                core.pendingAppId = APP_CLOCK;
                goNetwork_ = true;
              } else {
                launchApp(core);
              }
            } else {
              int extraIdx = menuView_.selected;
              if (extraIdx == 0) {
                core.pendingSync = SyncMode::FileTransfer;
              } else {
                core.pendingSync = SyncMode::CalibreWireless;
              }
              goNetwork_ = true;
            }
            break;
          case Button::Back:
            return StateTransition::to(StateId::Home);
          default:
            break;
        }
        break;

      case Mode::App:
        switch (e.button) {
          case Button::Back:
            stopApp(core);
            break;
          case Button::Center:
            showOverlay();
            break;
          default:
            if (activeApp_ >= 0 && APPS[activeApp_].onButton) {
              APPS[activeApp_].onButton(core, e.button);
              needsRender_ = true;
            }
            break;
        }
        break;

      case Mode::Overlay:
        switch (e.button) {
          case Button::Back:
            hideOverlay();
            break;
          default:
            if (activeApp_ >= 0 && APPS[activeApp_].onMenuButton) {
              APPS[activeApp_].onMenuButton(core, e.button);
              needsRender_ = true;
            }
            break;
        }
        break;
    }
  }

  // Call app update for timer-based logic (once per frame)
  if ((mode_ == Mode::App || mode_ == Mode::Overlay) && activeApp_ >= 0) {
    // Prevent auto-sleep while an app is running
    core.input.resetIdleTimer();

    if (APPS[activeApp_].update && APPS[activeApp_].update(core)) {
      needsRender_ = true;
    }

    // Ensure full CPU speed for responsive display I/O when rendering
    if (needsRender_) {
      core.cpu.unthrottle();
    }
  }

  if (core.pendingSync == SyncMode::WifiSetup) {
    core.pendingSync = SyncMode::None;
    return StateTransition::to(StateId::Network);
  }

  if (goNetwork_) {
    goNetwork_ = false;
    return StateTransition::to(StateId::Network);
  }

  return StateTransition::stay(StateId::AppLauncher);
}

void AppLauncherState::render(Core& core) {
  if (!needsRender_ && !menuView_.needsRender) {
    return;
  }

  switch (mode_) {
    case Mode::Menu:
      ui::render(renderer_, THEME, menuView_, APPS);
      menuView_.needsRender = false;
      break;

    case Mode::App:
      if (activeApp_ >= 0 && APPS[activeApp_].render) {
        APPS[activeApp_].render(core);
        renderer_.displayBuffer();
      }
      break;

    case Mode::Overlay:
      if (activeApp_ >= 0) {
        if (APPS[activeApp_].render) {
          APPS[activeApp_].render(core);
        }
        if (APPS[activeApp_].renderMenu) {
          APPS[activeApp_].renderMenu(core);
        }
        const int btnY = renderer_.getScreenHeight() - 50;
        renderer_.clearArea(0, btnY, renderer_.getScreenWidth(), 50, THEME.backgroundColor);
        ui::ButtonBar buttons("Back", "Confirm", "<", ">");
        ui::buttonBar(renderer_, THEME, buttons);
        renderer_.displayBuffer();
      }
      break;
  }

  needsRender_ = false;
  core.display.markDirty();
}

void AppLauncherState::launchApp(Core& core) {
  if (APP_COUNT == 0) return;

  if (activeApp_ < 0) {
    activeApp_ = menuView_.selected - ui::AppMenuView::EXTRA_COUNT;
  }
  LOG_INF(TAG, "Launching app: %s", APPS[activeApp_].name);

  if (APPS[activeApp_].enter) {
    APPS[activeApp_].enter(core);
  }

  mode_ = Mode::App;
  menuView_.needsRender = false;
  needsRender_ = true;
}

void AppLauncherState::stopApp(Core& core) {
  if (activeApp_ >= 0) {
    LOG_INF(TAG, "Stopping app: %s", APPS[activeApp_].name);
    if (APPS[activeApp_].exit) {
      APPS[activeApp_].exit(core);
    }
    activeApp_ = -1;
  }

  mode_ = Mode::Menu;
  menuView_.needsRender = true;
  needsRender_ = true;
}

void AppLauncherState::showOverlay() {
  if (activeApp_ >= 0 && APPS[activeApp_].renderMenu) {
    mode_ = Mode::Overlay;
    needsRender_ = true;
  }
}

void AppLauncherState::hideOverlay() {
  mode_ = Mode::App;
  needsRender_ = true;
}

}  // namespace papyrix
