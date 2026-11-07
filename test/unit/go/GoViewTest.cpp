#include <Display.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <string>

#include "test_utils.h"
#include "ui/views/GoView.h"

uint8_t GfxRenderer::frameBuffer_[papyrix::hal::Display::BUFFER_SIZE];

namespace {
bool hasText(const GfxRenderer& renderer, const char* text) {
  for (const auto& call : renderer.centeredTextCalls())
    if (call.text.find(text) != std::string::npos) return true;
  return false;
}
}  // namespace

int main() {
  TestUtils::TestRunner runner("Go view");
  for (const int width : {480, 528}) {
    const int height = width == 480 ? 800 : 792;
    for (const bool touch : {false, true}) {
      const auto layout = ui::goLayout(width, height, touch);
      const int firstX = layout.board.x + layout.spacing / 2;
      const int firstY = layout.board.y + layout.spacing / 2;
      for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) {
          runner.expectEq(
              y * 9 + x,
              ui::goPointAt(layout, {int16_t(firstX + x * layout.spacing), int16_t(firstY + y * layout.spacing)}),
              "intersection maps to board point");
        }
      runner.expectEq(-1, ui::goPointAt(layout, {int16_t(layout.board.x - 1), int16_t(firstY)}), "left outside");
      runner.expectEq(-1, ui::goPointAt(layout, {int16_t(layout.board.x + layout.board.width), int16_t(firstY)}),
                      "right outside");
      runner.expectEq(-1, ui::goPointAt(layout, {int16_t(firstX), int16_t(layout.board.y - 1)}), "above board");
      runner.expectEq(-1, ui::goPointAt(layout, {int16_t(firstX), int16_t(layout.board.y + layout.board.height)}),
                      "below board");
      runner.expectTrue(
          layout.board.x >= 0 && layout.board.x + layout.board.width <= width && layout.hintY + 30 < height - 56,
          "board and hint fit above footer");
      if (touch) {
        runner.expectTrue(layout.back.x >= 0 && layout.place.x + layout.place.width <= width &&
                              layout.back.y + layout.back.height <= height && layout.menu.y == layout.place.y,
                          "touch controls fit on screen");
        runner.expectTrue(
            layout.back.x + layout.back.width < layout.menu.x && layout.menu.x + layout.menu.width < layout.place.x,
            "touch controls do not overlap");
      }
    }
  }

  papyrix::hal::Display display(0, 0, 0, 0, 0, 0);
  GfxRenderer renderer(display);
  Theme theme{};
  theme.primaryTextBlack = true;
  theme.backgroundColor = 0xFF;
  gogame::Position position;
  position.cells[0] = gogame::Color::Black;
  position.cells[1] = gogame::Color::White;
  position.lastMove = 1;
  ui::renderGo(renderer, theme, position, 0, true, false, nullptr);
  const auto layout = ui::goLayout(480, 800, true);
  runner.expectEq(0,
                  ui::goPointAt(layout, {int16_t(layout.board.x + layout.spacing / 2),
                                         int16_t(layout.board.y + layout.spacing / 2)}),
                  "occupied point remains selectable for rejection by the game");
  bool black = false, white = false;
  for (const auto& rect : renderer.fillRects()) {
    if (rect.y == layout.board.y + layout.spacing / 2 && rect.x < layout.board.x + layout.spacing) black |= rect.color;
    if (rect.y == layout.board.y + layout.spacing / 2 && rect.x >= layout.board.x + layout.spacing)
      white |= !rect.color;
  }
  runner.expectTrue(black && white, "both stone colors render");
  runner.expectTrue(hasText(renderer, tr(GO_TURN)) && !hasText(renderer, tr(GO_KOMI)),
                    "ongoing game shows turn, not score");

  renderer.clearCenteredTextCalls();
  position.lastMove = gogame::kPass;
  ui::renderGo(renderer, theme, position, 0, false, true, nullptr);
  runner.expectTrue(hasText(renderer, tr(GO_LAST_PASS)) && hasText(renderer, tr(GO_THINKING)),
                    "pass and thinking are visible");
  runner.expectFalse(hasText(renderer, tr(GO_KOMI)), "thinking does not show a final score");

  renderer.clearCenteredTextCalls();
  position.outcome = gogame::Outcome::Scored;
  ui::renderGo(renderer, theme, position, 0, true, false, nullptr);
  runner.expectTrue(
      hasText(renderer, tr(GO_BLACK)) && hasText(renderer, tr(GO_WHITE)) && hasText(renderer, tr(GO_KOMI)),
      "completed game shows both areas and komi");
  return runner.allPassed() ? 0 : 1;
}
