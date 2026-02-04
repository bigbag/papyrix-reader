#pragma once

#include <Epub/RenderConfig.h>

#include <cstdint>

#include "Result.h"

struct Theme;

namespace papyrix {

namespace drivers {
class Storage;
}

struct Settings {
  // Sleep screen display modes
  enum SleepScreenMode : uint8_t { SleepDark = 0, SleepLight = 1, SleepCustom = 2, SleepCover = 3 };

  // Status bar display modes
  enum StatusBarMode : uint8_t { StatusNone = 0, StatusShow = 1 };

  // Screen orientation
  enum Orientation : uint8_t {
    Portrait = 0,      // 480x800 logical coordinates (current default)
    LandscapeCW = 1,   // 800x480 logical coordinates, rotated 180deg (swap top/bottom)
    Inverted = 2,      // 480x800 logical coordinates, inverted
    LandscapeCCW = 3,  // 800x480 logical coordinates, native panel orientation
  };

  // Reader font size
  enum FontSize : uint8_t { FontSmall = 0, FontMedium = 1, FontLarge = 2 };

  // Side button layout
  enum SideButtonLayout : uint8_t { PrevNext = 0, NextPrev = 1 };

  // Auto-sleep timeout (in minutes)
  enum AutoSleepTimeout : uint8_t { Sleep5Min = 0, Sleep10Min = 1, Sleep15Min = 2, Sleep30Min = 3, SleepNever = 4 };

  // Pages per full refresh (to clear ghosting)
  enum PagesPerRefresh : uint8_t { PPR1 = 0, PPR5 = 1, PPR10 = 2, PPR15 = 3, PPR30 = 4 };

  // Paragraph alignment (values match TextBlock::BLOCK_STYLE)
  enum ParagraphAlignment : uint8_t { AlignJustified = 0, AlignLeft = 1, AlignCenter = 2, AlignRight = 3 };

  // Text layout presets
  enum TextLayout : uint8_t { LayoutCompact = 0, LayoutStandard = 1, LayoutLarge = 2 };

  // Line spacing presets
  enum LineSpacing : uint8_t { SpacingCompact = 0, SpacingNormal = 1, SpacingRelaxed = 2, SpacingLarge = 3 };

  // Short power button press actions
  enum PowerButtonAction : uint8_t { PowerIgnore = 0, PowerSleep = 1, PowerPageTurn = 2 };

  // Startup behavior
  enum StartupBehavior : uint8_t { StartupLastDocument = 0, StartupHome = 1 };

  // Settings fields (same order as CrossPointSettings for binary compatibility)
  uint8_t sleepScreen = SleepDark;
  uint8_t statusBar = StatusShow;
  uint8_t textLayout = LayoutStandard;
  uint8_t shortPwrBtn = PowerIgnore;
  uint8_t orientation = Portrait;
  uint8_t fontSize = FontMedium;
  uint8_t pagesPerRefresh = PPR15;
  uint8_t sideButtonLayout = PrevNext;
  uint8_t autoSleepMinutes = Sleep10Min;
  uint8_t paragraphAlignment = AlignJustified;
  uint8_t hyphenation = 1;
  uint8_t textAntiAliasing = 1;
  uint8_t showImages = 1;
  uint8_t startupBehavior = StartupLastDocument;
  uint8_t coverDithering = 0;
  uint8_t lineSpacing = SpacingNormal;
  char themeName[32] = "light";
  char lastBookPath[256] = "";          // Path to last opened book
  uint8_t pendingTransition = 0;        // 0=none, 1=UI, 2=Reader
  uint8_t transitionReturnTo = 0;       // ReturnTo enum value (0=HOME, 1=FILE_MANAGER)
  uint8_t sunlightFadingFix = 0;        // Power down display after refresh (SSD1677 UV protection)
  char fileListDir[256] = "/";          // FileListState: last directory
  char fileListSelectedName[128] = "";  // FileListState: last selected filename
  uint16_t fileListSelectedIndex = 0;   // FileListState: last selected index

  // Persistence (using drivers::Storage wrapper)
  Result<void> load(drivers::Storage& storage);
  Result<void> save(drivers::Storage& storage) const;

  // Legacy persistence (uses SdMan directly - for early init before Core)
  bool loadFromFile();
  bool saveToFile() const;

  // Computed values
  uint16_t getPowerButtonDuration() const { return (shortPwrBtn == PowerSleep) ? 10 : 400; }

  uint32_t getAutoSleepTimeoutMs() const {
    switch (autoSleepMinutes) {
      case Sleep5Min:
        return 5 * 60 * 1000;
      case Sleep15Min:
        return 15 * 60 * 1000;
      case Sleep30Min:
        return 30 * 60 * 1000;
      case SleepNever:
        return 0;
      default:
        return 10 * 60 * 1000;
    }
  }

  int getReaderFontId(const Theme& theme) const;

  int getPagesPerRefreshValue() const {
    constexpr int values[] = {1, 5, 10, 15, 30};
    return values[pagesPerRefresh];
  }

  uint8_t getIndentLevel() const {
    switch (textLayout) {
      case LayoutCompact:
        return 0;
      case LayoutStandard:
        return 2;
      case LayoutLarge:
        return 3;
      default:
        return 2;
    }
  }

  uint8_t getSpacingLevel() const {
    switch (textLayout) {
      case LayoutCompact:
        return 0;
      case LayoutStandard:
        return 1;
      case LayoutLarge:
        return 3;
      default:
        return 1;
    }
  }

  float getLineCompression() const {
    switch (lineSpacing) {
      case SpacingCompact:
        return 0.85f;
      case SpacingNormal:
        return 0.95f;
      case SpacingRelaxed:
        return 1.10f;
      case SpacingLarge:
        return 1.20f;
      default:
        return 0.95f;
    }
  }

  RenderConfig getRenderConfig(const Theme& theme, uint16_t viewportWidth, uint16_t viewportHeight) const;
};

}  // namespace papyrix
