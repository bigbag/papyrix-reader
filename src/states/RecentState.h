#pragma once

#include <cstddef>
#include <cstdint>

#include "../content/RecentBooksStore.h"
#include "../ui/views/ReaderViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

class RecentState : public State {
  enum class Screen : uint8_t { Browse, Stats };

 public:
  static constexpr Button FILES_BUTTON = Button::Left;
  static constexpr Button INFO_BUTTON = Button::Right;

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
  ui::BookStatsView statsView_;

  void moveUp();
  void moveDown();
  StateTransition openSelected(Core& core);
  size_t displayedCount() const;
  void showSelectedStats();
  void renderBrowse(Core& core);
  void renderStats(Core& core);
  int rowHeight() const;
};

}  // namespace papyrix
