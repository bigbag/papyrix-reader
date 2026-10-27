#include "Core.h"

#include <Arduino.h>
#include <Logging.h>

#define TAG "CORE"

namespace papyrix {

Result<void> Core::init() {
  logMemory("Core::init start");
  battery.init();
  usb.init(battery);
  logMemory("Battery and USB initialized");
  frontLight.init(settings.smoothFrontLight != 0);
  logMemory("Front light initialized");

  clock.init();
  logMemory("Clock initialized");

  // Storage first - needed for settings/themes
  TRY(storage.init());
  logMemory("Storage initialized");

  // Note: Settings are loaded earlier in setup() via loadFromFile()
  // before Core::init() is called (needed for theme/font setup)

  // Input - connects to event queue
  TRY(input.init(events));
  logMemory("Input initialized");

  logMemory("Core::init complete");
  return Ok();
}

void Core::shutdown() {
  logMemory("Core::shutdown");

  if (wifi.isInitialized()) {
    wifi.shutdown();
  }
  frontLight.shutdown(false);
  input.shutdown();
  if (!display.deepSleep()) LOG_ERR("CORE", "Display power-off failed during shutdown");
  storage.shutdown();
}

uint32_t Core::freeHeap() const { return ESP.getFreeHeap(); }

void Core::logMemory(const char* label) const {
  LOG_DBG(TAG, "%s: free=%lu, largest=%lu", label, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

}  // namespace papyrix
