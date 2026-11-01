#include <vector>

#include "game2048_fonts.h"
#include "test_utils.h"
#include "ui/views/Game2048View.h"

int main() {
  TestUtils::TestRunner runner("2048 layout");
  for (bool touch : {false, true}) {
    for (const auto size : {ui::touch::Point{480, 800}, ui::touch::Point{528, 792}}) {
      const auto layout = ui::game2048Layout(size.x, size.y, touch);
      const auto& board = layout.board;
      runner.expectTrue(board.x >= 0 && board.y >= 128 && board.width == board.height &&
                            board.x + board.width <= size.x && board.y + board.height < layout.hintY,
                        "board fits below score and above hint");
      runner.expectTrue(layout.hintY + 24 < size.y - 50, "hint stays above footer");
      for (int i = 0; i < 4; ++i) {
        const auto& arrow = layout.arrows[i];
        if (!touch) {
          runner.expectEq(int16_t{0}, arrow.height, "button devices have no touch arrows");
          continue;
        }
        runner.expectTrue(arrow.height >= 46 && arrow.y > layout.hintY + 20 && arrow.y + arrow.height <= size.y - 50 &&
                              arrow.x >= 0 && arrow.x + arrow.width <= size.x,
                          "arrows fit separate row above footer");
        runner.expectEq(
            i, ui::game2048ArrowAt(layout, {int16_t(arrow.x + arrow.width / 2), int16_t(arrow.y + arrow.height / 2)}),
            "arrow center selects direction");
        runner.expectEq(-1, ui::game2048ArrowAt(layout, {int16_t(arrow.x + arrow.width), arrow.y}),
                        "arrow gap is not a move");
      }
      runner.expectEq(-1, ui::game2048ArrowAt(layout, {-1, -1}), "outside is not a move");
      runner.expectEq(-1, ui::game2048ArrowAt(layout, {board.x, board.y}), "board is not arrow target");
    }
  }
  for (bool x3 : {false, true}) {
    papyrix::hal::Display panel;
    if (x3) panel.setDisplayX3();
    GfxRenderer renderer(panel);
    renderer.begin();
    game2048Fonts(renderer);
    const papyrix::game2048::Game game{{1, 5, 6, 17, 7, 8, 9, 10, 11, 12, 13, 14, 0, 1, 2, 3}, 123456, true};
    ui::renderGame2048(renderer, getBuiltinLightTheme(), game, !x3);
    const std::vector<uint8_t> light(renderer.getFrameBuffer(), renderer.getFrameBuffer() + renderer.getBufferSize());
    if (!x3) {
      const int width = renderer.getScreenWidth();
      std::vector<bool> pixels(width * renderer.getScreenHeight());
      for (int py = 0; py < panel.getDisplayHeight(); ++py) {
        for (int px = 0; px < panel.getDisplayWidth(); ++px) {
          int x, y;
          if (!renderer.panelToLogical(px, py, &x, &y)) {
            runner.expectTrue(false, "panel to logical mapping succeeds");
            return 1;
          }
          pixels[y * width + x] = !(light[py * panel.getDisplayWidthBytes() + px / 8] & (0x80 >> (px % 8)));
        }
      }
      const auto layout = ui::game2048Layout(width, renderer.getScreenHeight(), true);
      for (int i = 0; i < 4; ++i) {
        const auto& rect = layout.arrows[i];
        const int dx = i == 0 ? -1 : i == 3 ? 1 : 0;
        const int dy = i == 1 ? -1 : i == 2 ? 1 : 0;
        for (int offset = -2; offset <= 3; ++offset) {
          const int x = rect.x + rect.width / 2 - dx * 5 + (dy != 0 ? offset : 0);
          const int y = rect.y + rect.height / 2 - dy * 5 + (dx != 0 ? offset : 0);
          runner.expectEq(offset >= -1 && offset <= 2, bool(pixels[y * width + x]),
                          "touch arrow shaft is four pixels thick");
        }
      }
    }
    ui::renderGame2048(renderer, getBuiltinDarkTheme(), game, !x3);
    bool inverted = true;
    for (size_t i = 0; i < light.size(); ++i) inverted = inverted && renderer.getFrameBuffer()[i] == uint8_t(~light[i]);
    runner.expectTrue(inverted, "dark theme reverses tile text, fill, arrows, and navigation");
  }
  return runner.allPassed() ? 0 : 1;
}
