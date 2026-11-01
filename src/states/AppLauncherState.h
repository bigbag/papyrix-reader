#pragma once

#include <cstdint>

#include "../ui/views/AppLauncherViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {
namespace clock_app {
struct ResumeRecord;
}

class AppLauncherState : public State {
 public:
  explicit AppLauncherState(GfxRenderer& renderer);
  ~AppLauncherState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::AppLauncher; }
  void resumeClock(Core& core, const clock_app::ResumeRecord& record);
  bool requestSleep(Core& core);

 private:
  enum class Mode : uint8_t { Menu, App, Overlay };
  enum class PendingExit : uint8_t { None, Apps, Sleep };
  PendingExit pendingExit_ = PendingExit::None;

  GfxRenderer& renderer_;
  Mode mode_;
  int8_t activeApp_;
  bool needsRender_;
  bool goNetwork_;

  ui::AppMenuView menuView_;

  void activateMenuItem(Core& core);
  void launchApp(Core& core);
  void stopApp(Core& core);
  bool saveClockSettings(Core& core);
  void showOverlay();
  void hideOverlay();
};

}  // namespace papyrix
