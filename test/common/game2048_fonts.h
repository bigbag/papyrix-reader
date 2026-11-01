#pragma once

#include <GfxRenderer.h>
#include <Theme.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/reader_large_2b.h>
#include <builtinFonts/reader_large_bold_2b.h>
#include <builtinFonts/reader_medium_2b.h>
#include <builtinFonts/reader_medium_bold_2b.h>
#include <builtinFonts/reader_xsmall_bold_2b.h>
#include <builtinFonts/reader_xsmall_regular_2b.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

inline void game2048Fonts(GfxRenderer& renderer) {
  static const EpdFont uiFont(&ui_12), uiBold(&ui_bold_12), small(&small14);
  static const EpdFont regular(&reader_2b), bold(&reader_bold_2b);
  static const EpdFont medium(&reader_medium_2b), mediumBold(&reader_medium_bold_2b);
  static const EpdFont large(&reader_large_2b), largeBold(&reader_large_bold_2b);
  static const EpdFont xsmall(&reader_xsmall_regular_2b), xsmallBold(&reader_xsmall_bold_2b);
  renderer.insertFont(UI_FONT_ID, EpdFontFamily(&uiFont, &uiBold));
  renderer.insertFont(SMALL_FONT_ID, EpdFontFamily(&small));
  renderer.insertFont(READER_FONT_ID, EpdFontFamily(&regular, &bold));
  renderer.insertFont(READER_FONT_ID_MEDIUM, EpdFontFamily(&medium, &mediumBold));
  renderer.insertFont(READER_FONT_ID_LARGE, EpdFontFamily(&large, &largeBold));
  renderer.insertFont(READER_FONT_ID_XSMALL, EpdFontFamily(&xsmall, &xsmallBold));
}
