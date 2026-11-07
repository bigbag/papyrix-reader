#include "ClockApp.h"
#include "Game2048App.h"
#include "GoApp.h"
#include "ImageViewerApp.h"
#include "LocalsendApp.h"
#include "MiniApp.h"
#include "PomodoroApp.h"
#include "PrinterApp.h"

namespace papyrix {

const MiniApp APPS[] = {
    {"Image Viewer", imageviewer_app::enter, imageviewer_app::update, imageviewer_app::handleEvent,
     imageviewer_app::render, imageviewer_app::exit},
    {"Printer", printer_app::enter, printer_app::update, printer_app::handleEvent, printer_app::render,
     printer_app::exit},
    {"LocalSend", localsend_app::enter, localsend_app::update, localsend_app::handleEvent, localsend_app::render,
     localsend_app::exit},
    {"Pomodoro", pomodoro_app::enter, pomodoro_app::update, pomodoro_app::handleEvent, pomodoro_app::render,
     pomodoro_app::exit, pomodoro_app::prepareExit, true, true},
    {"Clock", clock_app::enter, clock_app::update, clock_app::handleEvent, clock_app::render, clock_app::exit,
     clock_app::prepareExit},
    {"2048", game2048_app::enter, game2048_app::update, game2048_app::handleEvent, game2048_app::render,
     game2048_app::exit, game2048_app::prepareExit, false, true},
    {"Go", go_app::enter, go_app::update, go_app::handleEvent, go_app::render, go_app::exit, go_app::prepareExit, false,
     true},
};
const uint8_t APP_COUNT = sizeof(APPS) / sizeof(APPS[0]);
const int8_t APP_IMAGEVIEWER = 0;
const int8_t APP_PRINTER = 1;
const int8_t APP_LOCALSEND = 2;
const int8_t APP_POMODORO = 3;
const int8_t APP_CLOCK = 4;
const int8_t APP_2048 = 5;
const int8_t APP_GO = 6;

}  // namespace papyrix
