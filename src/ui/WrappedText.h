#pragma once

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Theme.h>

namespace ui {

int centeredTextWrapped(const GfxRenderer& r, int fontId, int y, const char* text, int maxWidth, int maxLines,
                        bool black, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
int centeredText(const GfxRenderer& r, const Theme& t, int y, const char* text);

}  // namespace ui
