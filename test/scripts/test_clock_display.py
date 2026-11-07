#!/usr/bin/env python3
"""Exercise Clock rendering and panel power recovery through the real display facade."""

import os
from pathlib import Path
import subprocess
import tempfile

from test_clock_app import block

ROOT = Path(__file__).resolve().parents[2]
SOURCES = [
    "lib/EInkDisplay/src/Display.cpp",
    "lib/EInkDisplay/src/Uc8279X3Driver.cpp",
    "lib/EInkDisplay/src/Uc8179X4ProDriver.cpp",
    "lib/EInkDisplay/src/Uc8279X4ProDriver.cpp",
    "lib/EInkDisplay/src/Uc8279SpiBus.cpp",
    "lib/EInkDisplay/src/Ssd2677Driver.cpp",
    "lib/BoardSupport/src/MurphyM4Batch.cpp",
    "lib/BoardSupport/src/HardwareIdentity.cpp",
    "lib/BoardSupport/src/BoardProfiles.cpp",
    "test/mocks/platform_stubs.cpp",
]
INCLUDES = [
    "lib/EInkDisplay/include",
    "test/mocks",
    "lib/BoardSupport/include",
    "lib/BatteryMonitor/include",
    "lib/EpdFont/src",
    "src",
    "lib/MiniApps/src",
]
TARGETS = {
    "PAPYRIX_TARGET_XTEINK_C3": ("ssd1677", "uc8253", "uc8279-x3"),
    "PAPYRIX_TARGET_X4PRO": ("uc8179-pro", "uc8279-pro"),
    "PAPYRIX_TARGET_X4CLASSIC": ("ssd1677", "uc8179-pro", "uc8279-pro"),
    "PAPYRIX_TARGET_STICKY": ("ssd1677", "ssd2677"),
    "PAPYRIX_TARGET_MURPHY_M4": ("ssd1677",),
}

