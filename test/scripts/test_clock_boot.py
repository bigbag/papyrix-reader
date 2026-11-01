#!/usr/bin/env python3
"""Exercise Clock boot routing with the production retained-record and UI dispatch code."""

import os
from pathlib import Path
import subprocess
import tempfile

from test_clock_app import block

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include "apps/ClockPower.h"
#define LOG_INF(...)
#define LOG_DBG(...)
#define LOG_ERR(...)
#define PAPYRIX_X4PRO_CHARACTERIZE 0

int mounts, settingsReads, fileWrites, coreStarts, timerCycles, uiStarts, readerStarts;
int serialStarts, delayMs, normalStarts, panelBegins, panelRetries, splashes;
bool storageOk = true, flashOk = true, boardOk = true, panelOk = true, timerSleeps = true;
struct Sleeping {};
struct { uint8_t app_elf_sha256[32]{}; } descriptor;
auto esp_app_get_description() { return &descriptor; }
constexpr int kSerialEnumerationDelayMs = 250, kSerialBaudRate = 115200;
void delay(int duration) { delayMs += duration; }
struct { void begin(int) { ++serialStarts; } } Serial;
struct { void begin() {} } inputManager;
struct { bool begin(bool) { ++mounts; return flashOk; } } LittleFS;
uint16_t rtcPowerButtonDurationMs;
struct Renderer {
  void clearScreen(int) {}
  template<class... T> void drawCenteredText(T...) {}
  int getScreenWidth() const { return 480; }
  void displayBuffer() { ++panelRetries; }
} renderer;
namespace i18n { void loadLocaleFromSD() {} }
namespace papyrix {
enum class BootMode { UI, READER };
enum class StateId { Home, AppLauncher, FileList, Recent, Network, Error };
enum class ReturnTo { HOME, FILE_MANAGER, RECENT };
enum class SyncMode { None, NtpSync };
constexpr int APP_CLOCK = 2;
struct Result { int err = 0; bool ok() const { return err == 0; } };
const char* errorToString(int) { return "error"; }
namespace hal {
struct Display {
  enum class InitResult { Ok, Failed };
  bool isInitialized() const { return panelBegins > 0 && panelOk; }
  void* getFrameBuffer() { return this; }
  InitResult begin() { ++panelBegins; return panelOk ? InitResult::Ok : InitResult::Failed; }
  InitResult recover() { ++panelRetries; return panelOk ? InitResult::Ok : InitResult::Failed; }
};
}
struct Core {
  struct Settings {
    const char* themeName = "default";
    uint8_t frontButtonLayout = 0;
    void loadFromFile() { ++settingsReads; }
    int getPowerButtonDuration() const { return 400; }
  } settings;
  struct Input {
    bool suppressed = false, ready = false;
    void suppressPowerUntilRelease() { suppressed = true; }
    void init(int&) { ready = true; }
  } input;
  struct Storage { Result init() { ++mounts; return {storageOk ? 0 : 1}; } } storage;
  hal::Display display;
  struct { char text[256]{}; } buf;
  int events = 0, pendingAppId = -1;
  SyncMode pendingSync = SyncMode::None;
  BootMode bootMode = BootMode::UI;
  Result init() {
    ++coreStarts;
    if (!storageOk) return {1};
    input.init(events);
    return {};
  }
} core;
struct Transition {
  ReturnTo returnTo = ReturnTo::HOME;
  bool valid = false;
  bool isValid() const { return valid; }
} transition;
const Transition& getTransition() { return transition; }
BootMode normalBootMode = BootMode::UI;
BootMode detectBootMode() { return normalBootMode; }
namespace board {
enum class BootPurpose { Interactive, ClockTimer };
class HardwareIdentity {
 public:
  BoardProfile profile_{};
  static HardwareIdentity& instance() { static HardwareIdentity value; return value; }
  BoardId board() const { return profile_.id; }
  const BoardProfile& profile() const { return profile_; }
};
const BoardProfile& bootProfile() { return HardwareIdentity::instance().profile(); }
bool selectBoard(HardwareIdentity&, bool allowProbe) { assert(!allowProbe); return boardOk; }
bool restoreClockPanel(HardwareIdentity&, eink::DisplayController, uint8_t) { return panelOk; }
void earlyInit(const BoardProfile&, BootPurpose) {}
void releaseDeepSleepHolds() {}
void restoreRecoveryStorage(const BoardProfile&) {}
}
namespace clock_app {
ResumeRecord retained;
StandbyResult timerResult = StandbyResult::Interactive;
@READ_RESUME@
@CLEAR_RESUME@
@ERROR_MESSAGE@
StandbyResult runTimerWake(Core&, ResumeRecord&) {
  ++timerCycles;
  if (timerSleeps) throw Sleeping{};
  return timerResult;
}
}
}
papyrix::BootMode currentBootMode;
struct { void init(Renderer&) {} } FONT_MANAGER;
struct {
  void loadTheme(const char*) {}
  void createDefaultThemeFiles() { ++fileWrites; }
  const char* currentThemeName() { return "default"; }
} THEME_MANAGER;
struct Theme {
  bool invertedMode = false, primaryTextBlack = true;
  int backgroundColor = 255, readerFontId = 0, uiFontId = 0, screenMarginSide = 10, itemPaddingX = 10;
} THEME;
struct { void setSettings(void*) {} } mappedInputManager;
namespace ui {
struct BootView { void setStatus(const char*) {} void setDarkMode(bool) {} };
void render(Renderer&, decltype(THEME)&, BootView&) { ++splashes; }
void setFrontButtonLayout(uint8_t) {}
template<class... T> void centeredTextWrapped(T&&...) {}
}
constexpr const char* BOOTING = "Booting";
const char* tr(const char* value) { return value; }
constexpr int BOLD = 1, REGULAR = 0;
constexpr const char* ERROR = "Error";
constexpr const char* PRESS_ANY_BUTTON = "Press any button";
namespace papyrix {
class ErrorState {
 public:
  bool needsRender_ = true;
  Renderer& renderer_ = renderer;
  const char* message_ = "Clock display update failed";
  void render(Core&);
};
@ERROR_RENDER@
}
int startupState, homeState, fileListState, recentState, readerState, settingsState;
int networkState, calibreSyncState, sleepState, errorState;
struct {
  int resumes = 0;
  void resumeClock(papyrix::Core&, const papyrix::clock_app::ResumeRecord&) { ++resumes; }
} appLauncherState;
struct {
  papyrix::StateId current = papyrix::StateId::Home;
  template<class T> void registerState(T*) {}
  void init(papyrix::Core&, papyrix::StateId next) { current = next; ++uiStarts; }
  void update(papyrix::Core&) {}
} stateMachine;
void setupDisplayAndFonts(bool = true, bool initialize = true) { if (initialize) papyrix::core.display.begin(); }
void applyThemeFonts() {}
void showErrorScreen(const char*) {}
bool earlyInit() { ++normalStarts; return true; }
void initReaderMode() { ++readerStarts; }
bool waitForPowerRelease() { return false; }
@UI_MODE@
@CLOCK_BOOT@
@SETUP@

