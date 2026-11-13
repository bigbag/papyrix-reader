#include <vector>

#include "game2048_fonts.h"
#include "test_utils.h"
#include "ui/views/LightsOutView.h"

int main() {
  TestUtils::TestRunner runner("Lights Out view");
  for (bool touch : {false, true}) {
    for (const auto size : {ui::touch::Point{480, 800}, ui::touch::Point{528, 792}, ui::touch::Point{800, 480},
                            ui::touch::Point{792, 528}}) {
      const auto layout = ui::lightsOutLayout(size.x, size.y, touch);
      const auto& board = layout.board;
      runner.expectTrue(layout.cell >= 40 && board.width == 5 * layout.cell && board.height == board.width &&
                            board.x >= 0 && board.x + board.width <= size.x && board.y >= 100 &&
                            board.y + board.height < layout.hintY && layout.hintY + 24 <= size.y - 50,
                        "board and hint fit between heading and footer");
      if (touch) {
        runner.expectTrue(layout.hintY + 24 <= layout.back.y && layout.back.width >= 40 && layout.back.height >= 40 &&
                              layout.back.x + layout.back.width < layout.menu.x &&
                              layout.menu.x + layout.menu.width < layout.toggle.x &&
                              layout.toggle.x + layout.toggle.width <= size.x &&
                              layout.toggle.y + layout.toggle.height <= size.y,
                          "Back Menu and Toggle remain separate below the board hint");
      }
      for (int i = 0; i < 25; ++i) {
        const int x = board.x + (i % 5) * layout.cell;
        const int y = board.y + (i / 5) * layout.cell;
        runner.expectEq(i, ui::lightsOutCellAt(layout, {int16_t(x), int16_t(y)}), "cell top-left hits cell");
        runner.expectEq(i, ui::lightsOutCellAt(layout, {int16_t(x + layout.cell - 1), int16_t(y + layout.cell - 1)}),
                        "cell bottom-right hits cell");
      }
      runner.expectEq(-1, ui::lightsOutCellAt(layout, {int16_t(board.x - 1), board.y}), "left edge excluded");
      runner.expectEq(-1, ui::lightsOutCellAt(layout, {board.x, int16_t(board.y - 1)}), "top edge excluded");
      runner.expectEq(-1, ui::lightsOutCellAt(layout, {int16_t(board.x + board.width), board.y}),
                      "right edge excluded");
      runner.expectEq(-1, ui::lightsOutCellAt(layout, {board.x, int16_t(board.y + board.height)}),
                      "bottom edge excluded");
    }
  }
  for (bool x3 : {false, true}) {
    papyrix::hal::Display panel;
    if (x3) panel.setDisplayX3();
    GfxRenderer renderer(panel);
    renderer.begin();
    game2048Fonts(renderer);
    for (bool landscape : {false, true}) {
      renderer.setOrientation(landscape ? GfxRenderer::LandscapeCounterClockwise : GfxRenderer::Portrait);
      papyrix::lightsout::Game game{};
      game.lights = 1;
      game.cursor = 1;
      ui::renderLightsOut(renderer, getBuiltinLightTheme(), game, !x3);
      const std::vector<uint8_t> light(renderer.getFrameBuffer(), renderer.getFrameBuffer() + renderer.getBufferSize());
      const auto layout = ui::lightsOutLayout(renderer.getScreenWidth(), renderer.getScreenHeight(), !x3);
      bool lit = false, off = true, cursor = false, plain = true;
      for (int py = 0; py < panel.getDisplayHeight(); ++py) {
        for (int px = 0; px < panel.getDisplayWidth(); ++px) {
          int x, y;
          if (!renderer.panelToLogical(px, py, &x, &y)) continue;
          const bool black = !(light[py * panel.getDisplayWidthBytes() + px / 8] & (0x80 >> (px % 8)));
          if (y == layout.board.y + layout.cell / 2) {
            if (x == layout.board.x + layout.cell / 2) lit = black;
            if (x == layout.board.x + layout.cell + layout.cell / 2) off = black;
            if (x == layout.board.x + layout.cell + 2) cursor = black;
            if (x == layout.board.x + 2 * layout.cell + 2) plain = black;
          }
        }
      }
      runner.expectTrue(lit && !off, "lit and unlit cells have distinct fills");
      runner.expectTrue(cursor && !plain, "cursor is visible on an unlit cell");
      ui::renderLightsOut(renderer, getBuiltinDarkTheme(), game, !x3);
      bool inverted = true;
      for (size_t i = 0; i < light.size(); ++i)
        inverted = inverted && renderer.getFrameBuffer()[i] == uint8_t(~light[i]);
      runner.expectTrue(inverted, "dark theme reverses the view");
      game.cursor = 0;
      ui::renderLightsOut(renderer, getBuiltinLightTheme(), game, !x3);
      bool litCursor = false;
      for (int py = 0; py < panel.getDisplayHeight(); ++py) {
        for (int px = 0; px < panel.getDisplayWidth(); ++px) {
          int x, y;
          if (renderer.panelToLogical(px, py, &x, &y) && x == layout.board.x + 2 &&
              y == layout.board.y + layout.cell / 2)
            litCursor = !(renderer.getFrameBuffer()[py * panel.getDisplayWidthBytes() + px / 8] & (0x80 >> (px % 8)));
        }
      }
      runner.expectTrue(litCursor, "cursor is visible on a lit cell");
    }
  }
  return runner.allPassed() ? 0 : 1;
}