HARNESS = r'''
#include <Display.h>
#include <HardwareIdentity.h>
#include <SPI.h>
#include <EpdFontFamily.h>
#include <apps/MiniApp.h>
#include <core/EventQueue.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using papyrix::hal::Display;
using papyrix::eink::DisplayController;
DisplayController panel;
int dcPin, csPin, busyPin;
int dc = HIGH, cs = HIGH;
unsigned busyReads = 2;
bool waiting = false;
bool busyStuck = false;

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

struct Command {
  uint8_t code;
  std::vector<uint8_t> data;
};
std::vector<Command> commands;

void recordPin(int pin, int value) {
  if (pin == dcPin) dc = value;
  if (pin == csPin) cs = value;
}

void recordSpi(const uint8_t* data, size_t length) {
  check(cs == LOW, "SPI write requires chip selection");
  if (dc == LOW) {
    check(length == 1, "command must contain one byte");
    commands.push_back({data[0], {}});
    const bool packed = panel == DisplayController::SSD2677;
    if (panel == DisplayController::SSD1677 ? data[0] == 0x20
        : data[0] == 0x04 || data[0] == 0x12 || data[0] == 0x02 ||
          (packed && (data[0] == 0x00 || data[0] == 0xE9 || data[0] == 0xA5))) {
      check(!waiting, "new operation starts before BUSY completes");
      busyReads = 0;
      waiting = true;
    }
  } else {
    check(!commands.empty(), "data requires a preceding command");
    commands.back().data.insert(commands.back().data.end(), data, data + length);
  }
}

int readBusy(int pin) {
  check(pin == busyPin, "display reads its selected BUSY pin");
  if (busyStuck) return panel == DisplayController::SSD1677 ? HIGH : LOW;
  const bool active = busyReads++ < 2;
  if (!active) waiting = false;
  return panel == DisplayController::SSD1677 ? (active ? HIGH : LOW) : (active ? LOW : HIGH);
}

size_t commandCount(uint8_t code) {
  return std::count_if(commands.begin(), commands.end(), [=](const Command& cmd) { return cmd.code == code; });
}

size_t lastCommand(uint8_t code) {
  for (size_t i = commands.size(); i > 0; --i) {
    if (commands[i - 1].code == code) return i - 1;
  }
  throw std::runtime_error("missing command " + std::to_string(code));
}

const std::vector<uint8_t>& lastData(uint8_t code) { return commands[lastCommand(code)].data; }

namespace papyrix::board {
void suppressTouchUntilIdle() {}
bool pauseTouchForDisplayReset() { return true; }
bool resumeTouchAfterDisplayReset() { return true; }
}

namespace papyrix { void paintStatusFrame(); }
class GfxRenderer {
 public:
  Display& display_;
  bool darkBackground_ = false;
  void logRenderTime() const {}
  void clearScreen(uint8_t color) { display_.clearScreen(color); }
  int getScreenHeight() const { return display_.getDisplayHeight(); }
  int getScreenWidth() const { return display_.getDisplayWidth(); }
  void clearArea(int, int, int, int, uint8_t) {}
  int getLineHeight(int) const { return 24; }
  void drawCenteredText(int, int, const char*, bool, EpdFontFamily::Style) const { papyrix::paintStatusFrame(); }
  void displayBuffer(Display::RefreshMode mode = Display::FAST_REFRESH, bool turnOffScreen = false) const;
};

#define TAG "GFX"
@GFX_DISPLAY@
#undef TAG

void checkClockWait();
unsigned statusFrames = 0, syncAttempts = 0;

namespace papyrix::hal {
struct Cpu {
  class PerformanceLock {
   public:
    explicit PerformanceLock(Cpu&) {}
  };
  void unthrottle() {}
};
struct WifiRadio {
  bool connected = false, succeeds = true;
  bool isConnected() const { return connected; }
  struct Result { bool connected; bool ok() const { return connected; } };
  Result connect(const char*, const char*, uint32_t, bool (*)()) {
    if (statusFrames) checkClockWait();
    connected = succeeds;
    return {connected};
  }
  void shutdown() { connected = false; }
};
@WIFI_SESSION@
}
namespace papyrix {
static constexpr const char* TAG = "CLOCK-DISPLAY-TEST";
struct Core {
  Display& display;
  uint8_t seed = 0;
  bool ownsDisplay = false;
  std::vector<uint8_t> frame;
  hal::Cpu cpu;
  hal::WifiRadio wifi;
  struct Clock {
    bool localTime(std::tm&) const { return false; }
    bool updateFromSystem() { return true; }
    bool canRetainTime() const { return true; }
  } clock;
  SyncMode pendingSync = SyncMode::None;
  int pendingAppId = -1;
};

void paint(Core& core) {
  core.frame.resize(core.display.getBufferSize());
  const auto width = core.display.getDisplayWidthBytes();
  for (uint16_t y = 0; y < core.display.getDisplayHeight(); ++y) {
    for (uint16_t x = 0; x < width; ++x) {
      core.frame[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(core.seed + y + 3 * x);
    }
  }
  std::copy(core.frame.begin(), core.frame.end(), core.display.getFrameBuffer());
}

bool renderApp(Core& core) {
  paint(core);
  if (core.ownsDisplay) core.display.displayBuffer(Display::FAST_REFRESH, false);
  return core.ownsDisplay;
}

struct Theme { uint8_t backgroundColor = 0xFF; int uiFontId = 0; bool primaryTextBlack = true; } theme;
Core* statusCore = nullptr;
#define THEME theme
void paintStatusFrame() {
  if (statusFrames) checkClockWait();
  commands.clear();
  ++statusFrames;
  ++statusCore->seed;
  paint(*statusCore);
}
constexpr int BACK = 0, CONFIRM = 1;
const char* tr(int key) { return key == CONFIRM ? "Confirm" : "Back"; }

namespace ui {
std::string confirmHint;
struct AppMenuView { bool needsRender = false; };
struct ButtonBar {
  const char* confirm;
  ButtonBar(const char*, const char* label, const char*, const char*) : confirm(label) {}
};
void buttonBar(GfxRenderer&, const decltype(theme)&, const ButtonBar& buttons) { confirmHint = buttons.confirm; }
void render(GfxRenderer&, const decltype(theme)&, AppMenuView&, const MiniApp*) {}
}

namespace clock_app {
GfxRenderer* clockRenderer = nullptr;
#define renderer (*clockRenderer)
enum class PowerMode { Normal, PowerSaving };
struct ClockSettings { int utcOffset = 0, ntpSyncSetting = 0; char ntpServers[128]{}; };
constexpr uint32_t NTP_SYNC_SECONDS[] = {10800, 21600, 86400, 0};
constexpr int64_t kValidTime = 946684800;
@NEXT_SYNC@
@SYNC_RESULT@;
@NTP_CONSTANTS@
struct : ClockSettings {
  int lastRenderedMin, menuSelected;
  PowerMode powerMode = PowerMode::Normal;
  int64_t nextNtpUtc = 0;
  uint32_t interactionStartedMs = 0;
} state;
struct {
  struct Credential { const char* ssid = "home"; const char* password = "secret"; } creds[2];
  int count = 1;
  bool loadFromFile() { return true; }
  int getCount() const { return count; }
  const Credential* getCredentials() const { return creds; }
  bool promoteCredential(const char* ssid) {
    for (int i = 0; i < count; ++i) {
      if (std::strcmp(creds[i].ssid, ssid) != 0) continue;
      std::rotate(creds, creds + i, creds + i + 1);
      return true;
    }
    return false;
  }
} credentialStore;
#define WIFI_STORE credentialStore
void loadSettings(Core&) {}
void applyTimezone(int) {}
bool powerWakeRequested() { return false; }
void delay(unsigned long ms) { if (statusFrames) checkClockWait(); ::delay(ms); }
constexpr int SNTP_SYNC_STATUS_COMPLETED = 1;
int esp_sntp_get_sync_status() { return SNTP_SYNC_STATUS_COMPLETED; }
void esp_sntp_stop() {}
void configTime(long, int, const char*, const char*, const char*) {
  if (statusFrames) checkClockWait();
  ++syncAttempts;
}
@PARSE_NTP@
@SYNC_MESSAGE@
@SYNCHRONIZE@
@CLOCK_ENTER@
bool update(Core&) { return false; }
miniapps::Action handleEvent(Core&, const Event& event, bool menuOpen) {
  if (event.type != EventType::ButtonPress) return miniapps::Action::None;
  if (event.button == Button::Back) return menuOpen ? miniapps::Action::CloseMenu : miniapps::Action::Exit;
  if (event.button == Button::Center) return menuOpen ? miniapps::Action::None : miniapps::Action::OpenMenu;
  return menuOpen ? miniapps::Action::Redraw : miniapps::Action::None;
}
Core* captureFrame(Core& core, int64_t, bool) { return &core; }
void renderFrame(GfxRenderer&, Core* core) { paint(*core); }
static void renderMenu(Core& core) {
  paint(core);
  ui::confirmHint.clear();
}
@CLOCK_RENDER@
miniapps::ExitResult prepareExit(Core&, miniapps::ExitTarget) { return miniapps::ExitResult::Ready; }
void exit(Core&) {}
#undef renderer
}
namespace imageviewer_app {
void enter(Core&) {}
bool update(Core&) { return false; }
miniapps::Action handleEvent(Core&, const Event& event, bool menuOpen) {
  if (event.type != EventType::ButtonPress) return miniapps::Action::None;
  if (event.button == Button::Center && !menuOpen) return miniapps::Action::OpenMenu;
  if (event.button == Button::Back) return menuOpen ? miniapps::Action::CloseMenu : miniapps::Action::Exit;
  return miniapps::Action::Redraw;
}
static bool renderImage() { return renderApp(*statusCore); }
static void renderMenu(Core& core) {
  paint(core);
  ui::confirmHint = "Confirm";
}
@IMAGE_RENDER@
void exit(Core&) {}
}
namespace printer_app {
void enter(Core&) {}
bool update(Core&) { return false; }
miniapps::Action handleEvent(Core&, const Event&, bool) { return miniapps::Action::None; }
miniapps::Frame render(Core& core, bool) {
  return {renderApp(core) ? miniapps::Refresh::None : miniapps::Refresh::Fast, false};
}
void exit(Core&) {}
}
namespace localsend_app {
void enter(Core&) {}
bool update(Core&) { return false; }
miniapps::Action handleEvent(Core&, const Event&, bool) { return miniapps::Action::None; }
miniapps::Frame render(Core& core, bool) {
  return {renderApp(core) ? miniapps::Refresh::None : miniapps::Refresh::Fast, false};
}
void exit(Core&) {}
}
namespace pomodoro_app {
void enter(Core&) {}
bool update(Core&) { return false; }
miniapps::Action handleEvent(Core&, const Event&, bool) { return miniapps::Action::None; }
miniapps::Frame render(Core& core, bool) {
  return {renderApp(core) ? miniapps::Refresh::None : miniapps::Refresh::Fast, true};
}
miniapps::ExitResult prepareExit(Core&, miniapps::ExitTarget) { return miniapps::ExitResult::Ready; }
void exit(Core&) {}
}
namespace game2048_app {
void enter(Core&) { std::abort(); }
bool update(Core&) { std::abort(); }
miniapps::Action handleEvent(Core&, const Event&, bool) { std::abort(); }
miniapps::Frame render(Core&, bool) { std::abort(); }
miniapps::ExitResult prepareExit(Core&, miniapps::ExitTarget) { std::abort(); }
void exit(Core&) { std::abort(); }
}
namespace go_app {
void enter(Core&) { std::abort(); }
bool update(Core&) { std::abort(); }
miniapps::Action handleEvent(Core&, const Event&, bool) { std::abort(); }
miniapps::Frame render(Core&, bool) { std::abort(); }
miniapps::ExitResult prepareExit(Core&, miniapps::ExitTarget) { std::abort(); }
void exit(Core&) { std::abort(); }
}
@REGISTRY@

class AppLauncherState {
 public:
  GfxRenderer& renderer_;
  miniapps::Host<Core, Event> app_;
  bool goNetwork_ = false;
  ui::AppMenuView menuView_;
  void render(Core& core);
  void press(Core& core, Button button, bool repeat = false) {
    app_.handleEvent(core, repeat ? Event::buttonRepeat(button) : Event::buttonPress(button));
  }
};

@LAUNCHER_RENDER@
}

std::vector<uint8_t> unpackPlane(const std::vector<uint8_t>& packed, bool old) {
  check(packed.size() == 96000, "packed plane covers the physical panel");
  std::vector<uint8_t> result(48000);
  for (size_t i = 0; i < result.size(); ++i) {
    for (unsigned bit = 0; bit < 8; ++bit) {
      const unsigned shift = 6 - 2 * (bit % 4) + (old ? 1 : 0);
      result[i] |= ((packed[2 * i + bit / 4] >> shift) & 1) << (7 - bit);
    }
  }
  return result;
}

void checkFrame(const papyrix::Core& core, bool turnOff, bool cold) {
  check(!waiting, "refresh must wait for BUSY completion");
  std::vector<uint8_t> expected;
  if (panel == DisplayController::UC8279_X4PRO) expected.assign(12000, 0xFF);
  const bool reversed = panel != DisplayController::SSD1677 && panel != DisplayController::UC8279_X4PRO;
  const size_t width = core.display.getDisplayWidthBytes();
  const size_t height = core.display.getDisplayHeight();
  for (size_t y = 0; y < height; ++y) {
    const size_t row = reversed ? height - 1 - y : y;
    expected.insert(expected.end(), core.frame.begin() + row * width, core.frame.begin() + (row + 1) * width);
  }
  if (panel == DisplayController::UC8179_X4PRO) expected.insert(expected.end(), 12000, 0xFF);

  if (panel == DisplayController::SSD1677) {
    check(commandCount(0x10) == 0, "Clock must not put SSD1677 into deep sleep");
    const unsigned updates = PAPYRIX_TARGET_MURPHY_M4 && turnOff && !cold ? 2 : 1;
    check(commandCount(0x20) == updates, "one SSD1677 refresh and any separate power-off complete");
    check(lastData(0x24) == expected, "SSD1677 current plane contains the submitted frame");
    check(lastData(0x26) == expected, "SSD1677 baseline contains the submitted frame");
    check(lastData(0x21) == std::vector<uint8_t>{static_cast<uint8_t>(cold ? 0x40 : 0x00)},
          "SSD1677 uses FAST once the previous frame is valid");
    if (turnOff || PAPYRIX_TARGET_STICKY || (PAPYRIX_TARGET_MURPHY_M4 && cold)) {
      check(lastData(0x22).size() == 1 && (lastData(0x22)[0] & 0x03) == 0x03,
            "Clock face powers SSD1677 off");
    } else {
      check(lastData(0x22).size() == 1 && (lastData(0x22)[0] & 0x03) == 0,
            "menus and other apps keep SSD1677 powered");
    }
    return;
  }

  if (panel == DisplayController::SSD2677) {
    check(commandCount(0x12) == 1, "one packed-panel refresh completes");
    check(unpackPlane(lastData(0x10), false) == expected, "packed current pixels match the frame");
    if (cold) check(unpackPlane(lastData(0x10), true) == expected, "full refresh establishes a complete packed baseline");
    check(lastData(0xE6) == std::vector<uint8_t>{static_cast<uint8_t>(cold ? 0xEE : 0x19)},
          "packed refresh selects its full or differential waveform");
    check(commandCount(0x02) == (turnOff || cold ? 1 : 0), "packed panel follows its power-off policy");
    return;
  }

  check(commandCount(0x07) == 0, "Clock must not put UC controllers into deep sleep");
  if (!cold) check(commandCount(0x12) == 1, "launcher must not submit a duplicate UC refresh");
  check(lastData(0x13) == expected, "UC current plane contains the submitted frame");
  check(lastData(0x10) == expected, "UC old plane contains the submitted frame");
  if (panel == DisplayController::UC8279_X4PRO) {
    check(lastData(0x50) == std::vector<uint8_t>{static_cast<uint8_t>(cold ? 0x97 : 0xD7)} &&
          lastData(0xE5) == std::vector<uint8_t>{static_cast<uint8_t>(cold ? 0x1E : 0x5A)},
          "UC8279 uses a differential waveform with a valid previous image");
    if (!cold) {
      check(lastCommand(0x91) < lastCommand(0x12) && lastCommand(0x12) < lastCommand(0x92) &&
            lastData(0x90) == std::vector<uint8_t>({0, 0, 3, 0x1F, 0, 0x78, 2, 0x57, 1}),
            "UC8279 differential refresh covers the visible 120-offset gate window");
    }
  }
  check(commandCount(0x02) == (turnOff ? 1 : 0), "only Clock requests UC panel power-off");
  if (turnOff) {
    check(lastCommand(0x12) < lastCommand(0x02), "UC panel powers off after refresh");
    if (panel == DisplayController::UC8253) {
      if (!cold) {
        check(lastCommand(0x02) < lastCommand(0x10), "UC8253 restores RED RAM after power-off");
        check(lastData(0x50) == std::vector<uint8_t>({0x29, 0x07}), "UC8253 retains FAST after power-off");
      }
    } else {
      check(lastCommand(0x12) < lastCommand(0x10) && lastCommand(0x10) < lastCommand(0x02),
            "UC old plane is synchronized between refresh and power-off");
    }
  }
}

void checkClockWait() {
  check(statusFrames > 0, "Clock displays status before waiting");
  checkFrame(*papyrix::statusCore, true, statusFrames == 1);
}

int main(int argc, char** argv) {
  try {
    check(argc == 3, "expected controller and app/overlay scenario");
    using namespace papyrix;
    using namespace papyrix::board;
    const std::string name = argv[1];
    BoardSelection selection;
#if PAPYRIX_TARGET_XTEINK_C3
    check(name == "ssd1677" || name == "uc8253" || name == "uc8279-x3", "unsupported C3 controller");
    selection.board = name == "ssd1677" ? BoardId::X4 : BoardId::X3;
    panel = name == "ssd1677" ? DisplayController::SSD1677
          : name == "uc8253" ? DisplayController::UC8253 : DisplayController::UC8279_X3;
#elif PAPYRIX_TARGET_X4CLASSIC
    check(name == "ssd1677" || name == "uc8179-pro" || name == "uc8279-pro", "unsupported Classic controller");
    selection.board = BoardId::X4Classic;
    panel = name == "ssd1677" ? DisplayController::SSD1677
          : name == "uc8179-pro" ? DisplayController::UC8179_X4PRO : DisplayController::UC8279_X4PRO;
#elif PAPYRIX_TARGET_STICKY
    check(name == "ssd1677" || name == "ssd2677", "unsupported Sticky controller");
    selection.board = BoardId::Sticky;
    panel = name == "ssd1677" ? DisplayController::SSD1677 : DisplayController::SSD2677;
#elif PAPYRIX_TARGET_MURPHY_M4
    check(name == "ssd1677", "unsupported M4 controller");
    selection.board = BoardId::MurphyM4;
    panel = DisplayController::SSD1677;
#else
    check(name == "uc8179-pro" || name == "uc8279-pro", "unsupported Pro controller");
    selection.board = BoardId::X4Pro;
    panel = name == "uc8179-pro" ? DisplayController::UC8179_X4PRO : DisplayController::UC8279_X4PRO;
#endif
    auto& identity = HardwareIdentity::instance();
    selection.source = BoardSelectionSource::Override;
    check(identity.applyBoardSelection(selection, {}), "board profile must be available");
    identity.setPanel(panel, PanelSelectionSource::Override, 0x68);
    dcPin = identity.profile().display.dc;
    csPin = identity.profile().display.cs;
    busyPin = identity.profile().display.busy;
    testSetManualMillis(100);
    testSetDigitalWriteHook(recordPin);
    testSetSpiWriteHook(recordSpi);
    testSetDigitalReadHook(readBusy);
    Display display;
    check(display.begin() == Display::InitResult::Ok, "real facade initializes the selected panel");
    std::unique_ptr<uint8_t, decltype(&std::free)> buffer(nullptr, std::free);
#if !PAPYRIX_TARGET_XTEINK_C3
    buffer.reset(display.getFrameBuffer());
#endif
    GfxRenderer renderer{display};
    Core core{display};
    AppLauncherState launcher{renderer};
    const std::string scenario = argv[2];
    statusCore = &core;
    clock_app::clockRenderer = &renderer;
    if (scenario == "resume") {
      paint(core);
      display.displayBuffer(Display::FULL_REFRESH, true);
      check(display.lastRefreshSucceeded(), "the previous image completes");
      const auto previous = core.frame;
      check(display.deepSleep(), "the previous image reaches panel standby");
      Display resumed;
      check(resumed.begin() == Display::InitResult::Ok, "a new MCU session initializes the panel");
      std::unique_ptr<uint8_t, decltype(&std::free)> resumedBuffer(nullptr, std::free);
#if !PAPYRIX_TARGET_XTEINK_C3
      resumedBuffer.reset(resumed.getFrameBuffer());
#endif
      std::copy(previous.begin(), previous.end(), resumed.getFrameBuffer());
      commands.clear();
      const auto baseline = resumed.restoreMonochromeBaseline();
      const bool full = panel != DisplayController::SSD1677 && panel != DisplayController::SSD2677 &&
                        panel != DisplayController::UC8279_X4PRO && panel != DisplayController::UC8253;
      check(baseline != Display::BaselineRestoreResult::Error, "the previous image is available for resume");
      if (panel == DisplayController::UC8253) {
        check(baseline == Display::BaselineRestoreResult::Ready, "UC8253 resumes without a full clear");
        check(commandCount(0x12) == 0, "UC8253 reference restoration does not refresh the screen");
        std::vector<uint8_t> expected;
        for (size_t row = 0; row < 528; ++row) {
          expected.insert(expected.end(), previous.begin() + (527 - row) * 99,
                          previous.begin() + (528 - row) * 99);
        }
        check(lastData(0x10) == expected && lastData(0x13) == expected,
              "UC8253 restores both reference planes with the retained row order and polarity");
      }
      if (panel == DisplayController::UC8279_X4PRO) {
        std::vector<uint8_t> expected(12000, 0xFF);
        expected.insert(expected.end(), previous.begin(), previous.end());
        check(commandCount(0x12) == 0, "UC8279 baseline restoration must not flash the panel");
        check(commandCount(0x10) == 1 && lastData(0x10) == expected,
              "UC8279 restores the retained image with white padding before the visible gates");
      }
      if (panel == DisplayController::SSD1677) {
        check(lastData(0x24) == previous && lastData(0x26) == previous, "resume restores both SSD1677 reference planes");
      }
      Core next{resumed};
      next.seed = 37;
      paint(next);
      commands.clear();
      resumed.displayBuffer(baseline == Display::BaselineRestoreResult::Ready ? Display::FAST_REFRESH
                                                                            : Display::FULL_REFRESH, true);
      check(resumed.lastRefreshSucceeded(), "the next minute completes after MCU reset");
      checkFrame(next, true, full);
      if (panel == DisplayController::SSD2677) {
        const auto old = unpackPlane(lastData(0x10), true);
        for (size_t row = 0; row < 480; ++row) {
          check(std::equal(old.begin() + row * 100, old.begin() + (row + 1) * 100,
                           previous.begin() + (479 - row) * 100), "packed resume compares against the retained image");
        }
      }
      busyStuck = true;
      commands.clear();
      resumed.displayBuffer(Display::FAST_REFRESH, true);
      check(!resumed.lastRefreshSucceeded(), "BUSY timeout cannot retain a successful result");
      busyStuck = false;
      busyReads = 2;
      waiting = false;
      ++next.seed;
      paint(next);
      commands.clear();
      resumed.displayBuffer(Display::FAST_REFRESH, true);
      check(resumed.lastRefreshSucceeded(), "the panel recovers after BUSY failure");
      checkFrame(next, true, true);
      if (panel == DisplayController::UC8279_X4PRO || panel == DisplayController::UC8253) {
        check(resumed.deepSleep(), "the panel returns to sleep after recovery");
        check(resumed.begin() == Display::InitResult::Ok, "the next wake resets the panel");
        paint(next);
        busyStuck = true;
        commands.clear();
        check(resumed.restoreMonochromeBaseline() == Display::BaselineRestoreResult::Error,
              "a baseline BUSY timeout prevents differential resume");
        check(commandCount(0x12) == 0, "failed restoration does not activate the panel");
        busyStuck = false;
        busyReads = 2;
        waiting = false;
        commands.clear();
        resumed.displayBuffer(Display::FAST_REFRESH, true);
        check(resumed.lastRefreshSucceeded(), "a full refresh recovers the missing baseline");
        checkFrame(next, true, true);
      }
    } else if (scenario == "retention") {
      check(panel == DisplayController::SSD1677, "retention scenario requires SSD1677");
      auto frame = [&](bool fast) {
        ++core.seed;
        paint(core);
        commands.clear();
        display.displayBuffer(Display::FAST_REFRESH, true);
        checkFrame(core, true, !fast);
        if (fast) {
          check(lastData(0x22) == std::vector<uint8_t>{0xDF},
                "retained FAST powers the panel on and off");
          check(lastCommand(0x20) < lastCommand(0x26),
                "FAST preserves the old plane until activation completes");
        }
      };
      frame(false);
      frame(true);
      check(display.deepSleep(), "SSD1677 enters deep sleep");
      check(lastData(0x10) == std::vector<uint8_t>{0x01}, "deep sleep reaches the controller");
      check(display.begin() == Display::InitResult::Ok, "SSD1677 initializes after sleep");
      frame(false);
      frame(true);

      display.copyGrayscaleBuffers(core.frame.data(), core.frame.data());
      display.displayGrayBuffer(true);
      display.cleanupGrayscaleBuffers(core.frame.data());
      frame(true);
      display.copyGrayscaleMsbBuffers(core.frame.data());
      frame(false);

      commands.clear();
      display.displayWindow(0, 0, 8, 8, true);
      check(lastData(0x21) == std::vector<uint8_t>{0x00}, "retained window remains differential");
      check(lastData(0x22) == std::vector<uint8_t>{0xDF}, "window preserves power-off policy");
      frame(true);

      busyStuck = true;
      commands.clear();
      display.displayBuffer(Display::FAST_REFRESH, true);
      check(commandCount(0x26) == 0, "failed refresh does not overwrite the old plane");
      busyStuck = false;
      busyReads = 2;
      waiting = false;
      frame(false);
      frame(true);

      display.copyGrayscaleBuffers(core.frame.data(), core.frame.data());
      busyStuck = true;
      display.displayGrayBuffer(true);
      busyStuck = false;
      busyReads = 2;
      waiting = false;
      display.cleanupGrayscaleBuffers(core.frame.data());
      commands.clear();
      display.displayWindow(0, 0, 8, 8, true);
      checkFrame(core, true, true);
      frame(true);

      display.displayBuffer(Display::FAST_REFRESH);
      ++core.seed;
      paint(core);
      commands.clear();
      display.displayBufferDriveAll(true);
      checkFrame(core, true, false);
      frame(true);

      check(display.recover() == Display::InitResult::Ok, "recovery resets the controller");
      display.cleanupGrayscaleBuffers(core.frame.data());
      frame(false);
      display.refreshDisplay(Display::FAST_REFRESH, true);
      frame(false);
    } else if (scenario.rfind("status-", 0) == 0) {
      core.wifi.connected = scenario == "status-connected";
      core.wifi.succeeds = scenario != "status-failed";
      clock_app::credentialStore.count = scenario == "status-missing" ? 0 : 2;
      commands.clear();
      if (scenario == "status-auto") clock_app::synchronize(core, clock_app::state, false);
      else clock_app::enter(core);
      if (scenario == "status-missing" || scenario == "status-auto") {
        check(statusFrames == 0 && commands.empty(), "automatic sync and missing credentials preserve the Clock display");
      } else {
        checkClockWait();
        check(statusFrames == (scenario == "status-failed" ? 2u : 1u), "Clock displays each status only once");
      }
      const bool failed = scenario == "status-failed" || scenario == "status-missing";
      check(syncAttempts == (failed ? 0u : 1u), "only a connected radio starts NTP");
      check(!core.wifi.connected, "Clock consumes inherited and acquired radio ownership");
    } else {
    check(scenario == "app" || scenario == "overlay" || scenario == "keys", "unsupported render scenario");
    launcher.app_.restore(APPS[APP_CLOCK]);
    if (scenario == "overlay") launcher.press(core, Button::Center);
    for (int frame = 0; frame < 6; ++frame) {
      renderer.darkBackground_ = frame >= 3;
      theme.backgroundColor = renderer.darkBackground_ ? 0x00 : 0xFF;
      core.seed = static_cast<uint8_t>(17 + frame * 37);
      commands.clear();
      launcher.app_.invalidate();
      launcher.render(core);
      checkFrame(core, scenario != "overlay", frame == 0);
      commands.clear();
      launcher.render(core);
      check(commands.empty(), "clean Clock frame must not refresh");
    }
    if (scenario == "keys") {
      for (const Button button : {Button::Up, Button::Down, Button::Left, Button::Right}) {
        commands.clear();
        launcher.press(core, button);
        launcher.render(core);
        check(commands.empty(), "unsupported Clock key must not submit a frame");
      }
      launcher.press(core, Button::Center, true);
      check(!launcher.app_.menuOpen(), "repeat must not open the menu");
      launcher.press(core, Button::Center);
      check(launcher.app_.menuOpen(), "Center opens the Clock menu");
      commands.clear();
      launcher.render(core);
      checkFrame(core, false, false);
      check(ui::confirmHint.empty(), "Clock settings have no Confirm action");
      commands.clear();
      launcher.press(core, Button::Center);
      launcher.render(core);
      check(commands.empty(), "Center in Clock settings must not refresh the display");
      commands.clear();
      launcher.press(core, Button::Down);
      launcher.render(core);
      checkFrame(core, false, false);
      launcher.press(core, Button::Back);
      check(!launcher.app_.menuOpen(), "Back closes the Clock menu");
      commands.clear();
      launcher.render(core);
      checkFrame(core, true, false);
      launcher.press(core, Button::Back);
      check(!launcher.app_.active(),
            "Back leaves the Clock face");
    }
    if (launcher.app_.active()) launcher.app_.stop(core);
    launcher.app_.restore(APPS[APP_IMAGEVIEWER]);
    for (int frame = 0; frame < 3; ++frame) {
      if (frame == 2) launcher.press(core, Button::Center);
      ++core.seed;
      commands.clear();
      launcher.app_.invalidate();
      launcher.render(core);
      checkFrame(core, false, false);
      if (frame == 2) check(!ui::confirmHint.empty(), "Image Viewer retains its Confirm action");
      if (frame == 1 && panel == DisplayController::SSD1677) {
        check(lastData(0x21) == std::vector<uint8_t>{0x00}, "powered-on SSD1677 resumes differential refresh");
      }
    }
    launcher.app_.closeMenu();
    core.ownsDisplay = true;
    ++core.seed;
    commands.clear();
    launcher.app_.invalidate();
    launcher.render(core);
    checkFrame(core, false, false);
    }
    testSetDigitalReadHook(nullptr);
    testSetDigitalWriteHook(nullptr);
    testSetSpiWriteHook(nullptr);
    testUseRealtimeMillis();
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
'''


