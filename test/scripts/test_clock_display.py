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
    "lib/BoardSupport/src/HardwareIdentity.cpp",
    "lib/BoardSupport/src/BoardProfiles.cpp",
    "test/mocks/platform_stubs.cpp",
]
INCLUDES = [
    "lib/EInkDisplay/include",
    "test/mocks",
    "lib/BoardSupport/include",
    "lib/BatteryMonitor/include",
    "src",
]
TARGETS = {
    "PAPYRIX_TARGET_XTEINK_C3": ("ssd1677", "uc8253", "uc8279-x3"),
    "PAPYRIX_TARGET_X4PRO": ("uc8179-pro", "uc8279-pro"),
    "PAPYRIX_TARGET_X4CLASSIC": ("ssd1677", "uc8179-pro", "uc8279-pro"),
}

HARNESS = r'''
#include <Display.h>
#include <HardwareIdentity.h>
#include <SPI.h>
#include <apps/MiniApp.h>

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
    // ponytail: PON checks cover power-off recovery. Extend the model for powered-on UC8253 resync.
    if (panel == DisplayController::SSD1677 ? data[0] == 0x20
                                          : data[0] == 0x04 || data[0] == 0x12 || data[0] == 0x02) {
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
  if (busyStuck) return HIGH;
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
}

class GfxRenderer {
 public:
  Display& display_;
  bool darkBackground_ = false;
  void logRenderTime() const {}
  void clearScreen(uint8_t color) { display_.clearScreen(color); }
  int getScreenHeight() const { return display_.getDisplayHeight(); }
  int getScreenWidth() const { return display_.getDisplayWidth(); }
  void clearArea(int, int, int, int, uint8_t) {}
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
  Result connect(const char*, const char*) {
    checkClockWait();
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
  struct Clock { bool localTime(std::tm&) const { return false; } } clock;
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

struct Theme { uint8_t backgroundColor = 0xFF; int uiFontId = 0; } theme;
Core* statusCore = nullptr;
#define THEME theme
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
void centeredMessage(GfxRenderer&, const Theme&, int, const char*) {
  if (statusFrames) checkClockWait();
  commands.clear();
  ++statusFrames;
  ++statusCore->seed;
  paint(*statusCore);
}
}

namespace clock_app {
GfxRenderer* clockRenderer = nullptr;
#define renderer (*clockRenderer)
struct {
  int lastRenderedMin, menuSelected, utcOffset;
  uint32_t lastNtpSyncMs;
} state;
struct {
  struct Credential { const char* ssid = "home"; const char* password = "secret"; } creds[2];
  int count = 1;
  void loadFromFile() {}
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
} credentials;
#define WIFI_STORE credentials
void loadSettings(Core&) {}
void applyTimezone(int) {}
void delay(unsigned long ms) { checkClockWait(); ::delay(ms); }
void syncNtpWithConnection(Core&) { checkClockWait(); ++syncAttempts; }
@SYNC_AUTO@
@CLOCK_ENTER@
bool update(Core&) { return false; }
bool render(Core& core) { return renderApp(core); }
void exit(Core&) {}
void renderMenu(Core& core) { paint(core); }
void onMenuButton(Core&, Button) {}
#undef renderer
}
namespace imageviewer_app {
void enter(Core&) {}
bool update(Core&) { return false; }
void onButton(Core&, Button) {}
bool render(Core& core) { return renderApp(core); }
void exit(Core&) {}
void renderMenu(Core& core) { paint(core); }
void onMenuButton(Core&, Button) {}
}
namespace printer_app {
void enter(Core&) {}
bool update(Core&) { return false; }
void onButton(Core&, Button) {}
bool render(Core& core) { return renderApp(core); }
void exit(Core&) {}
}
namespace localsend_app {
void enter(Core&) {}
bool update(Core&) { return false; }
void onButton(Core&, Button) {}
bool render(Core& core) { return renderApp(core); }
void exit(Core&) {}
}
namespace pomodoro_app {
void enter(Core&) {}
bool update(Core&) { return false; }
void onButton(Core&, Button) {}
bool render(Core& core) { return renderApp(core); }
void exit(Core&) {}
void renderMenu(Core& core) { paint(core); }
void onMenuButton(Core&, Button) {}
}
@REGISTRY@

class AppLauncherState {
 public:
  enum class Mode { Menu, App, Overlay };
  GfxRenderer& renderer_;
  Mode mode_ = Mode::App;
  int8_t activeApp_ = APP_CLOCK;
  bool needsRender_ = true;
  ui::AppMenuView menuView_;
  void render(Core& core);
  void stopApp(Core& core);
  void showOverlay();
  void hideOverlay();
  void press(Core& core, Button button, bool repeat = false) {
    enum class EventType { ButtonPress, ButtonRepeat };
    struct { Button button; EventType type; } e{button, repeat ? EventType::ButtonRepeat : EventType::ButtonPress};
    switch (mode_) {
@KEY_DISPATCH@
      default: break;
    }
  }
};

@LAUNCHER_RENDER@
@STOP_APP@
@SHOW_OVERLAY@
@HIDE_OVERLAY@
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
    check(commandCount(0x20) == 1, "launcher submits one SSD1677 refresh");
    check(lastData(0x24) == expected, "SSD1677 current plane contains the submitted frame");
    check(lastData(0x26) == expected, "SSD1677 baseline contains the submitted frame");
    check(lastData(0x21) == std::vector<uint8_t>{static_cast<uint8_t>(cold ? 0x40 : 0x00)},
          "SSD1677 uses FAST once the previous frame is valid");
    if (turnOff) {
      check(lastData(0x22).size() == 1 && (lastData(0x22)[0] & 0x03) == 0x03,
            "Clock face powers SSD1677 off");
    } else {
      check(lastData(0x22).size() == 1 && (lastData(0x22)[0] & 0x03) == 0,
            "menus and other apps keep SSD1677 powered");
    }
    return;
  }

  check(commandCount(0x07) == 0, "Clock must not put UC controllers into deep sleep");
  if (!cold) check(commandCount(0x12) == 1, "launcher must not submit a duplicate UC refresh");
  check(lastData(0x13) == expected, "UC current plane contains the submitted frame");
  check(lastData(0x10) == expected, "UC old plane contains the submitted frame");
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
    if (scenario == "retention") {
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
      clock_app::credentials.count = scenario == "status-missing" ? 0 : 2;
      commands.clear();
      clock_app::enter(core);
      if (scenario == "status-missing") {
        check(statusFrames == 0 && commands.empty(), "missing credentials must not replace the Clock display");
      } else {
        checkClockWait();
        check(statusFrames == (scenario == "status-failed" ? 2u : 1u), "Clock displays each status only once");
      }
      const bool failed = scenario == "status-failed" || scenario == "status-missing";
      check(syncAttempts == (failed ? 0u : 1u), "only a connected radio starts NTP");
      check(!core.wifi.connected, "Clock consumes inherited and acquired radio ownership");
    } else {
    check(scenario == "app" || scenario == "overlay" || scenario == "keys", "unsupported render scenario");
    launcher.mode_ = scenario == "overlay" ? AppLauncherState::Mode::Overlay : AppLauncherState::Mode::App;
    for (int frame = 0; frame < 6; ++frame) {
      renderer.darkBackground_ = frame >= 3;
      theme.backgroundColor = renderer.darkBackground_ ? 0x00 : 0xFF;
      core.seed = static_cast<uint8_t>(17 + frame * 37);
      commands.clear();
      launcher.needsRender_ = true;
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
      check(launcher.mode_ == AppLauncherState::Mode::App, "repeat must not open the menu");
      launcher.press(core, Button::Center);
      check(launcher.mode_ == AppLauncherState::Mode::Overlay, "Center opens the Clock menu");
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
      check(launcher.mode_ == AppLauncherState::Mode::App, "Back closes the Clock menu");
      commands.clear();
      launcher.render(core);
      checkFrame(core, true, false);
      launcher.press(core, Button::Back);
      check(launcher.mode_ == AppLauncherState::Mode::Menu && launcher.activeApp_ < 0,
            "Back leaves the Clock face");
    }
    launcher.activeApp_ = APP_IMAGEVIEWER;
    launcher.mode_ = AppLauncherState::Mode::App;
    for (int frame = 0; frame < 3; ++frame) {
      if (frame == 2) launcher.mode_ = AppLauncherState::Mode::Overlay;
      ++core.seed;
      commands.clear();
      launcher.needsRender_ = true;
      launcher.render(core);
      checkFrame(core, false, false);
      if (frame == 2) check(!ui::confirmHint.empty(), "Image Viewer retains its Confirm action");
      if (frame == 1 && panel == DisplayController::SSD1677) {
        check(lastData(0x21) == std::vector<uint8_t>{0x00}, "powered-on SSD1677 resumes differential refresh");
      }
    }
    launcher.mode_ = AppLauncherState::Mode::App;
    core.ownsDisplay = true;
    ++core.seed;
    commands.clear();
    launcher.needsRender_ = true;
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
    wifi = (ROOT / "src/hal/WifiRadio.h").read_text()
    harness = HARNESS.replace("@LAUNCHER_RENDER@", block(launcher, "void AppLauncherState::render("))
    harness = harness.replace("@GFX_DISPLAY@", block(gfx, "void GfxRenderer::displayBuffer("))
    replacements = {
        "WIFI_SESSION": block(wifi, "class WifiSession final") + ";",
        "SYNC_AUTO": block(clock, "static void syncNtpAutoConnect("),
        "CLOCK_ENTER": block(clock, "void enter("),
        "REGISTRY": registry[registry.index("const MiniApp APPS[]"):registry.rindex("\n}")],
        "KEY_DISPATCH": launcher[launcher.index("case Mode::App:"):launcher.index("\n    }\n  }")],
        "STOP_APP": block(launcher, "void AppLauncherState::stopApp("),
        "SHOW_OVERLAY": block(launcher, "void AppLauncherState::showOverlay("),
        "HIDE_OVERLAY": block(launcher, "void AppLauncherState::hideOverlay("),
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
                scenarios = ("app", "overlay", "keys", "status-connected", "status-success", "status-missing", "status-failed")
                if panel == "ssd1677":
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
