#pragma once

#include <MiniApps.h>

#include <cstdint>

namespace papyrix {

struct Core;
struct Event;

using MiniApp = miniapps::App<Core, Event>;

// App registry - defined in AppRegistry.cpp
extern const MiniApp APPS[];
extern const uint8_t APP_COUNT;
extern const int8_t APP_IMAGEVIEWER;
extern const int8_t APP_CLOCK;
extern const int8_t APP_PRINTER;
extern const int8_t APP_LOCALSEND;
extern const int8_t APP_POMODORO;
extern const int8_t APP_2048;
extern const int8_t APP_GO;

}  // namespace papyrix
