#pragma once

#include <cstdint>

#include "../apps/MiniApp.h"
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
  GfxRenderer& renderer_;
  miniapps::Host<Core, Event> app_;
  bool goNetwork_;

  ui::AppMenuView menuView_;

  void activateMenuItem(Core& core);
  void launchApp(Core& core, int8_t appId);
};

}  // namespace papyrix
