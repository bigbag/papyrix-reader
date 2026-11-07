#!/usr/bin/env python3
"""Exercise Clock menu, scheduling, entry, and synchronization paths with host hardware substitutes."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <iterator>
#include <string>
#include <algorithm>
#include <vector>
#include <charconv>
#include <map>
#include <string_view>
#include <EpdFontFamily.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <ui/TouchLayout.h>
#include <core/EventQueue.h>
#include <states/State.h>
#include <MiniApps.h>
#include <CheckedFile.h>
using papyrix::Button;
using papyrix::Event;
using papyrix::EventType;
using papyrix::StateId;
using papyrix::StateTransition;
#define LOG_INF(...)
#define LOG_ERR(...)
#define LOG_DBG(...)
uint32_t nowMs = 0;
uint32_t millis() { return nowMs; }
std::string settingsFile;
bool settingsExists = false;
int writes = 0, syncs = 0, statusFrames = 0;
bool ntpSynced = true;
bool sntpActive = false;
std::time_t systemEpoch = 1780000000;
extern "C" std::time_t time(std::time_t* out) noexcept {
  const auto value = systemEpoch + nowMs / 1000;
  if (out) *out = value;
  return value;
}
void deliverNtpPacket(std::time_t epoch) { if (sntpActive) systemEpoch = epoch; }
bool sntpStopQueued = false;
constexpr int SNTP_SYNC_STATUS_COMPLETED = 1;
int esp_sntp_get_sync_status() { return (sntpActive && ntpSynced) ? SNTP_SYNC_STATUS_COMPLETED : 0; }
void esp_sntp_stop() { sntpStopQueued = true; }
// esp_sntp_stop queues an lwIP callback; delay runs queued callbacks.
void delay(int ms) {
  nowMs += static_cast<uint32_t>(ms);
  if (sntpStopQueued) {
    sntpStopQueued = false;
    sntpActive = false;
  }
}
void configTime(long, int, const char*, const char*, const char*) {
  ++syncs;
  sntpActive = true;
  setenv("TZ", "UTC0", 1);
  tzset();
}
bool failSettingsWrite = false;
struct FsFile {
  std::string* data = nullptr;
  size_t fileSize() const { return data->size(); }
  int read(uint8_t* out, size_t length) {
    const size_t count = std::min(length, data->size());
    std::memcpy(out, data->data(), count);
    return static_cast<int>(count);
  }
  size_t write(const uint8_t* out, size_t length) {
    if (failSettingsWrite) return 0;
    data->assign(reinterpret_cast<const char*>(out), length);
    return length;
  }
  bool sync() { return true; }
  bool close() { return true; }
};
struct Settings { static constexpr uint8_t FrontLRBC = 1; };
enum class SyncMode { None, NtpSync, FileTransfer, CalibreWireless, WifiSetup, PrinterSetup, LocalsendSetup };
struct Result {
  size_t value = 0;
  bool okValue = true;
  bool ok() const { return okValue; }
};
enum class Error { None, InvalidOperation, Timeout };
Result Ok() { return {}; }
Result ErrVoid(Error) { return {0, false}; }
#undef TRY
#define TRY(expr) do { const auto result = (expr); if (!result.ok()) return result; } while (false)
bool radioConnectFails = false, cancelPower = false;
uint32_t radioDelayMs = 0, cancelAtMs = 0;
int radioConnects = 0;
constexpr int WL_CONNECTED = 1, WIFI_PS_NONE = 0;
void esp_wifi_set_ps(int) {}
struct WifiBoundary {
  bool started = false;
  uint32_t startedAt = 0;
  void persistent(bool) {}
  void disconnect(bool = false, bool = false) { started = false; }
  void begin(const char*, const char*) { ++radioConnects; started = true; startedAt = millis(); }
  int status() const {
    return started && !radioConnectFails && static_cast<uint32_t>(millis() - startedAt) >= radioDelayMs ? WL_CONNECTED : 0;
  }
} WiFi;
struct WifiCredential {
  char ssid[33];
  char password[65];
};
struct WifiCredentialStore {
  WifiCredential creds[8] = {};
  int count_ = 0;
  bool loadFromFile() { return true; }
  const WifiCredential* getCredentials() const { return creds; }
  int getCount() const { return count_; }
  bool promoteCredential(const char* ssid) {
    for (int i = 0; i < count_; ++i) {
      if (std::strcmp(creds[i].ssid, ssid) != 0) continue;
      std::rotate(creds, creds + i, creds + i + 1);
      return true;
    }
    return false;
  }
};
WifiCredentialStore wifiStore;
#define WIFI_STORE wifiStore
void seedCredential(const char* ssid, const char* password) {
  WifiCredential& cred = wifiStore.creds[wifiStore.count_++];
  snprintf(cred.ssid, sizeof(cred.ssid), "%s", ssid);
  snprintf(cred.password, sizeof(cred.password), "%s", password);
}
namespace hal {
struct Cpu {
  bool throttled = false;
  int performanceLocks = 0;
  void unthrottle() { throttled = false; }
  void throttle() { if (performanceLocks == 0) throttled = true; }
  class PerformanceLock {
   public:
    explicit PerformanceLock(Cpu& cpu) : cpu_(cpu) { ++cpu_.performanceLocks; cpu_.unthrottle(); }
    ~PerformanceLock() { --cpu_.performanceLocks; }
    PerformanceLock(const PerformanceLock&) = delete;
    PerformanceLock& operator=(const PerformanceLock&) = delete;
   private:
    Cpu& cpu_;
  };
};
struct WifiRadio {
  bool connected_ = false;
  bool initialized_ = false;
  bool apMode_ = false;
  int& connects = radioConnects;
  bool& connectFails = radioConnectFails;
  bool isConnected() const { return connected_; }
  bool isInitialized() const { return initialized_; }
  Result init() { initialized_ = true; delay(100); return {}; }
  void stopAP() { apMode_ = false; }
  @WIFI_CONNECT@
  void shutdown() {
    connected_ = false;
    initialized_ = false;
    WiFi.disconnect();
  }
};
@WIFI_SESSION@
}  // namespace hal
struct Core {
  papyrix::EventQueue events;
  struct { char text[256]{}; } buf;
  Result init() { return {}; }
  struct Display {
    static constexpr int HALF_REFRESH = 0, FAST_REFRESH = 1;
    enum class InitResult { Ok };
    InitResult recover() { return InitResult::Ok; }
  } display;
  char pendingDirectory[2]{};
  struct Settings { uint8_t frontButtonLayout = 0; } settings;
  struct Clock {
    bool valid = true;
    bool updateSucceeds = true;
    bool updateFromSystem() { return updateSucceeds; }
    bool canRetainTime() const { return updateSucceeds; }
    bool localTime(std::tm& out) {
      if (!valid) return false;
      const std::time_t epoch = systemEpoch + nowMs / 1000;
      out = *std::localtime(&epoch);
      return true;
    }
  } clock;
  struct Storage {
    std::map<std::string, std::string> files;
    static bool final(const char* path) { return std::strcmp(path, "/.papyrix/apps/clock.txt") == 0; }
    Result exists(const char* path) { return {final(path) ? settingsExists : files.count(path) != 0, true}; }
    Result mkdir(const char*) { return {}; }
    Result openRead(const char* path, FsFile& file) {
      if (!exists(path).value) return {0, false};
      file.data = final(path) ? &settingsFile : &files.at(path);
      return {};
    }
    Result openWrite(const char* path, FsFile& file) {
      files[path].clear();
      file.data = &files.at(path);
      return {};
    }
    Result remove(const char* path) {
      if (final(path)) settingsExists = false;
      else files.erase(path);
      return {};
    }
    Result rename(const char* from, const char* to) {
      if (!exists(from).value || exists(to).value) return {0, false};
      const auto contents = final(from) ? settingsFile : files.at(from);
      if (final(to)) { settingsFile = contents; settingsExists = true; ++writes; }
      else files[to] = contents;
      remove(from);
      return {};
    }
  } storage;
  struct Input {
    uint32_t lastActivityMs = 0;
    void resetIdleTimer() {}
    uint32_t userIdleTimeMs() const { return millis() - lastActivityMs; }
    void suppressPowerUntilRelease() {}
    Result init(papyrix::EventQueue&) { return {}; }
    bool hasPendingInteraction() const { return false; }
  } input;
  struct Battery {
    struct Status {
      bool percentageKnown;
      unsigned percentage;
      bool chargingKnown = false, charging = false;
    };
    Status status{true, 50};
    mutable int reads = 0;
    Status readStatus() const {
      ++reads;
      return status;
    }
  } battery;
  struct Usb { bool isConnected() const { return false; } } usb;
  hal::Cpu cpu;
  hal::WifiRadio wifi;
  SyncMode pendingSync = SyncMode::None;
  int pendingAppId = -1;
};
namespace hal { using Storage = Core::Storage; }
namespace papyrix::hal {
using Display = ::Core::Display;
}
namespace hal { using Display = papyrix::hal::Display; }
namespace board {
enum class BootPurpose { Interactive };
struct HardwareIdentity {
  static HardwareIdentity& instance() { static HardwareIdentity identity; return identity; }
  int profile() const { return 0; }
};
void earlyInit(int, BootPurpose) {}
void releaseDeepSleepHolds() {}
void restoreRecoveryStorage(int) {}
}
struct { void begin() {} } inputManager;
EpdFont uiFont(&ui_12), boldUiFont(&ui_bold_12), smallFont(&small14);
EpdFont readerFont(&reader_2b), boldReaderFont(&reader_bold_2b);
struct Renderer {
  struct Draw {
    char kind;
    int x, y, w, h;
    bool black;
    std::string text;
    int fontId = 0;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };
  mutable std::vector<Draw> draws;
  int width = 480, height = 800;
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  enum RenderMode { BW, GRAYSCALE_LSB, GRAYSCALE_MSB };
  Orientation orientation = Portrait;
  Orientation getOrientation() const { return orientation; }
  void setOrientation(Orientation value) { orientation = value; }
  void setRenderMode(RenderMode) {}
  static constexpr int BUTTON_HINT_WIDTH = 106, BUTTON_HINT_MAX_TEXT_WIDTH = 94;
  const EpdFont& font(int id, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    if (id == 1) return smallFont;
    if (id == 2) return style == EpdFontFamily::BOLD ? boldReaderFont : readerFont;
    return style == EpdFontFamily::BOLD ? boldUiFont : uiFont;
  }
  int getTextWidth(int id, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    int w, h;
    font(id, style).getTextDimensions(text, &w, &h);
    return w;
  }
  int getLineHeight(int id) const { return font(id).data->advanceY; }
  std::string truncatedText(int, const char* text, int) const { return text; }
  void displayBuffer(int = 0, bool = false) { ++statusFrames; }
  void clearScreen(int) const { draws.clear(); }
  void fillRect(int x, int y, int w, int h, bool black) const { draws.push_back({'F', x, y, w, h, black, {}}); }
  void clearArea(int x, int y, int w, int h, int color) const { fillRect(x, y, w, h, color == 0); }
  void drawRect(int x, int y, int w, int h, bool black) const { draws.push_back({'R', x, y, w, h, black, {}}); }
  void drawLine(int x, int y, int x2, int y2, bool black) const { draws.push_back({'L', x, y, x2, y2, black, {}}); }
  void drawText(int id, int x, int y, const char* text, bool black,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    draws.push_back({'T', x, y, getTextWidth(id, text, style), getLineHeight(id), black, text, id, style});
  }
  void drawCenteredText(int id, int y, const char* text, bool black,
                        EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    drawText(id, (width - getTextWidth(id, text, style)) / 2, y, text, black, style);
  }
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
} renderer;
using GfxRenderer = Renderer;
constexpr int UI_FONT_ID = 0, SMALL_FONT_ID = 1;
struct Theme {
  int uiFontId = 0, smallFontId = 1, readerFontId = 2;
  uint8_t backgroundColor = 255, screenMarginTop = 9, screenMarginSide = 3, itemHeight = 30, itemSpacing = 0;
  uint8_t itemPaddingX = 8, itemValuePadding = 20;
  bool invertedMode = false;
  bool primaryTextBlack = true, secondaryTextBlack = true;
  bool selectionFillBlack = true, selectionTextBlack = false;
} THEME;
namespace ui {
uint8_t frontButtonLayout_ = 0;
void setFrontButtonLayout(uint8_t value) { frontButtonLayout_ = value; }
@BATTERY_BODY@
@BATTERY_ICON@
@BATTERY@
@TITLE@
@ENUM_VALUE@
@MENU_ITEM@
@BUTTON_BOUNDS@
@DRAW_BUTTON_BAR@
std::string leftHint, rightHint;
struct ButtonBar {
  const char* left;
  const char* right;
  const char* labels[4];
  ButtonBar(const char* a = "", const char* b = "", const char* l = "", const char* r = "")
      : left(l), right(r), labels{a, b, l, r} {}
};
void buttonBar(Renderer& r, const Theme& t, const ButtonBar& buttons) {
  leftHint = buttons.left;
  rightHint = buttons.right;
  const char* remapped[] = {buttons.labels[2], buttons.labels[3], buttons.labels[0], buttons.labels[1]};
  drawButtonBar(r, t, frontButtonLayout_ == 1 ? remapped : buttons.labels);
}
@APP_MENU_VIEW@;
@SETTINGS_HIT@;
@SETTINGS_HIT_TEST@
}
@MINI_APP_BUTTON@
@HANDLE_APP_BUTTONS@
constexpr int APP_CLOCK = 0;
constexpr int APP_POMODORO = 1;
constexpr int APP_2048 = 2;
namespace ui {
int appIdForMenuItem(int) { return APP_CLOCK; }
}
namespace clock_faces {
enum class Face : uint8_t {
  Big = 0,
  Classic = 1,
  Analog = 2,
  Retro = 3,
  Flip = 4,
  Minimal = 5,
  Serif = 6,
  DayNight = 7,
  Count = 8,
};
struct Context {
  Renderer& renderer;
  const Theme& theme;
  const std::tm& time;
  bool use24h;
  int8_t dateFormat;
  int8_t utcOffset;
};
inline bool isSelectable(Face face) {
  return face == Face::Big || face == Face::Analog || face == Face::Retro || face == Face::Flip ||
         face == Face::DayNight;
}
inline Face selectRelative(Face face, int delta) {
  static constexpr Face faces[] = {Face::Big, Face::Analog, Face::Retro, Face::Flip, Face::DayNight};
  int index = 0;
  while (index < 5 && faces[index] != face) ++index;
  if (index == 5) return Face::Big;
  return faces[(index + delta + 5) % 5];
}
inline const char* name(Face face) {
  switch (face) {
    case Face::Big:
      return "Big";
    case Face::Analog:
      return "Analog";
    case Face::Retro:
      return "Retro";
    case Face::Flip:
      return "Flip";
    case Face::DayNight:
      return "Day & Night";
    default:
      return "";
  }
}
inline int renderCalls = 0;
inline Face lastFace = Face::Big;
inline bool lastUse24h = true;
inline int8_t lastDateFormat = 0;
inline void render(const Context& context, Face face) {
  ++renderCalls;
  lastFace = face;
  lastUse24h = context.use24h;
  lastDateFormat = context.dateFormat;
}
}  // namespace clock_faces
namespace clock_app {
@SETTINGS_MODEL@
@SETTINGS_STORAGE@
@FRAME_MODEL@
struct ResumeRecord { ClockSettings settings; int64_t nextNtpUtc = 0; ClockFrame lastFrame; };
constexpr int64_t kValidTime = 946684800;
@NEXT_SYNC@
@INTERACTION_EXPIRED@
@SYNC_RESULT@;
bool powerWakeRequested() { return cancelPower && millis() >= cancelAtMs; }
void clearResume() {}
@STATE@
@TIMEZONE@
@LOAD@
@SAVE@
@PARSE_NTP@
@SYNC_MESSAGE@
@SYNCHRONIZE@
@RESUME_INTERACTIVE@
@STANDBY_DUE@
@ENTER@
@UPDATE@
@CAPTURE_FRAME@
@RENDER_FRAME@
static void renderMenu(Core&);
@RENDER@
@RENDER_MENU@
@MENU_TAP@
@MENU@
@HANDLE_EVENT@
@PREPARE_EXIT@
@EXIT@
@STANDBY_RESULT@;
StandbyResult enterStandby(Core&) { return StandbyResult::TimeRequired; }
const char* standbyErrorMessage(StandbyResult) { return "Clock error"; }
}
const miniapps::App<Core, Event> APPS[] = {
    {"Clock", clock_app::enter, clock_app::update, clock_app::handleEvent, clock_app::render,
     clock_app::exit, clock_app::prepareExit},
};
constexpr int APP_COUNT = 1;
Core* harnessCore;
class AppLauncherState {
 public:
  Renderer& renderer_ = renderer;
  miniapps::Host<Core, Event> app_;
  bool goNetwork_ = false;
  ui::AppMenuView menuView_;
  explicit AppLauncherState(bool open = true) {
    menuView_.selected = ui::AppMenuView::EXTRA_COUNT;
    if (open) {
      app_.restore(APPS[APP_CLOCK]);
      app_.handleEvent(*harnessCore, Event::buttonPress(Button::Center));
      app_.render(*harnessCore);
    }
  }
  void launchApp(Core&, int8_t);
  bool requestSleep(Core&);
  void resumeClock(Core&, const clock_app::ResumeRecord&);
  StateTransition update(Core& core);
  StateTransition dispatchEvents(Core& core) {
    Event event;
    while (core.events.pop(event)) app_.handleEvent(core, event);
    return StateTransition::stay(StateId::AppLauncher);
  }
  void enter(Core& core);
  void exit(Core& core);
  void activateMenuItem(Core& core);
  void poll(Core& core) {
    app_.update(core);
    if (app_.needsRender()) core.cpu.unthrottle();
  }
  bool needsNetwork(Core& core) {
    return core.pendingSync == SyncMode::NtpSync || core.pendingSync == SyncMode::WifiSetup ||
           core.pendingSync == SyncMode::PrinterSetup || core.pendingSync == SyncMode::LocalsendSetup;
  }
  void press(Core& core, Button button) {
    app_.handleEvent(core, Event::buttonPress(button));
    core.cpu.unthrottle();
  }
};
@ACTIVATE@
@LAUNCHER_ENTER@
@LAUNCHER_EXIT@
@RESUME_CLOCK@
@LAUNCH_APP@
@REQUEST_SLEEP@
@LAUNCHER_UPDATE@
int main(int argc, char** argv) {
  assert(argc == 2);
  Core core;
  harnessCore = &core;
  using namespace clock_app;
  state.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[state.ntpSyncSetting]);
  const std::string scenario = argv[1];
  if (scenario == "time-failure-route") {
    seedCredential("home", "secret");
    state.powerMode = PowerMode::Normal;
    state.ntpSyncSetting = NTP_SYNC_COUNT - 1;
    state.menuSelected = static_cast<int8_t>(MenuItem::PowerMode);
    core.clock.updateSucceeds = false;
    AppLauncherState app;
    core.events.push(Event::buttonPress(Button::Right));
    core.events.push(Event::buttonPress(Button::Down));
    core.events.push(Event::buttonPress(Button::Right));
    app.update(core);
    assert(syncs == 1 && !core.clock.canRetainTime());
    renderMenu(core);
    app.app_.render(core);
    nowMs += 15000;
    assert(app.update(core).next == StateId::Network);
    assert(core.pendingSync == SyncMode::NtpSync && core.pendingAppId == APP_CLOCK);
    assert(settingsFile.find("powerMode=1\n") != std::string::npos);
    app.exit(core);
    assert(!app.app_.active());
    return 0;
  }
  if (scenario == "save-failure") {
    for (const bool initiallySaving : {false, true}) {
      settingsExists = true;
      settingsFile = initiallySaving ? "powerMode=1\n" : "powerMode=0\n";
      const auto original = settingsFile;
      loadSettings(core);
      AppLauncherState app;
      state.menuSelected = static_cast<int8_t>(MenuItem::PowerMode);
      core.events.push(Event::buttonPress(Button::Right));
      app.update(core);
      failSettingsWrite = true;
      core.events.push(Event::tap({50, 770}));
      assert(app.update(core).next == StateId::AppLauncher);
      assert(app.app_.menuOpen() && settingsFile == original);
      renderMenu(core);
      app.app_.render(core);
      nowMs += 60000;
      assert(app.update(core).next == StateId::AppLauncher);
      assert(app.app_.menuOpen());
      core.events.push(Event::buttonLongPress(Button::Power));
      assert(app.update(core).next == StateId::AppLauncher);
      assert(app.app_.menuOpen() && settingsFile == original);
      core.events.push(Event::buttonPress(Button::Down));
      core.events.push(Event::buttonPress(Button::Right));
      assert(app.update(core).next == StateId::AppLauncher);
      assert(core.pendingSync == SyncMode::None && core.pendingAppId == -1);
      assert(settingsFile == original);
      failSettingsWrite = false;
      core.events.push(Event::buttonPress(Button::Back));
      assert(app.update(core).next == StateId::AppLauncher);
      assert(app.app_.active() && !app.app_.menuOpen());
      assert(settingsFile.find(initiallySaving ? "powerMode=0\n" : "powerMode=1\n") != std::string::npos);
    }
    return 0;
  }
  if (scenario == "touch-power-mode") {
    for (const auto [width, height] : {std::pair{480, 800}, std::pair{528, 792},
                                      std::pair{800, 480}, std::pair{792, 528}}) {
      for (const uint8_t layout : {0, 1}) {
        renderer.width = width;
        renderer.height = height;
        core.settings.frontButtonLayout = layout;
        state.powerMode = PowerMode::Normal;
        state.settingsChanged = false;
        state.menuSelected = 0;
        AppLauncherState app;
        app.app_.closeMenu();
        const auto tap = [&](int x, int y) {
          core.input.lastActivityMs = millis();
          assert(core.events.push(Event::tap({static_cast<int16_t>(x), static_cast<int16_t>(y)})));
          app.dispatchEvents(core);
        };
        const auto tapText = [&](const char* text) {
          const auto item = std::find_if(renderer.draws.begin(), renderer.draws.end(), [&](const auto& draw) {
            return draw.kind == 'T' && draw.text == text;
          });
          assert(item != renderer.draws.end());
          tap(item->x + item->w / 2, item->y + item->h / 2);
        };
        const auto selectMode = [&] {
          clock_app::render(core, false);
          tapText("Menu");
          assert(app.app_.menuOpen());
          renderer.clearScreen(THEME.backgroundColor);
          renderMenu(core);
          tap(width / 2, 20);
          assert(state.menuSelected == 0);
          tap(width / 2, ui::SettingsListHit::LIST_START_Y + MENU_ITEM_COUNT * THEME.itemHeight + 10);
          assert(state.menuSelected == 0);
          nowMs += 14900;
          tapText("Mode");
          assert(state.menuSelected == static_cast<int8_t>(MenuItem::PowerMode));
          assert(app.app_.needsRender());
          renderer.clearScreen(THEME.backgroundColor);
          renderMenu(core);
          assert(std::any_of(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
            return draw.kind == 'T' && draw.text == "Mode" && draw.black == THEME.selectionTextBlack;
          }));
          if (state.powerMode == PowerMode::PowerSaving) {
            core.input.lastActivityMs = 0;
            nowMs += 14999;
            assert(!standbyDue(core, millis(), false));
            ++nowMs;
            assert(standbyDue(core, millis(), false));
          }
        };
        const int syncsBefore = syncs;
        selectMode();
        tapText(">");
        assert(state.powerMode == PowerMode::PowerSaving);
        assert(!standbyDue(core, millis(), false));
        tapText("Back");
        assert(app.app_.active() && !app.app_.menuOpen());
        assert(settingsFile.find("powerMode=1\n") != std::string::npos);

        ResumeRecord retained;
        retained.settings = state;
        retained.nextNtpUtc = state.nextNtpUtc;
        retained.lastFrame = captureFrame(core, std::time(nullptr), false);
        state.powerMode = PowerMode::Normal;
        nowMs = 0;
        app.app_.stop(core);
        app.resumeClock(core, retained);
        assert(app.app_.active() && !app.app_.menuOpen() && state.powerMode == PowerMode::PowerSaving);
        selectMode();
        tapText(">");
        assert(state.powerMode == PowerMode::Normal);
        tapText("Back");
        assert(app.app_.active() && !app.app_.menuOpen());
        assert(settingsFile.find("powerMode=0\n") != std::string::npos);
        assert(syncs == syncsBefore);
      }
    }
    return 0;
  }
  if (scenario == "overlay") {
    AppLauncherState app;
    state.lastRenderedMin = -1;
    app.poll(core);
    assert(!app.app_.needsRender());
    assert(!core.cpu.throttled);
    core.cpu.throttle();
    app.app_.invalidate();
    app.poll(core);
    assert(!core.cpu.throttled);
    app.app_.render(core);
    app.poll(core);
    assert(!core.cpu.throttled);
    app.press(core, Button::Down);
    assert(!core.cpu.throttled);
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    app.poll(core);
    assert(syncs == 0);
    assert(core.wifi.connects == 0);
  } else if (scenario == "menu") {
    seedCredential("home", "secret");
    AppLauncherState app;
    applyTimezone(0);
    std::tm before{};
    core.clock.localTime(before);
    state.lastRenderedMin = before.tm_min;
    state.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[0]);
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    for (int i = 0; i < 3; ++i) app.press(core, Button::Right);
    assert(writes == 0 && syncs == 0);
    app.app_.render(core);
    nowMs += 60000;
    for (int i = 0; i < 3; ++i) app.poll(core);
    assert(!app.app_.needsRender());
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    app.poll(core);
    assert(syncs == 0 && !app.app_.needsRender());
    app.press(core, Button::Back);
    assert(writes == 1 && settingsFile.find("utcOffset=3\n") != std::string::npos);
    assert(syncs == 0 && core.wifi.connects == 0);
    std::tm after{};
    nowMs = 0;
    core.clock.localTime(after);
    assert(after.tm_hour == (before.tm_hour + 3) % 24);
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    app.press(core, Button::Center);
    app.press(core, Button::Left);
    clock_app::exit(core);
    assert(writes == 2 && settingsFile.find("utcOffset=2\n") != std::string::npos);
  } else if (scenario == "startup") {
    seedCredential("home", "secret");
    AppLauncherState app(false);
    app.activateMenuItem(core);
    assert(app.app_.active() && !app.goNetwork_ && syncs == 0);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.wifi.connects == 0);
    assert(core.cpu.performanceLocks == 0);
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    clock_app::update(core);
    assert(syncs == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    state.ntpSyncSetting = NTP_SYNC_COUNT - 1;
    nowMs += 86400000;
    clock_app::update(core);
    assert(syncs == 1);
    core.clock.valid = false;
    AppLauncherState unsynced(false);
    unsynced.activateMenuItem(core);
    assert(!unsynced.app_.active() && unsynced.goNetwork_);
    assert(core.pendingSync == SyncMode::NtpSync && core.pendingAppId == APP_CLOCK);
    core.wifi.connected_ = true;
    core.wifi.initialized_ = true;
    unsynced.enter(core);
    assert(unsynced.app_.active() && syncs == 2 && core.wifi.connects == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
  } else if (scenario == "startup-cancel" || scenario == "manual-wifi-cancel") {
    core.clock.valid = scenario == "manual-wifi-cancel";
    AppLauncherState app(core.clock.valid);
    if (core.clock.valid) {
      for (int i = 0; i < static_cast<int>(MenuItem::SyncNow); ++i) app.press(core, Button::Down);
      app.press(core, Button::Right);
    } else {
      app.activateMenuItem(core);
    }
    assert((app.goNetwork_ || app.needsNetwork(core)));
    assert(syncs == 0 && core.wifi.connects == 0 && statusFrames == 0);
    app.exit(core);
    assert(!app.app_.active());
    bool goBack_ = true;
    constexpr int returnState_ = 0;
    struct StateTransition { static void to(int) {} };
    auto leaveNetwork = [&] { @NETWORK_BACK@ };
    leaveNetwork();
    app.enter(core);
    assert(!app.app_.active());
    assert(core.pendingSync == SyncMode::None);
    assert(syncs == 0 && core.wifi.connects == 0);
  } else if (scenario == "timeout" || scenario == "sync-failure") {
    seedCredential("home", "secret");
    state.utcOffset = 3;
    applyTimezone(state.utcOffset);
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    std::tm before{};
    core.clock.localTime(before);
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    handleEvent(core, Event::buttonPress(Button::Right), true);
    ntpSynced = scenario != "timeout";
    core.clock.updateSucceeds = scenario != "sync-failure";
    prepareExit(core, miniapps::ExitTarget::Menu);
    clock_app::update(core);
    std::tm after{};
    core.clock.localTime(after);
    assert(after.tm_hour == (before.tm_hour + 1) % 24);
    assert(settingsFile.find("utcOffset=4\n") != std::string::npos);
    assert(writes == 1 && syncs == 1 && core.wifi.connects == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
    delay(1);
    assert(!sntpActive);
  } else if (scenario == "battery-icon") {
    renderer.draws.clear();
    ui::batteryIcon(renderer, THEME, 20, 22, -1, false);
    assert(std::none_of(renderer.draws.begin(), renderer.draws.end(),
                        [](const auto& draw) { return draw.kind == 'T'; }));
    assert(std::any_of(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'R' && draw.x == 20 && draw.y == 22 && draw.w == 30 && draw.h == 14;
    }));
    assert(std::none_of(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'F' && draw.x > 20 && draw.x < 50 && draw.w > 3;
    }));

    renderer.draws.clear();
    ui::batteryIcon(renderer, THEME, 20, 22, 50, true);
    auto halo = std::find_if(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'F' && draw.x == 32 && draw.y == 23 && draw.w == 7 && draw.h == 12;
    });
    assert(halo != renderer.draws.end() && !halo->black);
    assert(std::count_if(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'L' && draw.black;
    }) == 3);

    Theme dark = THEME;
    dark.backgroundColor = 0x00;
    dark.primaryTextBlack = false;
    renderer.draws.clear();
    ui::batteryIcon(renderer, dark, 20, 22, 50, true);
    halo = std::find_if(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'F' && draw.x == 32 && draw.y == 23 && draw.w == 7 && draw.h == 12;
    });
    assert(halo != renderer.draws.end() && halo->black);
    assert(std::count_if(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'L' && !draw.black;
    }) == 3);
  } else if (scenario == "layout") {
    state.utcOffset = -12;
    state.ntpSyncSetting = 2;
    core.battery.status = {true, 100};
    for (const int width : {480, 528}) {
      renderer.width = width;
      renderer.height = width == 528 ? 792 : 800;
      for (int selected = 0; selected < MENU_ITEM_COUNT; ++selected) {
        state.menuSelected = selected;
        renderer.clearScreen(THEME.backgroundColor);
        renderMenu(core);
        bool fullWidthSelection = false, visibleBattery = false;
        for (const auto& draw : renderer.draws) {
          if (draw.kind == 'F' && draw.w == width - 2 * THEME.screenMarginSide)
            fullWidthSelection = true;
          if (draw.kind != 'T') continue;
          assert(draw.x >= 0 && draw.x + draw.w <= width);
          assert(draw.y >= 0 && draw.y + draw.h <= renderer.height);
          if (draw.fontId == THEME.smallFontId && draw.y < 60) visibleBattery = true;
          for (const auto& other : renderer.draws) {
            if (&draw == &other || other.kind != 'T') continue;
            if (draw.y < other.y + other.h && other.y < draw.y + draw.h)
              assert(draw.x + draw.w <= other.x || other.x + other.w <= draw.x);
          }
        }
        assert(fullWidthSelection && !visibleBattery);
      }
      for (const bool valid : {false, true}) {
        core.clock.valid = valid;
        renderer.clearScreen(THEME.backgroundColor);
        clock_app::render(core, false);
        assert(std::any_of(renderer.draws.begin(), renderer.draws.end(), [&](const auto& draw) {
          return draw.kind == 'R' && draw.x == width - 53 && draw.y == 22 && draw.w == 30 && draw.h == 14;
        }));
        assert(std::none_of(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
          return draw.kind == 'T' && draw.text.find('%') != std::string::npos;
        }));
      }
    }
  } else if (scenario == "power-resume") {
    seedCredential("home", "secret");
    ResumeRecord record;
    record.settings.powerMode = PowerMode::PowerSaving;
    record.nextNtpUtc = systemEpoch + 100;
    resumeInteractive(core, &record);
    nowMs = 14999;
    assert(!standbyDue(core, millis(), false));
    nowMs = 15000;
    assert(standbyDue(core, millis(), false));
    nowMs = 99000;
    clock_app::update(core);
    assert(syncs == 0 && core.wifi.connects == 0);
    nowMs = 100000;
    clock_app::update(core);
    assert(syncs == 1 && core.wifi.connects == 1 && writes == 0);
  } else if (scenario == "power-mode") {
    state.menuSelected = static_cast<int8_t>(MenuItem::PowerMode);
    assert(state.powerMode == PowerMode::Normal);
    handleEvent(core, Event::buttonPress(Button::Right), true);
    assert(state.powerMode == PowerMode::PowerSaving);
    state.interactionStartedMs = 0;
    nowMs = 14999;
    assert(!standbyDue(core, millis(), false));
    nowMs = 15000;
    assert(standbyDue(core, millis(), false));
    assert(!standbyDue(core, millis(), true));
    core.input.lastActivityMs = 1;
    assert(!standbyDue(core, millis(), false));
    core.input.lastActivityMs = 0;
    prepareExit(core, miniapps::ExitTarget::Menu);
    assert(settingsFile.find("powerMode=1\n") != std::string::npos);
    handleEvent(core, Event::buttonPress(Button::Left), true);
    assert(state.powerMode == PowerMode::Normal);
    nowMs += 15000;
    assert(!standbyDue(core, millis(), false));
    prepareExit(core, miniapps::ExitTarget::Menu);
    assert(settingsFile.find("powerMode=0\n") != std::string::npos);
  } else if (scenario == "settings-menu") {
    AppLauncherState app;
    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
      renderMenu(core);
      const bool syncRow = i == static_cast<int>(MenuItem::SyncNow);
      assert(ui::leftHint == (syncRow ? "" : "<"));
      assert(ui::rightHint == (syncRow ? "Sync" : ">"));
      app.app_.render(core);
      app.press(core, Button::Center);
      assert(!app.app_.needsRender());
      app.press(core, Button::Down);
    }
    app.press(core, Button::Back);
    assert(writes == 0 && syncs == 0 && core.wifi.connects == 0);
  } else if (scenario == "manual-sync" || scenario == "manual-wifi") {
    if (scenario == "manual-sync") seedCredential("home", "secret");
    core.clock.valid = true;
    systemEpoch = 1577836800;
    state.ntpSyncSetting = NTP_SYNC_COUNT - 1;
    enter(core);
    AppLauncherState app;
    for (int i = 0; i < static_cast<int>(MenuItem::SyncNow); ++i) app.press(core, Button::Down);
    app.press(core, Button::Left);
    assert(syncs == 0 && writes == 0);
    app.press(core, Button::Right);
    if (scenario == "manual-wifi") {
      assert(core.pendingSync == SyncMode::NtpSync && core.pendingAppId == APP_CLOCK);
      assert(app.needsNetwork(core));
      assert(syncs == 0 && statusFrames == 0);
      app.exit(core);
      assert(!app.app_.active());
      core.wifi.connected_ = true;
      core.wifi.initialized_ = true;
      app.enter(core);
      assert(app.app_.active() && core.wifi.connects == 0);
    }
    assert(syncs == 1 && core.pendingSync == SyncMode::None);
    assert(!core.wifi.isInitialized() && core.cpu.performanceLocks == 0);
    app.press(core, Button::Back);
    assert(syncs == 1 && writes == 0);
  } else if (scenario == "local-edit-off") {
    seedCredential("home", "secret");
    core.clock.valid = true;
    applyTimezone(0);
    state.ntpSyncSetting = NTP_SYNC_COUNT - 1;
    state.menuSelected = static_cast<int8_t>(MenuItem::DateFormat);
    handleEvent(core, Event::buttonPress(Button::Right), true);
    prepareExit(core, miniapps::ExitTarget::Menu);
    assert(writes == 1);
    assert(settingsFile.find("dateFmt=1\n") != std::string::npos);
    assert(settingsFile.find("ntpSync=3\n") != std::string::npos);
    assert(core.wifi.connects == 0 && syncs == 0);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
  } else if (scenario == "timezone-only") {
    seedCredential("home", "secret");
    core.clock.valid = true;
    applyTimezone(0);
    std::tm before{};
    core.clock.localTime(before);
    const std::time_t epochBefore = 1780000000 + nowMs / 1000;
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    handleEvent(core, Event::buttonPress(Button::Right), true);
    prepareExit(core, miniapps::ExitTarget::Menu);
    assert(writes == 1 && settingsFile.find("utcOffset=1\n") != std::string::npos);
    assert(core.wifi.connects == 0 && syncs == 0);
    assert(!core.wifi.isConnected());
    std::tm after{};
    core.clock.localTime(after);
    assert(after.tm_hour == (before.tm_hour + 1) % 24);
    const std::time_t epochAfter = 1780000000 + nowMs / 1000;
    assert(epochAfter == epochBefore);
    assert(std::mktime(&after) == epochAfter);
  } else if (scenario == "invalid-time-schedule") {
    seedCredential("home", "secret");
    core.clock.valid = false;
    systemEpoch = 0;
    ntpSynced = false;
    AppLauncherState app(false);
    app.launchApp(core, APP_CLOCK);
    assert(syncs == 1 && core.wifi.connects == 1);
    int frames = 0;
    for (int i = 0; i < (10800000 + 60000) / 50; ++i) {
      nowMs += 50;
      if (clock_app::update(core)) {
        ++frames;
        clock_app::render(core, false);
      }
    }
    assert(syncs == 2 && frames == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
    state.ntpSyncSetting = NTP_SYNC_COUNT - 1;
    for (int i = 0; i < (10800000 + 60000) / 50; ++i) {
      nowMs += 50;
      assert(!clock_app::update(core));
    }
    assert(syncs == 2 && frames == 1);
  } else if (scenario == "validity-transition") {
    seedCredential("home", "secret");
    core.clock.valid = false;
    AppLauncherState app(false);
    app.launchApp(core, APP_CLOCK);
    assert(syncs == 1);
    clock_app::render(core, false);
    for (int i = 0; i < 300; ++i) {
      nowMs += 50;
      assert(!clock_app::update(core));
    }
    core.clock.valid = true;
    assert(clock_app::update(core));
    clock_app::render(core, false);
    assert(!clock_app::update(core));
    nowMs += 60000;
    assert(clock_app::update(core));
    clock_app::render(core, false);
    core.clock.valid = false;
    assert(clock_app::update(core));
    clock_app::render(core, false);
    for (int i = 0; i < 300; ++i) {
      nowMs += 50;
      assert(!clock_app::update(core));
    }
  } else if (scenario == "wrap") {
    seedCredential("home", "secret");
    core.clock.valid = true;
    applyTimezone(0);
    state.ntpSyncSetting = 0;
    const auto due = state.nextNtpUtc;
    nowMs = 4294967290u;
    systemEpoch = due - 1 - nowMs / 1000;
    clock_app::update(core);
    assert(syncs == 0);
    nowMs = 0;
    systemEpoch = due;
    clock_app::update(core);
    assert(syncs == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    systemEpoch = state.nextNtpUtc;
    clock_app::update(core);
    assert(syncs == 2);
  } else if (scenario == "retry-baseline") {
    seedCredential("home", "secret");
    ntpSynced = false;
    state.ntpSyncSetting = 0;
    core.clock.valid = true;
    applyTimezone(0);
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    clock_app::update(core);
    assert(syncs == 1);
    const int64_t nextAttempt = state.nextNtpUtc;
    delay(1);
    assert(!sntpActive);
    nowMs = 0;
    systemEpoch = nextAttempt - 1;
    clock_app::update(core);
    assert(syncs == 1);
    systemEpoch = nextAttempt;
    clock_app::update(core);
    assert(syncs == 2);
    delay(1);
    assert(!sntpActive);
    assert(!core.wifi.isConnected() && core.cpu.performanceLocks == 0);
  } else if (scenario == "radio-inherited-connected" || scenario == "radio-inherited-initialized") {
    seedCredential("home", "secret");
    core.clock.valid = true;
    applyTimezone(0);
    core.wifi.connected_ = scenario == "radio-inherited-connected";
    core.wifi.initialized_ = true;
    enter(core);
    assert(syncs == 0 && core.wifi.connects == 0);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
    assert(!sntpActive);
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    clock_app::update(core);
    assert(syncs == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
  } else if (scenario == "connected-enter" || scenario == "sync-cleanup") {
    seedCredential("home", "secret");
    core.clock.valid = false;
    const int expectedConnections = scenario == "connected-enter" ? 0 : 1;
    core.wifi.connected_ = expectedConnections == 0;
    core.wifi.initialized_ = core.wifi.connected_;
    enter(core);
    assert(syncs == 1 && core.wifi.connects == expectedConnections);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
    delay(5000);
    assert(!sntpActive);
    core.clock.valid = true;
    std::tm before{};
    core.clock.localTime(before);
    deliverNtpPacket(systemEpoch + 86400);
    std::tm after{};
    core.clock.localTime(after);
    assert(std::mktime(&after) == std::mktime(&before));
    nowMs += NTP_SYNC_SECONDS[0] * 1000;
    clock_app::update(core);
    assert(syncs == 2 && core.wifi.connects == expectedConnections + 1 && writes == 0);
    assert(!core.wifi.isConnected() && core.cpu.performanceLocks == 0);
  } else if (scenario == "no-credentials") {
    core.clock.valid = false;
    enter(core);
    assert(syncs == 0 && core.wifi.connects == 0);
    assert(!core.wifi.isInitialized() && !core.wifi.isConnected());
    assert(core.cpu.performanceLocks == 0);
    assert(!sntpActive);
    assert(statusFrames == 0 && nowMs == 0);
    AppLauncherState app;
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    app.press(core, Button::Right);
    nowMs = NTP_SYNC_SECONDS[0] * 1000;
    app.poll(core);
    app.press(core, Button::Back);
    app.poll(core);
    assert(writes == 1 && settingsFile.find("utcOffset=1\n") != std::string::npos);
    assert(statusFrames == 0 && nowMs == NTP_SYNC_SECONDS[0] * 1000);
    assert(syncs == 0 && core.wifi.connects == 0);
    seedCredential("home", "secret");
    nowMs += NTP_SYNC_SECONDS[0] * 1000;
    app.poll(core);
    assert(syncs == 1 && core.wifi.connects == 1);
    assert(!core.wifi.isInitialized() && core.cpu.performanceLocks == 0);
  } else if (scenario == "all-credentials-fail") {
    seedCredential("a", "p");
    seedCredential("b", "q");
    core.wifi.connectFails = true;
    core.clock.valid = false;
    enter(core);
    assert(syncs == 0 && !sntpActive);
    assert(core.wifi.connects == 2);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
  } else if (scenario == "sync-budget" || scenario == "sync-cancel") {
    seedCredential("first", "p");
    seedCredential("second", "q");
    radioConnectFails = scenario == "sync-budget";
    radioDelayMs = 5000;
    cancelPower = scenario == "sync-cancel";
    cancelAtMs = 250;
    const auto result = synchronize(core, state, false);
    assert(result == (cancelPower ? SyncResult::Cancelled : SyncResult::Failed));
    assert(nowMs <= (cancelPower ? 270u : 15000u));
    assert(core.wifi.connects == 1 && syncs == 0 && statusFrames == 0);
    assert(!core.wifi.isInitialized() && core.cpu.performanceLocks == 0);
    assert(std::strcmp(wifiStore.creds[0].ssid, "first") == 0);
  } else if (scenario == "connect-zero") {
    assert(!core.wifi.connect("home", "p", 0, nullptr).ok());
    assert(!core.wifi.isInitialized() && core.wifi.connects == 0);
  } else if (scenario == "connect-wrap") {
    nowMs = UINT32_MAX - 50;
    radioDelayMs = 60;
    assert(core.wifi.connect("home", "p", 500, nullptr).ok());
    assert(core.wifi.isConnected() && nowMs < 500);
  } else if (scenario == "face-migration") {
    settingsExists = false;
    state.face = clock_faces::Face::DayNight;
    loadSettings(core);
    assert(state.face == clock_faces::Face::Big);

    settingsExists = true;
    settingsFile = "utcOffset=0\nuse24h=1\n";
    state.face = clock_faces::Face::Big;
    loadSettings(core);
    assert(state.face == clock_faces::Face::Retro);

    settingsFile += "face=7\n";
    loadSettings(core);
    assert(state.face == clock_faces::Face::DayNight);

    settingsFile = "face=1\n";
    loadSettings(core);
    assert(state.face == clock_faces::Face::Retro);
    settingsFile = "face=99\n";
    loadSettings(core);
    assert(state.face == clock_faces::Face::Retro);
    settingsFile = "face=\n";
    loadSettings(core);
    assert(state.face == clock_faces::Face::Retro);
    settingsFile = "face=garbage\n";
    loadSettings(core);
    assert(state.face == clock_faces::Face::Retro);
    settingsFile = "face=7junk\n";
    loadSettings(core);
    assert(state.face == clock_faces::Face::Retro);
  } else if (scenario == "face-selection") {
    state.menuSelected = static_cast<int8_t>(MenuItem::Face);
    state.face = clock_faces::Face::Big;
    const clock_faces::Face expected[] = {
        clock_faces::Face::Analog,
        clock_faces::Face::Retro,
        clock_faces::Face::Flip,
        clock_faces::Face::DayNight,
        clock_faces::Face::Big,
    };
    for (clock_faces::Face face : expected) {
      handleEvent(core, Event::buttonPress(Button::Right), true);
      assert(state.face == face);
    }
    handleEvent(core, Event::buttonPress(Button::Left), true);
    assert(state.face == clock_faces::Face::DayNight);
    prepareExit(core, miniapps::ExitTarget::Menu);
    assert(settingsFile.find("face=7\n") != std::string::npos);
  } else if (scenario == "face-render") {
    core.battery.reads = 0;
    clock_faces::renderCalls = 0;
    state.face = clock_faces::Face::Flip;
    state.use24h = false;
    state.dateFormat = 3;
    render(core, false);
    assert(clock_faces::renderCalls == 1);
    assert(clock_faces::lastFace == clock_faces::Face::Flip);
    assert(!clock_faces::lastUse24h && clock_faces::lastDateFormat == 3);
    assert(core.battery.reads == 1);
    assert(std::none_of(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
      return draw.kind == 'T' && draw.text.find('%') != std::string::npos;
    }));
  } else {
    assert(false);
  }
}
'''


def main():
    clock = (ROOT / "src/apps/ClockApp.cpp").read_text()
    launcher = (ROOT / "src/states/AppLauncherState.cpp").read_text()
    network = (ROOT / "src/states/NetworkState.cpp").read_text()
    wifi = (ROOT / "src/hal/WifiRadio.h").read_text()
    wifi_source = (ROOT / "src/hal/WifiRadio.cpp").read_text()
    power_source = (ROOT / "src/apps/ClockPower.cpp").read_text()
    clock_header = (ROOT / "src/apps/ClockApp.h").read_text()
    elements = (ROOT / "src/ui/Elements.cpp").read_text()
    app_input = (ROOT / "src/apps/MiniAppInput.cpp").read_text()
    state = clock[clock.index("static constexpr int NTP_SYNC_COUNT"):
                  clock.index("static void applyTimezone")]
    settings_header = (ROOT / "src/apps/ClockSettings.h").read_text()
    settings_source = (ROOT / "src/apps/ClockSettings.cpp").read_text()
    power_header = (ROOT / "src/apps/ClockPower.h").read_text()
    frame_source = (ROOT / "src/apps/ClockFrame.cpp").read_text()
    settings_view = (ROOT / "src/ui/views/SettingsViews.h").read_text()
    app_menu_view = (ROOT / "src/ui/views/AppLauncherViews.h").read_text()
    replacements = {
        "APP_MENU_VIEW": block(app_menu_view, "struct AppMenuView"),
        "SETTINGS_HIT": block(settings_view, "struct SettingsListHit"),
        "SETTINGS_HIT_TEST": block(settings_view, "inline SettingsListHit settingsListHitTest("),
        "RESUME_CLOCK": block(launcher, "void AppLauncherState::resumeClock("),
        "LAUNCHER_UPDATE": block(launcher, "StateTransition AppLauncherState::update("),
        "STANDBY_RESULT": block(power_header, "enum class StandbyResult"),
        "LAUNCH_APP": block(launcher, "void AppLauncherState::launchApp("),
        "REQUEST_SLEEP": block(launcher, "bool AppLauncherState::requestSleep("),
        "MINI_APP_BUTTON": block(app_input, "bool miniAppButton("),
        "HANDLE_APP_BUTTONS": block(app_input, "miniapps::Action handleAppButtons("),
        "SETTINGS_MODEL": settings_header.split("namespace papyrix::clock_app {", 1)[1].rsplit("}", 1)[0],
        "SETTINGS_STORAGE": settings_source.split("namespace papyrix::clock_app {", 1)[1].rsplit("}", 1)[0],
        "FRAME_MODEL": "struct ClockAppearance" + power_header.split("struct ClockAppearance", 1)[1].split("struct ResumeIdentity", 1)[0],
        "CAPTURE_FRAME": block(clock, "ClockFrame captureFrame("),
        "RENDER_FRAME": block(frame_source, "void renderFrame("),
        "BATTERY_BODY": block(elements, "static void drawBatteryBody("),
        "BATTERY_ICON": block(elements, "void batteryIcon("),
        "BATTERY": block(elements, "void battery("),
        "TITLE": block(elements, "void title("),
        "ENUM_VALUE": block(elements, "void enumValue("),
        "MENU_ITEM": block(elements, "void menuItem("),
        "BUTTON_BOUNDS": block(elements, "touch::Rect buttonBarButtonBounds("),
        "DRAW_BUTTON_BAR": block(elements, "static void drawButtonBar("),
        "WIFI_SESSION": block(wifi, "class WifiSession final") + ";",
        "WIFI_CONNECT": block(wifi_source, "Result<void> WifiRadio::connect(").replace(
            "Result<void> WifiRadio::connect(", "Result connect(", 1),
        "NEXT_SYNC": block(power_source, "int64_t nextSyncUtc("),
        "INTERACTION_EXPIRED": block(power_source, "bool interactionExpired("),
        "STANDBY_DUE": block(clock, "bool standbyDue("),
        "RESUME_INTERACTIVE": block(clock, "void resumeInteractive("),
        "SYNC_RESULT": block(clock_header, "enum class SyncResult"),
        "STATE": state,
        "TIMEZONE": block(clock, "static void applyTimezone("),
        "LOAD": block(clock, "static void loadSettings("),
        "SAVE": block(clock, "bool saveSettings("),
        "PARSE_NTP": block(clock, "static int parseNtpServers("),
        "SYNC_MESSAGE": block(clock, "static void showSyncMessage("),
        "SYNCHRONIZE": block(clock, "SyncResult synchronize("),
        "ENTER": block(clock, "void enter("),
        "UPDATE": block(clock, "bool update("),
        "RENDER": block(clock, "miniapps::Frame render("),
        "RENDER_MENU": block(clock, "static void renderMenu(Core& core) {"),
        "MENU_TAP": block(clock, "bool onMenuTap("),
        "MENU": block(clock, "void onMenuButton("),
        "HANDLE_EVENT": block(clock, "miniapps::Action handleEvent("),
        "PREPARE_EXIT": block(clock, "miniapps::ExitResult prepareExit("),
        "EXIT": block(clock, "void exit("),
        "ACTIVATE": block(launcher, "void AppLauncherState::activateMenuItem("),
        "LAUNCHER_ENTER": block(launcher, "void AppLauncherState::enter("),
        "LAUNCHER_EXIT": block(launcher, "void AppLauncherState::exit("),
        "NETWORK_BACK": block(network, "if (goBack_)"),
    }
    harness = HARNESS
    for marker, code in replacements.items():
        harness = harness.replace(f"@{marker}@", code)
    with tempfile.TemporaryDirectory(prefix="papyrix-clock-") as directory:
        path = Path(directory)
        (path / "Arduino.h").write_text("#pragma once\n#include <cstdint>\n#define PROGMEM\n")
        source = path / "clock.cpp"
        source.write_text(harness)
        binary = path / "clock"
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", f"-I{path}",
            f"-I{ROOT / 'lib/EpdFont/src'}", f"-I{ROOT / 'lib/Utf8/src'}", f"-I{ROOT / 'src'}",
            f"-I{ROOT / 'lib/MiniApps/src'}",
            str(source), str(ROOT / "lib/EpdFont/src/EpdFont.cpp"), str(ROOT / "lib/Utf8/src/Utf8.cpp"),
            str(ROOT / "src/ui/TouchLayout.cpp"), "-o", str(binary),
        ], check=True)
        failed = False
        scenarios = (
            "overlay", "menu", "startup", "startup-cancel", "timeout", "sync-failure", "settings-menu",
            "local-edit-off", "timezone-only",
            "invalid-time-schedule", "validity-transition", "wrap", "retry-baseline",
            "radio-inherited-connected", "radio-inherited-initialized", "connected-enter",
            "sync-cleanup", "no-credentials", "all-credentials-fail",
            "sync-budget", "sync-cancel", "connect-zero", "connect-wrap",
            "manual-sync", "manual-wifi", "manual-wifi-cancel",
            "battery-icon", "face-migration", "face-selection", "face-render",
            "layout", "power-mode", "power-resume", "touch-power-mode",
            "time-failure-route",
            "save-failure",
        )
        for scenario in scenarios:
            result = subprocess.run([str(binary), scenario])
            failed |= result.returncode != 0
            print(f"{'PASS' if result.returncode == 0 else 'FAIL'}: Clock {scenario}", flush=True)
        if failed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
