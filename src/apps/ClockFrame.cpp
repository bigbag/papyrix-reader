#include <GfxRenderer.h>
#include <Theme.h>

#include <ctime>

#include "../ui/Elements.h"
#include "ClockApp.h"

namespace papyrix::clock_app {

void renderFrame(GfxRenderer& renderer, const ClockFrame& frame) {
  const auto& appearance = frame.appearance;
  Theme theme{};
  theme.invertedMode = appearance.invertedMode;
  theme.selectionFillBlack = appearance.selectionFillBlack;
  theme.selectionTextBlack = appearance.selectionTextBlack;
  theme.primaryTextBlack = appearance.primaryTextBlack;
  theme.secondaryTextBlack = appearance.secondaryTextBlack;
  theme.backgroundColor = appearance.backgroundColor;
  theme.screenMarginTop = appearance.screenMarginTop;
  theme.screenMarginSide = appearance.screenMarginSide;
  theme.itemHeight = appearance.itemHeight;
  theme.itemSpacing = appearance.itemSpacing;
  theme.itemPaddingX = appearance.itemPaddingX;
  theme.itemValuePadding = appearance.itemValuePadding;
  theme.uiFontId = UI_FONT_ID;
  theme.smallFontId = SMALL_FONT_ID;
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(appearance.orientation));
  renderer.clearScreen(theme.backgroundColor);

  const std::time_t local = static_cast<std::time_t>(frame.utcSeconds + static_cast<int64_t>(frame.utcOffset) * 3600);
  std::tm timeinfo{};
#ifdef _WIN32
  const bool valid = gmtime_s(&timeinfo, &local) == 0;
#else
  const bool valid = gmtime_r(&local, &timeinfo) != nullptr;
#endif
  if (frame.utcSeconds >= 946684800 && valid) {
    clock_faces::render({renderer, theme, timeinfo, frame.use24h != 0, frame.dateFormat, frame.utcOffset}, frame.face);
  } else {
    renderer.drawCenteredText(theme.uiFontId, (renderer.getScreenHeight() - renderer.getLineHeight(theme.uiFontId)) / 2,
                              "Time not synced", theme.primaryTextBlack);
  }

  constexpr int batteryBodyWidth = 30;
  constexpr int batteryTipWidth = 3;
  constexpr int batteryRightMargin = 20;
  ui::batteryIcon(renderer, theme, renderer.getScreenWidth() - batteryRightMargin - batteryBodyWidth - batteryTipWidth,
                  22, frame.batteryPercent, frame.usbConnected);
  if (frame.interactive) {
    ui::setFrontButtonLayout(appearance.frontButtonLayout);
    ui::buttonBar(renderer, theme, ui::ButtonBar("Back", "Menu", "", ""));
  } else {
    renderer.drawCenteredText(theme.smallFontId, renderer.getScreenHeight() - 34, "Press Power to unlock",
                              theme.secondaryTextBlack);
  }
}

}  // namespace papyrix::clock_app
