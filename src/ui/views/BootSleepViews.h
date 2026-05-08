#pragma once

#include <GfxRenderer.h>
#include <Theme.h>

#include <cstdint>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// BootView - Boot splash screen with branding text and version
// ============================================================================

struct BootView {
  static constexpr int MAX_VERSION_LEN = 16;
  static constexpr int MAX_STATUS_LEN = 32;

  // Legacy logo fields retained for compatibility; renderers ignore them.
  const uint8_t* logoData = nullptr;
  int16_t logoWidth = 0;
  int16_t logoHeight = 0;

  char version[MAX_VERSION_LEN] = {0};
  char status[MAX_STATUS_LEN] = "Starting...";
  bool needsRender = true;

  void setLogo(const uint8_t* data, int w, int h) {
    logoData = data;
    logoWidth = static_cast<int16_t>(w);
    logoHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setVersion(const char* v) {
    strncpy(version, v, MAX_VERSION_LEN - 1);
    version[MAX_VERSION_LEN - 1] = '\0';
    needsRender = true;
  }

  void setStatus(const char* s) {
    strncpy(status, s, MAX_STATUS_LEN - 1);
    status[MAX_STATUS_LEN - 1] = '\0';
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const BootView& v);

// ============================================================================
// SleepView - Sleep screen with optional image
// ============================================================================

struct SleepView {
  enum class Mode : uint8_t {
    Logo,       // Show branding text
    BookCover,  // Show current book cover
    Black,      // Black screen
    Custom      // Custom image
  };

  // Legacy logo fields retained for compatibility; renderers ignore them.
  const uint8_t* logoData = nullptr;
  int16_t logoWidth = 0;
  int16_t logoHeight = 0;

  // External image pointer (not owned) - for BookCover/Custom modes
  const uint8_t* imageData = nullptr;
  int16_t imageWidth = 0;
  int16_t imageHeight = 0;

  Mode mode = Mode::Logo;
  bool darkMode = false;
  bool needsRender = true;

  void setMode(Mode m) {
    mode = m;
    needsRender = true;
  }

  void setLogo(const uint8_t* data, int w, int h) {
    logoData = data;
    logoWidth = static_cast<int16_t>(w);
    logoHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setImage(const uint8_t* data, int w, int h) {
    imageData = data;
    imageWidth = static_cast<int16_t>(w);
    imageHeight = static_cast<int16_t>(h);
    needsRender = true;
  }

  void setDarkMode(bool dark) {
    darkMode = dark;
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const SleepView& v);

}  // namespace ui
