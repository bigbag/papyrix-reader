#pragma once

#include <cstdint>

namespace papyrix {

// Boot modes for memory optimization
enum class BootMode : uint8_t {
  UI,     // Full UI mode: all states, all fonts, theme cache
  READER  // Minimal reader mode: ReaderState only, single font size
};

// Where to return when exiting reader mode
enum class ReturnTo : uint8_t {
  HOME,         // Return to HomeState
  FILE_MANAGER  // Return to FileListState
};

// RTC memory structure for mode transitions (persists across restart)
struct ModeTransition {
  uint32_t magic;      // 0xB007MODE - validation marker
  BootMode mode;       // Target boot mode
  ReturnTo returnTo;   // Where to return when exiting reader
  char bookPath[512];  // Path to open in reader mode

  static constexpr uint32_t MAGIC = 0xB007BADE;  // "BOOT BADE" validation marker

  bool isValid() const { return magic == MAGIC; }
  void invalidate() { magic = 0; }
};

// Detect which mode to boot into
// Checks RTC memory first, falls back to settings "Last Document" behavior
BootMode detectBootMode();

// Get the transition data (only valid if detectBootMode returned based on RTC)
const ModeTransition& getTransition();

// Save transition data before ESP.restart()
void saveTransition(BootMode mode, const char* bookPath = nullptr, ReturnTo returnTo = ReturnTo::HOME);

// Clear transition data (called after reading to prevent stale data on next boot)
void clearTransition();

// Show a notification message on display before restart
// The e-ink display will retain this message during the reboot
void showTransitionNotification(const char* message);

}  // namespace papyrix
