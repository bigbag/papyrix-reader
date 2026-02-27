#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>

namespace ui {

// Button bar configuration - tracks which buttons are active
struct ButtonBar {
  const char* labels[4] = {"", "", "", ""};

  explicit constexpr ButtonBar(const char* b1 = "", const char* b2 = "", const char* b3 = "", const char* b4 = "")
      : labels{b1 ? b1 : "", b2 ? b2 : "", b3 ? b3 : "", b4 ? b4 : ""} {}

  bool isActive(int idx) const { return idx >= 0 && idx < 4 && labels[idx] && labels[idx][0] != '\0'; }
};

// Title - Centered bold heading
void title(const GfxRenderer& r, const Theme& t, int y, const char* text);

// Brand title - Left-aligned bold heading with margin
void brandTitle(const GfxRenderer& r, const Theme& t, int y, const char* text);

// Menu item - Selectable entry with optional highlight
void menuItem(const GfxRenderer& r, const Theme& t, int y, const char* text, bool selected);

// Toggle - On/Off setting row
void toggle(const GfxRenderer& r, const Theme& t, int y, const char* label, bool value, bool selected);

// Enum value - Setting with current value display
void enumValue(const GfxRenderer& r, const Theme& t, int y, const char* label, const char* value, bool selected);

// Set front button layout for hint remapping (0=BCLR, 1=LRBC)
void setFrontButtonLayout(uint8_t layout);

// Button bar - 4-button hints at bottom (wraps drawButtonHints)
void buttonBar(const GfxRenderer& r, const Theme& t, const char* b1, const char* b2, const char* b3, const char* b4);
void buttonBar(const GfxRenderer& r, const Theme& t, const ButtonBar& buttons);

// Progress bar - Shows current/total progress
void progress(const GfxRenderer& r, const Theme& t, int y, int current, int total);

// Text - Single line text block
void text(const GfxRenderer& r, const Theme& t, int y, const char* str);

// Text wrapped - Multi-line wrapped text (returns lines used)
int textWrapped(const GfxRenderer& r, const Theme& t, int y, const char* str, int maxLines);

// Image - Bitmap display at position
void image(const GfxRenderer& r, int x, int y, const uint8_t* data, int w, int h);

// Dialog - Yes/No confirmation dialog
void dialog(const GfxRenderer& r, const Theme& t, const char* title, const char* msg, int selected);

// Keyboard - 10x10 grid keyboard with control row
struct KeyboardState {
  static constexpr int NUM_ROWS = 10;
  static constexpr int KEYS_PER_ROW = 10;

  // Control row boundaries
  static constexpr int CONTROL_ROW = 0;
  static constexpr int BACKSPACE_START = 0;
  static constexpr int BACKSPACE_END = 2;
  static constexpr int SPACE_START = 3;
  static constexpr int SPACE_END = 6;
  static constexpr int CONFIRM_START = 7;
  static constexpr int CONFIRM_END = 9;

  int8_t cursorX = 0;
  int8_t cursorY = 1;  // Start on first letter row, not control row

  void moveUp();
  void moveDown();
  void moveLeft();
  void moveRight();

  bool isOnBackspace() const {
    return cursorY == CONTROL_ROW && cursorX >= BACKSPACE_START && cursorX <= BACKSPACE_END;
  }
  bool isOnSpace() const { return cursorY == CONTROL_ROW && cursorX >= SPACE_START && cursorX <= SPACE_END; }
  bool isOnConfirm() const { return cursorY == CONTROL_ROW && cursorX >= CONFIRM_START && cursorX <= CONFIRM_END; }
};

void keyboard(const GfxRenderer& r, const Theme& t, int y, const KeyboardState& state);
char getKeyboardChar(const KeyboardState& state);

// Battery indicator - Icon + percentage
void battery(const GfxRenderer& r, const Theme& t, int x, int y, int percent);

// Status bar - Page numbers and progress percentage
void statusBar(const GfxRenderer& r, const Theme& t, int page, int total, int percent);

// Book card - Cover + title + author (for home screen)
void bookCard(const GfxRenderer& r, const Theme& t, int y, const char* title, const char* author, const uint8_t* cover,
              int coverW, int coverH);

// File entry - File name with directory indicator
void fileEntry(const GfxRenderer& r, const Theme& t, int y, const char* name, bool isDir, bool selected);

// Chapter item - TOC entry with depth indentation and current chapter indicator
// fontId: Use reader font (readerFontIdXSmall) for EPUB/TXT/Markdown to support non-Latin glyphs,
//         or UI font (uiFontId) for XTC/XTCH where reader fonts aren't loaded
void chapterItem(const GfxRenderer& r, const Theme& t, int fontId, int y, const char* title, uint8_t depth,
                 bool selected, bool isCurrent);

// Wifi entry - Network name + signal strength + lock icon
void wifiEntry(const GfxRenderer& r, const Theme& t, int y, const char* ssid, int signal, bool locked, bool selected);

// Centered text - Horizontally centered text
void centeredText(const GfxRenderer& r, const Theme& t, int y, const char* str);

// Centered message - Bold centered message (for loading/error screens)
void centeredMessage(const GfxRenderer& r, const Theme& t, int fontId, const char* message);

// Book placeholder - Stylized book icon with "No Cover" label for missing covers
void bookPlaceholder(const GfxRenderer& r, const Theme& t, int x, int y, int width, int height);

// Overlay box - Centered notification box with text (for Indexing/Loading messages)
void overlayBox(const GfxRenderer& r, const Theme& t, int fontId, int y, const char* message);

// Popup menu - Centered overlay with title and selectable items
void popupMenu(const GfxRenderer& r, const Theme& t, const char* title, const char* const* items, int itemCount,
               int selected);

// Two-column row - Label on left, value on right
void twoColumnRow(const GfxRenderer& r, const Theme& t, int y, const char* label, const char* value);

// Reader status bar data
struct ReaderStatusBarData {
  int currentPage;
  int totalPages;
  const char* title;
  int batteryPercent;      // -1 if unavailable
  uint8_t mode;            // Settings::StatusBarMode
  bool isPartial = false;  // True when page cache is incomplete
};

// Reader status bar - Battery (left), title (center), page numbers (right)
void readerStatusBar(const GfxRenderer& r, const Theme& t, int marginLeft, int marginRight, int marginBottom,
                     const ReaderStatusBarData& data);

}  // namespace ui
