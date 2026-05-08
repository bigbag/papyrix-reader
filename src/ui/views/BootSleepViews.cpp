#include "BootSleepViews.h"

#include <EInkDisplay.h>
#include <I18n.h>

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const BootView& v) {
  // Match old BootActivity layout exactly
  const auto pageWidth = r.getScreenWidth();
  const auto pageHeight = r.getScreenHeight();

  r.clearScreen(t.backgroundColor);

  // Logo position matches old: (pageWidth + 128) / 2, (pageHeight - 128) / 2
  if (v.logoData != nullptr) {
    r.drawImage(v.logoData, (pageWidth + v.logoWidth) / 2, (pageHeight - v.logoHeight) / 2, v.logoWidth, v.logoHeight);
  }

  // Text positions match old BootActivity exactly
  r.drawCenteredText(t.uiFontId, pageHeight / 2 + 70, tr(PAPYRIX), t.primaryTextBlack, BOLD);
  r.drawCenteredText(t.smallFontId, pageHeight / 2 + 110, v.status, t.primaryTextBlack);
  r.drawCenteredText(t.smallFontId, pageHeight - 30, v.version, t.primaryTextBlack);

  r.displayBuffer();
}

void render(const GfxRenderer& r, const Theme& t, const SleepView& v) {
  // Match old SleepActivity renderDefaultSleepScreen() layout exactly
  const auto pageWidth = r.getScreenWidth();
  const auto pageHeight = r.getScreenHeight();

  // Always start with background color (light)
  r.clearScreen(t.backgroundColor);

  // For Logo mode (default), render the same as old SleepActivity
  if (v.mode == SleepView::Mode::Logo) {
    // Logo at same position as boot screen
    if (v.logoData != nullptr) {
      r.drawImage(v.logoData, (pageWidth + v.logoWidth) / 2, (pageHeight - v.logoHeight) / 2, v.logoWidth,
                  v.logoHeight);
    }

    // Text at same positions as boot screen, but "SLEEPING" instead of status
    // Always use primaryTextBlack - invertScreen() will handle color for dark mode
    r.drawCenteredText(t.uiFontId, pageHeight / 2 + 70, tr(PAPYRIX), t.primaryTextBlack, BOLD);
    r.drawCenteredText(t.smallFontId, pageHeight / 2 + 110, tr(SLEEPING), t.primaryTextBlack);

    // Note: No version text on sleep screen (matches old behavior)

    if (v.darkMode) {
      r.invertScreen();
    }
  } else if (v.mode == SleepView::Mode::Black) {
    r.clearScreen(0x00);
  } else if (v.mode == SleepView::Mode::BookCover || v.mode == SleepView::Mode::Custom) {
    // Image modes: center the image
    if (v.imageData != nullptr) {
      const int imageX = (pageWidth - v.imageWidth) / 2;
      const int imageY = (pageHeight - v.imageHeight) / 2;
      r.drawImage(v.imageData, imageX, imageY, v.imageWidth, v.imageHeight);
    }
  }

  // Use HALF_REFRESH for sleep (matches old SleepActivity)
  r.displayBuffer(EInkDisplay::HALF_REFRESH);
}

}  // namespace ui
