#pragma once
#include <cstdint>
#include <iosfwd>

#include "ThemeManager.h"
#include "config.h"

class CrossPointSettings {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  // Static instance
  static CrossPointSettings instance;

 public:
  // Delete copy constructor and assignment
  CrossPointSettings(const CrossPointSettings&) = delete;
  CrossPointSettings& operator=(const CrossPointSettings&) = delete;

  // Should match with SettingsActivity text
  enum SLEEP_SCREEN_MODE { DARK = 0, LIGHT = 1, CUSTOM = 2, COVER = 3 };

  // Status bar display type enum
  enum STATUS_BAR_MODE { NONE = 0, NO_PROGRESS = 1, FULL = 2 };

  enum ORIENTATION {
    PORTRAIT = 0,      // 480x800 logical coordinates (current default)
    LANDSCAPE_CW = 1,  // 800x480 logical coordinates, rotated 180° (swap top/bottom)
    INVERTED = 2,      // 480x800 logical coordinates, inverted
    LANDSCAPE_CCW = 3  // 800x480 logical coordinates, native panel orientation
  };

  enum FONT_SIZE { FONT_SMALL = 0, FONT_MEDIUM = 1, FONT_LARGE = 2 };

  // Side button layout options
  // Default: Previous, Next
  // Swapped: Next, Previous
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1 };

  // Auto-sleep timeout options (in minutes)
  enum AUTO_SLEEP_TIMEOUT { SLEEP_5_MIN = 0, SLEEP_10_MIN = 1, SLEEP_15_MIN = 2, SLEEP_30_MIN = 3, SLEEP_NEVER = 4 };

  // Pages per full refresh (to clear ghosting)
  enum PAGES_PER_REFRESH { PPR_1 = 0, PPR_5 = 1, PPR_10 = 2, PPR_15 = 3, PPR_30 = 4 };

  // Paragraph alignment options (values match TextBlock::BLOCK_STYLE)
  enum PARAGRAPH_ALIGNMENT { ALIGN_JUSTIFIED = 0, ALIGN_LEFT = 1, ALIGN_CENTER = 2, ALIGN_RIGHT = 3 };

  // Short power button press actions
  enum SHORT_PWRBTN { PWRBTN_IGNORE = 0, PWRBTN_SLEEP = 1, PWRBTN_PAGE_TURN = 2 };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Status bar settings
  uint8_t statusBar = FULL;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  // Short power button click behaviour
  uint8_t shortPwrBtn = PWRBTN_IGNORE;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  uint8_t orientation = PORTRAIT;
  // Font size for reading
  // 0 = small (14pt), 1 = normal (16pt, default), 2 = large (18pt)
  uint8_t fontSize = FONT_MEDIUM;
  // Pages per full refresh for e-paper (to clear ghosting)
  uint8_t pagesPerRefresh = PPR_15;
  // Side button layout
  uint8_t sideButtonLayout = PREV_NEXT;
  // Show book details: cover as first page when reading, and title/author on home screen
  uint8_t showBookDetails = 1;
  // Auto-sleep timeout setting
  uint8_t autoSleepMinutes = SLEEP_10_MIN;
  // Paragraph alignment for EPUB text
  uint8_t paragraphAlignment = ALIGN_JUSTIFIED;
  // Hyphenation enabled (soft hyphen support)
  uint8_t hyphenation = 1;
  // Text anti-aliasing (grayscale text rendering)
  uint8_t textAntiAliasing = 1;
  // Theme name (loaded from /themes/<name>.theme)
  char themeName[32] = "light";

  ~CrossPointSettings() = default;

  // Get singleton instance
  static CrossPointSettings& getInstance() { return instance; }

  uint16_t getPowerButtonDuration() const { return (shortPwrBtn == PWRBTN_SLEEP) ? 10 : 400; }

  uint32_t getAutoSleepTimeoutMs() const {
    switch (autoSleepMinutes) {
      case SLEEP_5_MIN:
        return 5 * 60 * 1000;
      case SLEEP_15_MIN:
        return 15 * 60 * 1000;
      case SLEEP_30_MIN:
        return 30 * 60 * 1000;
      case SLEEP_NEVER:
        return 0;  // Disabled
      default:
        return 10 * 60 * 1000;
    }
  }

  int getReaderFontId() const {
    switch (fontSize) {
      case FONT_MEDIUM:
        return THEME.readerFontIdMedium;
      case FONT_LARGE:
        return THEME.readerFontIdLarge;
      default:
        return THEME.readerFontId;
    }
  }

  int getPagesPerRefreshValue() const {
    constexpr int values[] = {1, 5, 10, 15, 30};
    return values[pagesPerRefresh];
  }

  bool saveToFile() const;
  bool loadFromFile();
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
