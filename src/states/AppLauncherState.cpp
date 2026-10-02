#include "AppLauncherState.h"

#include <GfxRenderer.h>
#include <I18n.h>
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
    activeApp_ = core.pendingAppId;
    core.pendingAppId = -1;
    launchApp(core);
    core.pendingSync = SyncMode::None;
    return;
  }
}

void AppLauncherState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  if (activeApp_ >= 0) {
    stopApp(core);
  }
}

void AppLauncherState::activateMenuItem(Core& core) {
  if (menuView_.selected >= ui::AppMenuView::EXTRA_COUNT) {
    const int appIdx = menuView_.selected - ui::AppMenuView::EXTRA_COUNT;
    std::tm timeinfo{};
    if (appIdx == APP_CLOCK && !core.clock.localTime(timeinfo)) {
      core.pendingSync = SyncMode::NtpSync;
      core.pendingAppId = APP_CLOCK;
      goNetwork_ = true;
    } else {
      launchApp(core);
    }
    return;
  }
  core.pendingSync = menuView_.selected == 0 ? SyncMode::FileTransfer : SyncMode::CalibreWireless;
  goNetwork_ = true;
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

    if (e.type == EventType::Tap && mode_ == Mode::Menu) {
      const auto hit = menuView_.hitTest({e.touch.x, e.touch.y}, renderer_.getScreenWidth(),
                                         renderer_.getScreenHeight(), THEME.itemHeight + THEME.itemSpacing,
                                         core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (hit.type == ui::AppMenuView::Hit::Type::Entry) {
        menuView_.selected = static_cast<int8_t>(hit.index);
        menuView_.needsRender = true;
        activateMenuItem(core);
      } else if (hit.type == ui::AppMenuView::Hit::Type::Open) {
        activateMenuItem(core);
      } else if (hit.type == ui::AppMenuView::Hit::Type::Back) {
        // A pending directory request routes to the file manager after the loop
        if (!core.pendingDirectory[0]) return StateTransition::to(StateId::Home);
      }
      continue;
    }

    if (e.type == EventType::Tap) {
      const int action = ui::touch::semanticButtonBarIndex({e.touch.x, e.touch.y}, renderer_.getScreenWidth(),
                                                           renderer_.getScreenHeight(),
                                                           core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (action < 0) continue;
      static constexpr Button buttons[] = {Button::Back, Button::Center, Button::Left, Button::Right};
      e = Event::buttonPress(buttons[action]);
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
            activateMenuItem(core);
            break;
          case Button::Back:
            if (!core.pendingDirectory[0]) return StateTransition::to(StateId::Home);
            break;
          default:
            break;
        }
        break;

      case Mode::App:
        if (e.type == EventType::ButtonRepeat) break;
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
        if (e.type == EventType::ButtonRepeat) break;
        if (activeApp_ == APP_CLOCK && e.button == Button::Center) break;
        core.cpu.unthrottle();
        switch (e.button) {
          case Button::Back:
            if (activeApp_ >= 0 && APPS[activeApp_].onMenuButton) {
              APPS[activeApp_].onMenuButton(core, e.button);
            }
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

    if (mode_ == Mode::App && APPS[activeApp_].update && APPS[activeApp_].update(core)) {
      needsRender_ = true;
    }

    // Ensure full CPU speed for responsive display I/O when rendering
    if (needsRender_) {
      core.cpu.unthrottle();
    }
  }

  if (core.pendingDirectory[0]) {
    return StateTransition::to(StateId::FileList);
  }

  if (core.pendingSync == SyncMode::WifiSetup) {
    core.pendingSync = SyncMode::None;
    return StateTransition::to(StateId::Network);
  }

  if (core.pendingSync == SyncMode::NtpSync) {
    return StateTransition::to(StateId::Network);
  }

  if (core.pendingSync == SyncMode::PrinterSetup) {
    return StateTransition::to(StateId::Network);
  }

  if (core.pendingSync == SyncMode::LocalsendSetup) {
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
        if (!APPS[activeApp_].render(core)) {
          renderer_.displayBuffer(papyrix::hal::Display::FAST_REFRESH,
                                  activeApp_ == APP_CLOCK || activeApp_ == APP_POMODORO);
        }
      }
      break;

    case Mode::Overlay:
      if (activeApp_ >= 0) {
        renderer_.clearScreen(THEME.backgroundColor);
        if (APPS[activeApp_].renderMenu) {
          APPS[activeApp_].renderMenu(core);
        }
        if (activeApp_ != APP_CLOCK) {
          const int btnY = renderer_.getScreenHeight() - 50;
          renderer_.clearArea(0, btnY, renderer_.getScreenWidth(), 50, THEME.backgroundColor);
          ui::ButtonBar buttons(tr(BACK), tr(CONFIRM), "<", ">");
          ui::buttonBar(renderer_, THEME, buttons);
        }
        renderer_.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
      }
      break;
  }

  needsRender_ = false;
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
