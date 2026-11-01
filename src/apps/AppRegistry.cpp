#include "ClockApp.h"
#include "Game2048App.h"
#include "ImageViewerApp.h"
#include "LocalsendApp.h"
#include "MiniApp.h"
#include "PomodoroApp.h"
#include "PrinterApp.h"

namespace papyrix {

const MiniApp APPS[] = {
    {"Image Viewer", imageviewer_app::enter, imageviewer_app::update, imageviewer_app::onButton,
     imageviewer_app::render, imageviewer_app::exit, imageviewer_app::renderMenu, imageviewer_app::onMenuButton},
    {"Printer", printer_app::enter, printer_app::update, printer_app::onButton, printer_app::render, printer_app::exit,
     nullptr, nullptr},
    {"LocalSend", localsend_app::enter, localsend_app::update, localsend_app::onButton, localsend_app::render,
     localsend_app::exit, nullptr, nullptr},
    {"Pomodoro", pomodoro_app::enter, pomodoro_app::update, pomodoro_app::onButton, pomodoro_app::render,
     pomodoro_app::exit, pomodoro_app::renderMenu, pomodoro_app::onMenuButton},
    {"Clock", clock_app::enter, clock_app::update, nullptr, clock_app::render, clock_app::exit, clock_app::renderMenu,
     clock_app::onMenuButton},
    {"2048", game2048_app::enter, game2048_app::update, nullptr, game2048_app::render, game2048_app::exit,
     game2048_app::renderMenu, nullptr},
};
const uint8_t APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
const int8_t APP_IMAGEVIEWER = 0;
const int8_t APP_PRINTER = 1;
const int8_t APP_LOCALSEND = 2;
const int8_t APP_POMODORO = 3;
const int8_t APP_CLOCK = 4;
const int8_t APP_2048 = 5;

}  // namespace papyrix
