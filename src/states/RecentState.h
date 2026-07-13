#pragma once

#include <cstddef>

#include "../content/RecentBooksStore.h"
#include "../ui/views/SettingsViews.h"  // ConfirmDialogView
#include "State.h"

class GfxRenderer;

namespace papyrix {

class RecentState : public State {
  enum class Screen : uint8_t { Browse, ConfirmRemove };

 public:
  explicit RecentState(GfxRenderer& renderer) : renderer_(renderer) {}

  void enter(Core& core) override;
  void exit(Core& core) override {}
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Recent; }

 private:
  GfxRenderer& renderer_;
  size_t selected_ = 0;
  bool needsRender_ = true;
  Screen currentScreen_ = Screen::Browse;
  ui::ConfirmDialogView confirmView_;

  void moveUp();
  void moveDown();
  StateTransition openSelected(Core& core);
  void promptRemove(Core& core);
  void confirmRemove(Core& core);

  int rowHeight() const;     // pixel pitch fitting one title + one author line
  int visibleCount() const;  // one-screen capacity from current fonts/geometry
};

}  // namespace papyrix
