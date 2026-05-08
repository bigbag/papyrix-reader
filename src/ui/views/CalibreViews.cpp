#include "CalibreViews.h"

#include <I18n.h>

#include "../Elements.h"

namespace ui {

void render(const GfxRenderer& r, const Theme& t, const CalibreView& v) {
  r.clearScreen(t.backgroundColor);

  title(r, t, t.screenMarginTop, tr(CALIBRE_SYNC));

  const int centerY = r.getScreenHeight() / 2 - 60;

  centeredText(r, t, centerY, v.statusMsg);

  // Show help text below status when available
  if (v.helpText[0] != '\0') {
    centeredText(r, t, centerY + 40, v.helpText);
  }

  if (v.status == CalibreView::Status::Receiving && v.total > 0) {
    progress(r, t, centerY + 50, v.received, v.total);

    char sizeStr[32];
    snprintf(sizeStr, sizeof(sizeStr), "%d / %d KB", v.received / 1024, v.total / 1024);
    centeredText(r, t, centerY + 100, sizeStr);
  }

  buttonBar(r, t, v.buttons);

  r.displayBuffer();
}

}  // namespace ui