def main():
    launcher = (ROOT / "src/states/AppLauncherState.cpp").read_text()
    gfx = (ROOT / "lib/GfxRenderer/src/GfxRenderer.cpp").read_text()
    clock = (ROOT / "src/apps/ClockApp.cpp").read_text()
    registry = (ROOT / "src/apps/AppRegistry.cpp").read_text()
    image = (ROOT / "src/apps/ImageViewerApp.cpp").read_text()
    wifi = (ROOT / "src/hal/WifiRadio.h").read_text()
    clock_header = (ROOT / "src/apps/ClockApp.h").read_text()
    power = (ROOT / "src/apps/ClockPower.cpp").read_text()
    harness = HARNESS.replace("@LAUNCHER_RENDER@", block(launcher, "void AppLauncherState::render("))
    harness = harness.replace("@GFX_DISPLAY@", block(gfx, "void GfxRenderer::displayBuffer("))
    replacements = {
        "WIFI_SESSION": block(wifi, "class WifiSession final") + ";",
        "SYNCHRONIZE": block(clock, "SyncResult synchronize("),
        "SYNC_MESSAGE": block(clock, "static void showSyncMessage("),
        "PARSE_NTP": block(clock, "static int parseNtpServers("),
        "NEXT_SYNC": block(power, "int64_t nextSyncUtc("),
        "SYNC_RESULT": block(clock_header, "enum class SyncResult"),
        "NTP_CONSTANTS": clock[clock.index("static constexpr int NTP_SERVER_MAX"):clock.index("static struct")],
        "CLOCK_ENTER": block(clock, "void enter("),
        "CLOCK_RENDER": block(clock, "miniapps::Frame render("),
        "IMAGE_RENDER": block(image, "miniapps::Frame render("),
        "REGISTRY": registry[registry.index("const MiniApp APPS[]"):registry.rindex("\n}")],
    }
    for marker, code in replacements.items():
        harness = harness.replace(f"@{marker}@", code)
    with tempfile.TemporaryDirectory(prefix="papyrix-clock-display-") as directory:
        path = Path(directory)
        source = path / "clock_display.cpp"
        source.write_text(harness)
        failed = False
        for target, panels in TARGETS.items():
            binary = path / target
            subprocess.run([
                os.environ.get("CXX", "c++"), "-std=c++17", "-DTEST_BUILD",
                "-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1", f"-D{target}=1",
                *(f"-I{ROOT / include}" for include in INCLUDES),
                str(source), *(str(ROOT / item) for item in SOURCES), "-o", str(binary),
            ], check=True)
            for panel in panels:
                scenarios = ("app", "overlay", "keys", "status-connected", "status-success", "status-missing", "status-failed", "status-auto", "resume")
                if panel == "ssd1677" and target in ("PAPYRIX_TARGET_XTEINK_C3", "PAPYRIX_TARGET_X4CLASSIC"):
                    scenarios += ("retention",)
                for scenario in scenarios:
                    result = subprocess.run([str(binary), panel, scenario], capture_output=True, text=True)
                    if result.returncode:
                        print(result.stdout, end="")
                        print(result.stderr, end="")
                        failed = True
                    print(f"{'FAIL' if result.returncode else 'PASS'}: Clock display {panel} {scenario}", flush=True)
        if failed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
