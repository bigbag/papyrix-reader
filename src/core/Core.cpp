#include "Core.h"

#include <Arduino.h>
#include <Logging.h>

#define TAG "CORE"

namespace papyrix {

Result<void> Core::init() {
  logMemory("Core::init start");

  // Storage first - needed for settings/themes
  TRY(storage.init());
  logMemory("Storage initialized");

  // Note: Settings are loaded earlier in setup() via loadFromFile()
  // before Core::init() is called (needed for theme/font setup)

  // Display
  TRY(display.init());
  logMemory("Display initialized");

  // Input - connects to event queue
  TRY(input.init(events));
  logMemory("Input initialized");

  // Network is NOT initialized here - lazy init when needed
  // WiFi fragments heap, so we only init when entering network states

  logMemory("Core::init complete");
  return Ok();
}

void Core::shutdown() {
  logMemory("Core::shutdown");

  // Shutdown in reverse order
  if (network.isInitialized()) {
    network.shutdown();
  }
  input.shutdown();
  display.shutdown();
  storage.shutdown();
}

uint32_t Core::freeHeap() const { return ESP.getFreeHeap(); }

void Core::logMemory(const char* label) const {
  LOG_DBG(TAG, "%s: free=%lu, largest=%lu", label, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

}  // namespace papyrix
