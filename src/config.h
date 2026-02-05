#pragma once

/**
 * Generated with:
 *  ruby -rdigest -e 'puts [
 *    "./lib/EpdFont/builtinFonts/reader_xsmall_regular_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_xsmall_bold_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_xsmall_italic_2b.h",
 *  ].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
 */
#define READER_FONT_ID_XSMALL (-1506109732)

/**
 * Generated with:
 *  ruby -rdigest -e 'puts [
 *    "./lib/EpdFont/builtinFonts/reader_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_bold_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_italic_2b.h",
 *  ].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
 */
#define READER_FONT_ID 1818981670

/**
 * Generated with:
 *  ruby -rdigest -e 'puts [
 *    "./lib/EpdFont/builtinFonts/reader_medium_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_medium_bold_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_medium_italic_2b.h",
 *  ].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
 */
#define READER_FONT_ID_MEDIUM (-455101320)

/**
 * Generated with:
 *  ruby -rdigest -e 'puts [
 *    "./lib/EpdFont/builtinFonts/reader_large_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_large_bold_2b.h",
 *    "./lib/EpdFont/builtinFonts/reader_large_italic_2b.h",
 *  ].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
 */
#define READER_FONT_ID_LARGE 1922188069

/**
 * Generated with:
 *  ruby -rdigest -e 'puts [
 *    "./lib/EpdFont/builtinFonts/ui_12.h",
 *    "./lib/EpdFont/builtinFonts/ui_bold_12.h",
 *  ].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
 */
#define UI_FONT_ID (-731562571)

/**
 * Generated with:
 *  ruby -rdigest -e 'puts [
 *    "./lib/EpdFont/builtinFonts/small14.h",
 *  ].map{|f| Digest::SHA256.hexdigest(File.read(f)).to_i(16) }.sum % (2 ** 32) - (2 ** 31)'
 */
#define SMALL_FONT_ID 1482513144

// System directory for settings and cache
#define PAPYRIX_DIR "/.papyrix"
#define PAPYRIX_CACHE_DIR PAPYRIX_DIR "/cache"
#define PAPYRIX_SETTINGS_FILE PAPYRIX_DIR "/settings.bin"
#define PAPYRIX_STATE_FILE PAPYRIX_DIR "/state.bin"
#define PAPYRIX_WIFI_FILE PAPYRIX_DIR "/wifi.bin"

// Thumbnail dimensions for home screen
#define THUMB_WIDTH 320
#define THUMB_HEIGHT 440

// User configuration directory
#define CONFIG_DIR "/config"
#define CONFIG_CALIBRE_FILE CONFIG_DIR "/calibre.ini"
#define CONFIG_THEMES_DIR CONFIG_DIR "/themes"
#define CONFIG_FONTS_DIR CONFIG_DIR "/fonts"

// Calibre sync settings
#define CALIBRE_BOOKS_DIR "/Calibre"
#define CALIBRE_PORT 9090
#define CALIBRE_PROCESS_TIMEOUT_MS 50

// Applies custom theme fonts for the currently selected font size.
// Call this after font size or theme changes to reload fonts.
void applyThemeFonts();
