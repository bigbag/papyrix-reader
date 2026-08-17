#pragma once

#include <HomeThumbnail.h>

#include <cstdint>
#include <string>

#include "../ui/views/HomeView.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

class HomeState : public State {
 public:
  explicit HomeState(GfxRenderer& renderer);
  ~HomeState() override = default;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Home; }

 private:
  GfxRenderer& renderer_;
  ui::HomeView view_;

  // Cover image state
  home_thumbnail::HomeImageSelection homeImage_;
  bool hasCoverImage_ = false;
  bool coverLoadFailed_ = false;

  void loadLastBook(Core& core);
  void selectHomeImage(bool imagesEnabled, const std::string& thumbnailPath, const std::string& coverPath);
  void updateBattery();

  static constexpr unsigned long kBatteryPollIntervalMs = 5000;
  unsigned long lastBatteryPollMs_ = 0;
  bool renderCoverToCard();
};

}  // namespace papyrix
