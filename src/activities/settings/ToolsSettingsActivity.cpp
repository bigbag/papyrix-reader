#include "ToolsSettingsActivity.h"

#include <cstring>

constexpr SettingInfo ToolsSettingsActivity::settings[];

#include "CrossPointSettings.h"
#include "StorageActivity.h"

void ToolsSettingsActivity::handleAction(const char* actionName) {
  if (std::strcmp(actionName, "Net Library") == 0) {
    SETTINGS.saveToFile();
    onOpdsLibraryOpen();
  } else if (std::strcmp(actionName, "Calibre Wireless") == 0) {
    SETTINGS.saveToFile();
    onCalibreWirelessOpen();
  } else if (std::strcmp(actionName, "File Transfer") == 0) {
    SETTINGS.saveToFile();
    onFileTransferOpen();
  } else if (std::strcmp(actionName, "Cleanup") == 0) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    exitActivity();
    enterNewActivity(new StorageActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    xSemaphoreGive(renderingMutex);
  }
}
