#include "AppLauncherState.h"

#include <BootHardware.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <I18n.h>
#include <InputManager.h>
#include <Logging.h>
#include <PowerPolicy.h>

#include "../apps/ClockApp.h"
#include "../apps/ClockStandby.h"
#include "../apps/MiniApp.h"
#include "../core/Core.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

#define TAG "APP_LAUNCHER"

extern InputManager inputManager;

namespace papyrix {

AppLauncherState::AppLauncherState(GfxRenderer& renderer) : renderer_(renderer), goNetwork_(false), menuView_{} {}

AppLauncherState::~AppLauncherState() = default;

void AppLauncherState::enter(Core& core) {
  LOG_INF(TAG, "Entering");
  app_.stop(core);
  goNetwork_ = false;

  menuView_.appCount = APP_COUNT;
  menuView_.itemCount = APP_COUNT + ui::AppMenuView::EXTRA_COUNT;
  if (menuView_.selected >= menuView_.itemCount) {
    menuView_.selected = 0;
  }
  menuView_.needsRender = true;

  if (core.pendingAppId >= 0 && static_cast<unsigned>(core.pendingAppId) < APP_COUNT) {
    const auto appId = core.pendingAppId;
    core.pendingAppId = -1;
    launchApp(core, appId);
    core.pendingSync = SyncMode::None;
    return;
  }
}

void AppLauncherState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");
  app_.stop(core);
}

void AppLauncherState::resumeClock(Core& core, const clock_app::ResumeRecord& record) {
  app_.restore(APPS[APP_CLOCK]);
  menuView_.needsRender = false;
  clock_app::resumeInteractive(core, &record);
}

void AppLauncherState::activateMenuItem(Core& core) {
  if (menuView_.selected >= ui::AppMenuView::EXTRA_COUNT) {
    const int appIdx = ui::appIdForMenuItem(menuView_.selected);
    std::tm timeinfo{};
    if (appIdx == APP_CLOCK && !core.clock.localTime(timeinfo)) {
      core.pendingSync = SyncMode::NtpSync;
      core.pendingAppId = APP_CLOCK;
      goNetwork_ = true;
    } else {
      launchApp(core, appIdx);
    }
    return;
  }
  core.pendingSync = menuView_.selected == 0 ? SyncMode::FileTransfer : SyncMode::CalibreWireless;
  goNetwork_ = true;
}

