#pragma once

#include <cstdint>

namespace papyrix::eink {

enum class DisplayController : uint8_t { SSD1677 = 0, UC8253 = 2, UC8279_X3, UC8179_X4PRO, UC8279_X4PRO, SSD2677 = 6 };

inline const char* displayControllerName(DisplayController controller) {
  switch (controller) {
    case DisplayController::SSD1677:
      return "SSD1677";
    case DisplayController::UC8253:
      return "UC8253";
    case DisplayController::UC8279_X3:
      return "UC8279_X3";
    case DisplayController::UC8179_X4PRO:
      return "UC8179_X4PRO";
    case DisplayController::UC8279_X4PRO:
      return "UC8279_X4PRO";
    case DisplayController::SSD2677:
      return "SSD2677";
  }
  return "unknown";
}

}  // namespace papyrix::eink
