#pragma once

#include <cstdint>
#include <cstring>

#include "config.h"

// Front button layout options
// Default: Back, Confirm, Left, Right
// Swapped: Left, Right, Back, Confirm
enum FrontButtonLayout { FRONT_BCLR = 0, FRONT_LRBC = 1 };

/**
 * Theme configuration for Papyrix UI.
 *
 * Controls colors, fonts, and layout for all activities.
 * Since e-ink is binary (black/white), colors are represented as booleans:
 * - true = black (default ink color)
 * - false = white (inverted/background color)
 */
struct Theme {
  // Theme metadata
  char displayName[32];  // Human-readable name for settings UI

  // Color scheme
  bool invertedMode;  // Global dark/light mode (true = dark background)

  // Selection styles
  bool selectionFillBlack;  // Selection highlight fill color
  bool selectionTextBlack;  // Text color on selection

  // Text styles
  bool primaryTextBlack;    // Normal text color
  bool secondaryTextBlack;  // Secondary/dimmed text color

  // Background
  uint8_t backgroundColor;  // Screen clear color (0x00 = black, 0xFF = white)

  // Layout margins and spacing
  uint8_t screenMarginTop;
  uint8_t screenMarginSide;
  uint8_t itemHeight;
  uint8_t itemSpacing;

  // Font IDs
  int uiFontId;
  int smallFontId;
  int readerFontId;
  int readerFontIdMedium;
  int readerFontIdLarge;

  // External font family names (empty = use builtin)
  char uiFontFamily[32];
  char readerFontFamilySmall[32];
  char readerFontFamilyMedium[32];
  char readerFontFamilyLarge[32];

  // UI Layout settings
  uint8_t frontButtonLayout;  // FrontButtonLayout enum: FRONT_BCLR or FRONT_LRBC
};

/**
 * Get default light theme - used as default when no theme file exists.
 */
inline Theme getBuiltinLightTheme() {
  Theme theme = {};
  strncpy(theme.displayName, "Light", sizeof(theme.displayName));
  theme.invertedMode = false;
  theme.selectionFillBlack = true;
  theme.selectionTextBlack = false;
  theme.primaryTextBlack = true;
  theme.secondaryTextBlack = true;
  theme.backgroundColor = 0xFF;
  theme.screenMarginTop = 9;
  theme.screenMarginSide = 3;
  theme.itemHeight = 30;
  theme.itemSpacing = 0;
  theme.uiFontId = UI_FONT_ID;
  theme.smallFontId = SMALL_FONT_ID;
  theme.readerFontId = READER_FONT_ID;
  theme.readerFontIdMedium = READER_FONT_ID_MEDIUM;
  theme.readerFontIdLarge = READER_FONT_ID_LARGE;
  theme.uiFontFamily[0] = '\0';
  theme.readerFontFamilySmall[0] = '\0';
  theme.readerFontFamilyMedium[0] = '\0';
  theme.readerFontFamilyLarge[0] = '\0';
  theme.frontButtonLayout = FRONT_BCLR;
  return theme;
}

/**
 * Get default dark theme - inverted colors.
 */
inline Theme getBuiltinDarkTheme() {
  Theme theme = {};
  strncpy(theme.displayName, "Dark", sizeof(theme.displayName));
  theme.invertedMode = true;
  theme.selectionFillBlack = false;
  theme.selectionTextBlack = true;
  theme.primaryTextBlack = false;
  theme.secondaryTextBlack = false;
  theme.backgroundColor = 0x00;
  theme.screenMarginTop = 9;
  theme.screenMarginSide = 3;
  theme.itemHeight = 30;
  theme.itemSpacing = 0;
  theme.uiFontId = UI_FONT_ID;
  theme.smallFontId = SMALL_FONT_ID;
  theme.readerFontId = READER_FONT_ID;
  theme.readerFontIdMedium = READER_FONT_ID_MEDIUM;
  theme.readerFontIdLarge = READER_FONT_ID_LARGE;
  theme.uiFontFamily[0] = '\0';
  theme.readerFontFamilySmall[0] = '\0';
  theme.readerFontFamilyMedium[0] = '\0';
  theme.readerFontFamilyLarge[0] = '\0';
  theme.frontButtonLayout = FRONT_BCLR;
  return theme;
}

// For use in initialization
#define BUILTIN_LIGHT_THEME getBuiltinLightTheme()
#define BUILTIN_DARK_THEME getBuiltinDarkTheme()
