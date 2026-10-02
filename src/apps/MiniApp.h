#pragma once

#include <cstdint>

#include "../core/Types.h"

namespace papyrix {

struct Core;

struct MiniApp {
  const char* name;
  void (*enter)(Core& core);
  bool (*update)(Core& core);
  void (*onButton)(Core& core, Button btn);
  bool (*render)(Core& core);  // Returns true if app handled display itself
  void (*exit)(Core& core);
  void (*renderMenu)(Core& core);
  void (*onMenuButton)(Core& core, Button btn);
};

// App registry - defined in AppRegistry.cpp
extern const MiniApp APPS[];
extern const uint8_t APP_COUNT;
extern const int8_t APP_IMAGEVIEWER;
extern const int8_t APP_CLOCK;
extern const int8_t APP_PRINTER;
extern const int8_t APP_LOCALSEND;
extern const int8_t APP_POMODORO;

}  // namespace papyrix