StateTransition AppLauncherState::update(Core& core) {
  Event e;
  while (core.events.pop(e)) {
    if (e.type == EventType::ButtonLongPress && e.button == Button::Power) {
      if (!requestSleep(core)) continue;
      return StateTransition::to(StateId::Sleep);
    }

    if (app_.active()) {
      core.cpu.unthrottle();
      const bool hadPendingExit = app_.pendingExit() != miniapps::ExitTarget::None;
      const auto destination = app_.handleEvent(core, e);
      if (destination == miniapps::ExitTarget::Sleep) return StateTransition::to(StateId::Sleep);
      if (!app_.active()) {
        menuView_.needsRender = true;
      } else if (hadPendingExit && app_.pendingExit() == miniapps::ExitTarget::None) {
        core.input.resetIdleTimer();
      }
      continue;
    }

    if (e.type == EventType::Tap) {
      const auto hit = menuView_.hitTest({e.touch.x, e.touch.y}, renderer_.getScreenWidth(),
                                         renderer_.getScreenHeight(), THEME.itemHeight + THEME.itemSpacing,
                                         core.settings.frontButtonLayout == Settings::FrontLRBC);
      if (hit.type == ui::AppMenuView::Hit::Type::Entry) {
        menuView_.selected = static_cast<int8_t>(hit.index);
        menuView_.needsRender = true;
        activateMenuItem(core);
      } else if (hit.type == ui::AppMenuView::Hit::Type::Open) {
        activateMenuItem(core);
      } else if (hit.type == ui::AppMenuView::Hit::Type::Back && !core.pendingDirectory[0]) {
        return StateTransition::to(StateId::Home);
      }
      continue;
    }

    if (e.type != EventType::ButtonPress && e.type != EventType::ButtonRepeat) continue;
    switch (e.button) {
      case Button::Up:
        menuView_.moveUp();
        break;
      case Button::Down:
        menuView_.moveDown();
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
  }

  if (app_.active()) {
    if (app_.keepAwake()) core.input.resetIdleTimer();
    app_.update(core);
    if (app_.needsRender()) {
      core.cpu.unthrottle();
    } else if (app_.throttleInMenu()) {
      core.cpu.throttle();
    }
  }

  if ((core.pendingDirectory[0] || core.pendingSync != SyncMode::None || goNetwork_) &&
      !app_.prepareExit(core, miniapps::ExitTarget::Navigation)) {
    core.pendingDirectory[0] = '\0';
    core.pendingSync = SyncMode::None;
    core.pendingAppId = -1;
    goNetwork_ = false;
    return StateTransition::stay(StateId::AppLauncher);
  }

  if (core.pendingDirectory[0]) return StateTransition::to(StateId::FileList);
  if (core.pendingSync == SyncMode::WifiSetup) {
    core.pendingSync = SyncMode::None;
    return StateTransition::to(StateId::Network);
  }
  if (core.pendingSync == SyncMode::NtpSync || core.pendingSync == SyncMode::PrinterSetup ||
      core.pendingSync == SyncMode::LocalsendSetup) {
    return StateTransition::to(StateId::Network);
  }
  if (goNetwork_) {
    goNetwork_ = false;
    return StateTransition::to(StateId::Network);
  }

  if (app_.active() == &APPS[APP_CLOCK] &&
      clock_app::standbyDue(core, millis(), app_.needsRender() || core.input.hasPendingInteraction())) {
    if (!app_.prepareExit(core, miniapps::ExitTarget::Navigation)) return StateTransition::stay(StateId::AppLauncher);
    auto result = clock_app::enterStandby(core);
    core.cpu.unthrottle();
    board::earlyInit(board::HardwareIdentity::instance().profile(), board::BootPurpose::Interactive);
    board::releaseDeepSleepHolds();
    inputManager.begin();
    board::restoreRecoveryStorage(board::HardwareIdentity::instance().profile());
    const auto restored = core.init();
    core.input.init(core.events);
    core.input.suppressPowerUntilRelease();
    if (!restored.ok() && result != clock_app::StandbyResult::DisplayError)
      result = clock_app::StandbyResult::StorageError;
    if (result != clock_app::StandbyResult::DisplayError && core.display.recover() != hal::Display::InitResult::Ok) {
      result = clock_app::StandbyResult::DisplayError;
    }
    if (result == clock_app::StandbyResult::Interactive) {
      clock_app::resumeInteractive(core, nullptr);
      app_.closeMenu();
    } else {
      app_.abandon();
      menuView_.needsRender = true;
      if (result == clock_app::StandbyResult::TimeRequired) {
        core.pendingSync = SyncMode::NtpSync;
        core.pendingAppId = APP_CLOCK;
        return StateTransition::to(StateId::Network);
      }
      snprintf(core.buf.text, sizeof(core.buf.text), "%s", clock_app::standbyErrorMessage(result));
      return StateTransition::to(StateId::Error);
    }
  }
  return StateTransition::stay(StateId::AppLauncher);
}

void AppLauncherState::render(Core& core) {
  if (app_.active()) {
    if (!app_.needsRender()) return;
    hal::Cpu::PerformanceLock performance(core.cpu);
    const auto frame = app_.render(core);
    if (frame.refresh != miniapps::Refresh::None) {
      renderer_.displayBuffer(
          frame.refresh == miniapps::Refresh::Full ? hal::Display::FULL_REFRESH : hal::Display::FAST_REFRESH,
          frame.turnOffScreen);
    }
  } else if (menuView_.needsRender) {
    hal::Cpu::PerformanceLock performance(core.cpu);
    ui::render(renderer_, THEME, menuView_, APPS);
    menuView_.needsRender = false;
  }
}

void AppLauncherState::launchApp(Core& core, int8_t appId) {
  if (appId < 0 || static_cast<unsigned>(appId) >= APP_COUNT) return;
  LOG_INF(TAG, "Launching app: %s", APPS[appId].name);
  if (app_.start(core, APPS[appId])) menuView_.needsRender = false;
}

bool AppLauncherState::requestSleep(Core& core) {
  if (app_.pendingExit() == miniapps::ExitTarget::None && app_.prepareExit(core, miniapps::ExitTarget::Sleep))
    return true;
  core.cpu.unthrottle();
  core.input.resetIdleTimer();
  core.input.suppressPowerUntilRelease();
  return false;
}

}  // namespace papyrix
