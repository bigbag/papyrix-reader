#include "ErrorState.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstring>

#include "../core/Core.h"
#include "ThemeManager.h"

#define TAG "ERROR"

namespace papyrix {

ErrorState::ErrorState(GfxRenderer& renderer) : renderer_(renderer), needsRender_(true) {}

void ErrorState::setError(Error err, const char* message) {
  error_ = err;
  if (message) {
    strncpy(message_, message, sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
  } else {
    strncpy(message_, errorToString(err), sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
  }
  needsRender_ = true;
}

void ErrorState::enter(Core& core) {
  // Check for error message from shared buffer (e.g., from ReaderState)
  if (core.buf.text[0] != '\0') {
    strncpy(message_, core.buf.text, sizeof(message_) - 1);
    message_[sizeof(message_) - 1] = '\0';
    core.buf.text[0] = '\0';  // Clear after reading
  }
  LOG_INF(TAG, "Entering - %s", message_);
  needsRender_ = true;
}

void ErrorState::exit(Core& core) { LOG_INF(TAG, "Exiting"); }

StateTransition ErrorState::update(Core& core) {
  // Process events
  Event e;
  while (core.events.pop(e)) {
    if (e.type == EventType::ButtonPress) {
      // Any button press goes back to file list
      return StateTransition::to(StateId::FileList);
    }
  }

  return StateTransition::stay(StateId::Error);
}

void ErrorState::render(Core& core) {
  if (!needsRender_) {
    return;
  }

  const Theme& theme = THEME_MANAGER.current();

  renderer_.clearScreen(theme.backgroundColor);

  // Error title
  renderer_.drawCenteredText(theme.readerFontId, 100, "Error", theme.primaryTextBlack, BOLD);

  // Error message
  renderer_.drawCenteredText(theme.uiFontId, 200, message_, theme.primaryTextBlack, REGULAR);

  // Instructions
  renderer_.drawCenteredText(theme.uiFontId, 350, "Press any button to continue", theme.primaryTextBlack, REGULAR);

  renderer_.displayBuffer();
  needsRender_ = false;
  core.display.markDirty();
}

}  // namespace papyrix