void prepare() {
  using namespace papyrix;
  using namespace clock_app;
  mounts = settingsReads = fileWrites = coreStarts = timerCycles = uiStarts = readerStarts = 0;
  serialStarts = delayMs = normalStarts = panelBegins = panelRetries = splashes = 0;
  appLauncherState.resumes = 0;
  storageOk = flashOk = boardOk = panelOk = timerSleeps = true;
  timerResult = StandbyResult::Interactive;
  core = Core{};
  transition = Transition{};
  normalBootMode = BootMode::UI;
  testResetReason = ESP_RST_DEEPSLEEP;
  testWakeCause = ESP_SLEEP_WAKEUP_TIMER;
  std::memset(&descriptor, 0, sizeof(descriptor));
  std::memset(static_cast<void*>(&retained), 0, sizeof(retained));
  retained.settings = ClockSettings{};
  retained.settings.powerMode = PowerMode::PowerSaving;
  retained.lastFrame = ClockFrame{};
  retained.lastFrame.utcSeconds = 1780000000;
  retained.active = retained.frameValid = 1;
  retained.identity.board = board::BoardId::X4;
  board::HardwareIdentity::instance().profile_.id = board::BoardId::X4;
  sealResume(retained);
}
int main() {
  using namespace papyrix;
  using namespace clock_app;
  prepare();
  try { setup(); assert(false); } catch (const Sleeping&) {}
  assert(timerCycles == 1 && mounts == 0 && fileWrites == 0 && coreStarts == 0);
  assert(delayMs == 0 && serialStarts == 0 && normalStarts == 0 && splashes == 0);

  for (const auto cause : {ESP_SLEEP_WAKEUP_GPIO, ESP_SLEEP_WAKEUP_EXT1}) {
    prepare(); testWakeCause = cause; setup();
    assert(stateMachine.current == StateId::AppLauncher && appLauncherState.resumes == 1);
    assert(timerCycles == 0 && splashes == 0 && normalStarts == 0 && readerStarts == 0);
    assert(core.input.ready && core.input.suppressed && retained.active == 0);
  }
  prepare(); timerSleeps = false; setup();
  assert(timerCycles == 1 && appLauncherState.resumes == 1 && splashes == 0);

  for (const auto result : {StandbyResult::StorageError, StandbyResult::DisplayError, StandbyResult::PowerError}) {
    prepare(); timerSleeps = false; timerResult = result; setup();
    assert(stateMachine.current == StateId::Error && appLauncherState.resumes == 0);
    assert(retained.active == 0 && core.input.ready && core.input.suppressed && splashes == 0);
    assert(std::strstr(core.buf.text, "Clock") != nullptr);
    if (result == StandbyResult::DisplayError) assert(panelBegins == 0 && panelRetries == 0);
    if (result == StandbyResult::DisplayError) {
      ErrorState error;
      error.render(core);
      assert(panelBegins == 0 && panelRetries == 0);
    }
  }
  prepare(); timerSleeps = false; timerResult = StandbyResult::DisplayError; storageOk = false; setup();
  assert(stateMachine.current == StateId::Error && core.input.ready);
  assert(panelBegins == 0 && panelRetries == 0);

  prepare(); timerSleeps = false; timerResult = StandbyResult::TimeRequired; setup();
  assert(stateMachine.current == StateId::Network && core.pendingSync == SyncMode::NtpSync);
  assert(core.pendingAppId == APP_CLOCK && retained.active == 0 && splashes == 0);

  for (const auto reason : {ESP_RST_POWERON, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_TASK_WDT, ESP_RST_BROWNOUT}) {
    prepare(); testResetReason = reason; setup();
    assert(normalStarts == 1 && timerCycles == 0 && appLauncherState.resumes == 0 && retained.active == 0);
  }
  for (int fault = 0; fault < 5; ++fault) {
    prepare();
    if (fault == 0) retained.crc32 ^= 1;
    if (fault == 1) descriptor.app_elf_sha256[0] = 1;
    if (fault == 2) boardOk = false;
    if (fault == 3) board::HardwareIdentity::instance().profile_.id = board::BoardId::X3;
    if (fault == 4) panelOk = false;
    setup();
    assert(normalStarts == 1 && timerCycles == 0 && appLauncherState.resumes == 0 && retained.active == 0);
  }
  prepare(); testResetReason = ESP_RST_SW; normalBootMode = BootMode::READER; setup();
  assert(readerStarts == 1 && uiStarts == 0 && timerCycles == 0 && retained.active == 0);
  std::puts("PASS: Clock boot precedence, direct Power return, bounded errors, and Reader routing");
}
'''


def main():
    source = (ROOT / "src/main.cpp").read_text()
    standby = (ROOT / "src/apps/ClockStandby.cpp").read_text()
    replacements = {
        "UI_MODE": block(source, "void initUIMode("),
        "CLOCK_BOOT": block(source, "bool tryClockBoot("),
        "SETUP": block(source, "void setup()"),
        "READ_RESUME": block(standby, "bool readResume("),
        "CLEAR_RESUME": block(standby, "void clearResume("),
        "ERROR_MESSAGE": block(standby, "const char* standbyErrorMessage("),
        "ERROR_RENDER": block((ROOT / "src/states/ErrorState.cpp").read_text(), "void ErrorState::render(")
                        .replace("THEME_MANAGER.current()", "THEME"),
    }
    harness = HARNESS
    for marker, code in replacements.items():
        harness = harness.replace(f"@{marker}@", code)
    with tempfile.TemporaryDirectory(prefix="papyrix-clock-boot-") as directory:
        path = Path(directory)
        (path / "esp_sleep.h").write_text(
            "#pragma once\nusing esp_sleep_wakeup_cause_t = int;\n"
            "constexpr int ESP_SLEEP_WAKEUP_TIMER=4, ESP_SLEEP_WAKEUP_EXT1=3, ESP_SLEEP_WAKEUP_GPIO=7;\n"
            "inline int testWakeCause=4;\ninline int esp_sleep_get_wakeup_cause() { return testWakeCause; }\n")
        (path / "boot.cpp").write_text(harness)
        subprocess.run([
            os.environ.get("CC", "cc"), f"-I{ROOT / 'lib/uzlib/src'}", "-c",
            str(ROOT / "lib/uzlib/src/crc32.c"), "-o", str(path / "crc32.o"),
        ], check=True)
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17",
            f"-I{path}", f"-I{ROOT / 'test/mocks'}", f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'lib/BoardSupport/include'}", f"-I{ROOT / 'lib/EInkDisplay/include'}",
            f"-I{ROOT / 'lib/uzlib/src'}", str(path / "boot.cpp"),
            str(ROOT / "src/apps/ClockPower.cpp"), str(path / "crc32.o"), "-o", str(path / "boot"),
        ], check=True)
        subprocess.run([str(path / "boot")], check=True)


if __name__ == "__main__":
    main()
