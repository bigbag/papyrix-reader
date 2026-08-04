#include "test_utils.h"

#include <EInkDisplay.h>
#include <GfxRenderer.h>
#include <Theme.h>

#include "ui/WrappedText.h"

uint8_t GfxRenderer::frameBuffer_[EInkDisplay::BUFFER_SIZE];

int main() {
  TestUtils::TestRunner runner("WrappedTextTest");
  EInkDisplay display(0, 0, 0, 0, 0, 0);
  GfxRenderer renderer(display);

  renderer.setWrappedTextResult({"first", "second"});
  const int count =
      ui::centeredTextWrapped(renderer, 7, 100, "long text", 220, 2, false, EpdFontFamily::BOLD);
  runner.expectEq(2, count, "wrapped helper returns rendered line count");
  runner.expectEq(220, renderer.lastWrapMaxWidth(), "wrapped helper forwards width");
  runner.expectEq(2, renderer.lastWrapMaxLines(), "wrapped helper forwards line cap");
  runner.expectEq(size_t(2), renderer.centeredTextCalls().size(), "wrapped helper draws each line");
  runner.expectEq(100, renderer.centeredTextCalls()[0].y, "first line keeps requested y");
  runner.expectEq(120, renderer.centeredTextCalls()[1].y, "second line uses font line height");
  runner.expectFalse(renderer.centeredTextCalls()[0].black, "wrapped helper preserves ink color");

  renderer.clearCenteredTextCalls();
  runner.expectEq(0, ui::centeredTextWrapped(renderer, 7, 100, "", 220, 2, true),
                  "empty text renders no lines");
  runner.expectEq(0, ui::centeredTextWrapped(renderer, 7, 100, "text", 0, 2, true),
                  "zero width renders no lines");
  runner.expectEq(size_t(0), renderer.centeredTextCalls().size(), "invalid input makes no draw calls");

  Theme theme{};
  theme.screenMarginSide = 3;
  theme.itemPaddingX = 8;
  theme.uiFontId = 7;
  theme.primaryTextBlack = true;
  renderer.setWrappedTextResult({"short"});
  runner.expectEq(1, ui::centeredText(renderer, theme, 50, "short"), "default centered text stays one line");
  runner.expectEq(458, renderer.lastWrapMaxWidth(), "default centered text uses screen margins");
  runner.expectEq(2, renderer.lastWrapMaxLines(), "default centered text allows two lines");

  renderer.clearCenteredTextCalls();
  renderer.setWrappedTextResult({"one", "two", "unexpected"});
  runner.expectEq(2, ui::centeredTextWrapped(renderer, 7, 0, "long", 100, 2, true),
                  "helper enforces its line cap defensively");
  runner.expectEq(size_t(2), renderer.centeredTextCalls().size(), "line cap prevents a third draw");

  const char* overflowingExamples[] = {
      "Buchdateien und Statistiken bleiben erhalten.",
      "En Calibre: Conectar/compartir > Dispositivo inalámbrico",
      "Dans Calibre : Connecter/partager > Appareil sans fil",
  };
  for (const char* text : overflowingExamples) {
    renderer.clearCenteredTextCalls();
    renderer.setWrappedTextResult({"bounded first line", "bounded second line"});
    runner.expectEq(2, ui::centeredTextWrapped(renderer, 7, 0, text, 458, 2, true),
                    "audited locale text uses bounded wrapping");
    runner.expectEq(size_t(2), renderer.centeredTextCalls().size(),
                    "audited locale text draws only bounded lines");
  }

  return runner.allPassed() ? 0 : 1;
}
