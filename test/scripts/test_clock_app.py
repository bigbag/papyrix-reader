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
#include <EpdFontFamily.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <ui/TouchLayout.h>
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
struct FsFile {
  void write(const uint8_t* data, size_t length) {
    settingsFile.assign(reinterpret_cast<const char*>(data), length);
    ++writes;
  }
  void close() {}
};
enum class Button { Up, Down, Left, Right, Back, Center };
enum class SyncMode { None, NtpSync, FileTransfer, CalibreWireless, WifiSetup, PrinterSetup, LocalsendSetup };
struct Result {
  size_t value = 0;
  bool okValue = true;
  bool ok() const { return okValue; }
};
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
  int connects = 0;
  bool connectFails = false;
  bool isConnected() const { return connected_; }
  bool isInitialized() const { return initialized_; }
  Result connect(const char*, const char*) {
    ++connects;
    initialized_ = true;
    if (connectFails) return {0, false};
    connected_ = true;
    return {};
  }
  void shutdown() {
    connected_ = false;
    initialized_ = false;
  }
};
@WIFI_SESSION@
}  // namespace hal
struct Core {
  struct Clock {
    bool valid = true;
    bool updateSucceeds = true;
    bool updateFromSystem() { return updateSucceeds; }
    bool localTime(std::tm& out) {
      if (!valid) return false;
      const std::time_t epoch = systemEpoch + nowMs / 1000;
      out = *std::localtime(&epoch);
      return true;
    }
  } clock;
  struct Storage {
    Result readToBuffer(const char*, char* buf, size_t size) {
      if (!settingsExists) return {0, false};
      std::snprintf(buf, size, "%s", settingsFile.c_str());
      return {settingsFile.size(), true};
    }
    void mkdir(const char*) {}
    Result openWrite(const char*, FsFile&) {
      settingsExists = true;
      return {};
    }
  } storage;
  struct Input { void resetIdleTimer() {} } input;
  struct Battery {
    struct Status {
      bool percentageKnown;
      unsigned percentage;
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
namespace papyrix::hal {
struct Display { static constexpr int HALF_REFRESH = 0, FAST_REFRESH = 1; };
}
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
  void displayBuffer(int = 0, bool = false) {}
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
struct Theme {
  int uiFontId = 0, smallFontId = 1, readerFontId = 2, backgroundColor = 255;
  int screenMarginTop = 9, screenMarginSide = 3, itemHeight = 30, itemSpacing = 0;
  int itemPaddingX = 8, itemValuePadding = 20;
  bool primaryTextBlack = true, secondaryTextBlack = true;
  bool selectionFillBlack = true, selectionTextBlack = false;
} THEME;
namespace ui {
void centeredMessage(Renderer&, const Theme&, int, const char*) { ++statusFrames; }
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
  ButtonBar(const char* a, const char* b, const char* l, const char* r) : left(l), right(r), labels{a, b, l, r} {}
};
void buttonBar(Renderer& r, const Theme& t, const ButtonBar& buttons) {
  leftHint = buttons.left;
  rightHint = buttons.right;
  drawButtonBar(r, t, buttons.labels);
}
struct AppMenuView { static constexpr int EXTRA_COUNT = 2; };
struct SettingsListHit { static constexpr int LIST_START_Y = 60; };
}
constexpr int APP_CLOCK = 0;
constexpr int APP_POMODORO = 1;
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
@STATE@
@TIMEZONE@
@LOAD@
@SAVE@
@PARSE_NTP@
@SYNC_NTP@
@SYNC_AUTO@
@ENTER@
@UPDATE@
@RENDER@
@RENDER_MENU@
@MENU@
@EXIT@
}
struct App {
  bool (*update)(Core&);
  void (*onMenuButton)(Core&, Button);
} APPS[] = {{clock_app::update, clock_app::onMenuButton}};
constexpr int APP_COUNT = 1;
class AppLauncherState {
 public:
  enum class Mode { Menu, App, Overlay };
  Mode mode_ = Mode::Overlay;
  int activeApp_ = APP_CLOCK;
  bool needsRender_ = false, goNetwork_ = false, launched = false;
  struct MenuView {
    int selected = ui::AppMenuView::EXTRA_COUNT, appCount = 0, itemCount = 0;
    bool needsRender = false;
  } menuView_;
  void launchApp(Core& core) { launched = true; clock_app::enter(core); }
  void hideOverlay() { mode_ = Mode::App; }
  void enter(Core& core);
  void activateMenuItem(Core& core);
  void poll(Core& core) { @POLL@ }
  bool needsNetwork(Core& core) {
    enum class StateId { Network };
    struct StateTransition { static bool to(StateId) { return true; } };
    @NETWORK_ROUTE@
    return false;
  }
  void press(Core& core, Button button) {
    struct { Button button; } e{button};
    do { @OVERLAY@ } while (false);
  }
};
@ACTIVATE@
@LAUNCHER_ENTER@
int main(int argc, char** argv) {
  assert(argc == 2);
  Core core;
  using namespace clock_app;
  const std::string scenario = argv[1];
  if (scenario == "overlay") {
    AppLauncherState app;
    state.lastRenderedMin = -1;
    app.poll(core);
    assert(!app.needsRender_);
    assert(!core.cpu.throttled);
    core.cpu.throttle();
    app.needsRender_ = true;
    app.poll(core);
    assert(!core.cpu.throttled);
    app.needsRender_ = false;
    app.poll(core);
    assert(!core.cpu.throttled);
    app.press(core, Button::Down);
    assert(!core.cpu.throttled);
    nowMs = NTP_SYNC_INTERVALS[0];
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
    state.lastNtpSyncMs = millis();
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    for (int i = 0; i < 3; ++i) app.press(core, Button::Right);
    assert(writes == 0 && syncs == 0);
    app.needsRender_ = false;
    nowMs += 60000;
    for (int i = 0; i < 3; ++i) app.poll(core);
    assert(!app.needsRender_);
    nowMs = NTP_SYNC_INTERVALS[0];
    app.poll(core);
    assert(syncs == 0 && !app.needsRender_);
    app.press(core, Button::Back);
    assert(writes == 1 && settingsFile.find("utcOffset=3\n") != std::string::npos);
    assert(syncs == 0 && core.wifi.connects == 0);
    std::tm after{};
    nowMs = 0;
    core.clock.localTime(after);
    assert(after.tm_hour == (before.tm_hour + 3) % 24);
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    app.mode_ = AppLauncherState::Mode::Overlay;
    app.press(core, Button::Left);
    clock_app::exit(core);
    assert(writes == 2 && settingsFile.find("utcOffset=2\n") != std::string::npos);
  } else if (scenario == "startup") {
    seedCredential("home", "secret");
    AppLauncherState app;
    app.activateMenuItem(core);
    assert(app.launched && !app.goNetwork_ && syncs == 0);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.wifi.connects == 0);
    assert(core.cpu.performanceLocks == 0);
    nowMs = NTP_SYNC_INTERVALS[0];
    clock_app::update(core);
    assert(syncs == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    state.ntpSyncSetting = NTP_SYNC_COUNT - 1;
    nowMs += 86400000;
    clock_app::update(core);
    assert(syncs == 1);
    core.clock.valid = false;
    AppLauncherState unsynced;
    unsynced.activateMenuItem(core);
    assert(!unsynced.launched && unsynced.goNetwork_);
    assert(core.pendingSync == SyncMode::NtpSync && core.pendingAppId == APP_CLOCK);
    core.wifi.connected_ = true;
    core.wifi.initialized_ = true;
    unsynced.enter(core);
    assert(unsynced.launched && syncs == 2 && core.wifi.connects == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    assert(core.cpu.performanceLocks == 0);
  } else if (scenario == "startup-cancel" || scenario == "manual-wifi-cancel") {
    core.clock.valid = scenario == "manual-wifi-cancel";
    AppLauncherState app;
    if (core.clock.valid) {
      for (int i = 0; i < 5; ++i) app.press(core, Button::Down);
      app.press(core, Button::Right);
    } else {
      app.activateMenuItem(core);
    }
    assert(!app.launched && (app.goNetwork_ || app.needsNetwork(core)));
    bool goBack_ = true;
    constexpr int returnState_ = 0;
    struct StateTransition { static void to(int) {} };
    auto leaveNetwork = [&] { @NETWORK_BACK@ };
    leaveNetwork();
    app.enter(core);
    assert(!app.launched && app.mode_ == AppLauncherState::Mode::Menu);
    assert(core.pendingSync == SyncMode::None);
    assert(syncs == 0 && core.wifi.connects == 0 && statusFrames == 0);
  } else if (scenario == "timeout" || scenario == "sync-failure") {
    seedCredential("home", "secret");
    state.utcOffset = 3;
    applyTimezone(state.utcOffset);
    nowMs = NTP_SYNC_INTERVALS[0];
    std::tm before{};
    core.clock.localTime(before);
    state.menuSelected = static_cast<int8_t>(MenuItem::UtcOffset);
    onMenuButton(core, Button::Right);
    ntpSynced = scenario != "timeout";
    core.clock.updateSucceeds = scenario != "sync-failure";
    onMenuButton(core, Button::Back);
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
        clock_app::render(core);
        assert(std::any_of(renderer.draws.begin(), renderer.draws.end(), [&](const auto& draw) {
          return draw.kind == 'R' && draw.x == width - 53 && draw.y == 22 && draw.w == 30 && draw.h == 14;
        }));
        assert(std::none_of(renderer.draws.begin(), renderer.draws.end(), [](const auto& draw) {
          return draw.kind == 'T' && draw.text.find('%') != std::string::npos;
        }));
      }
    }
  } else if (scenario == "settings-menu") {
    AppLauncherState app;
    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
      renderMenu(core);
      const bool syncRow = i == static_cast<int>(MenuItem::SyncNow);
      assert(ui::leftHint == (syncRow ? "" : "<"));
      assert(ui::rightHint == (syncRow ? "Sync" : ">"));
      app.needsRender_ = false;
      app.press(core, Button::Center);
      assert(!app.needsRender_);
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
    for (int i = 0; i < 5; ++i) app.press(core, Button::Down);
    app.press(core, Button::Left);
    assert(syncs == 0 && writes == 0);
    app.press(core, Button::Right);
    if (scenario == "manual-wifi") {
      assert(core.pendingSync == SyncMode::NtpSync && core.pendingAppId == APP_CLOCK);
      assert(app.needsNetwork(core));
      assert(syncs == 0 && statusFrames == 0);
      core.wifi.connected_ = true;
      core.wifi.initialized_ = true;
      app.enter(core);
      assert(app.launched && core.wifi.connects == 0);
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
    onMenuButton(core, Button::Right);
    onMenuButton(core, Button::Back);
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
    onMenuButton(core, Button::Right);
    onMenuButton(core, Button::Back);
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
    ntpSynced = false;
    AppLauncherState app;
    app.launchApp(core);
    assert(syncs == 1 && core.wifi.connects == 1);
    int frames = 0;
    for (int i = 0; i < (10800000 + 60000) / 50; ++i) {
      nowMs += 50;
      if (clock_app::update(core)) {
        ++frames;
        clock_app::render(core);
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
    AppLauncherState app;
    app.launchApp(core);
    assert(syncs == 1);
    clock_app::render(core);
    for (int i = 0; i < 300; ++i) {
      nowMs += 50;
      assert(!clock_app::update(core));
    }
    core.clock.valid = true;
    assert(clock_app::update(core));
    clock_app::render(core);
    assert(!clock_app::update(core));
    nowMs += 60000;
    assert(clock_app::update(core));
    clock_app::render(core);
    core.clock.valid = false;
    assert(clock_app::update(core));
    clock_app::render(core);
    for (int i = 0; i < 300; ++i) {
      nowMs += 50;
      assert(!clock_app::update(core));
    }
  } else if (scenario == "wrap") {
    seedCredential("home", "secret");
    core.clock.valid = true;
    applyTimezone(0);
    state.ntpSyncSetting = 0;
    state.lastNtpSyncMs = 4294967290u;
    nowMs = 4294967290u;
    for (int i = 0; i < (10800000 + 60000) / 50; ++i) {
      nowMs += 50;
      clock_app::update(core);
    }
    assert(syncs == 1);
    assert(!core.wifi.isConnected() && !core.wifi.isInitialized());
    nowMs = 10799994u + 10800000u;
    clock_app::update(core);
    assert(syncs == 2);
  } else if (scenario == "retry-baseline") {
    seedCredential("home", "secret");
    ntpSynced = false;
    state.ntpSyncSetting = 0;
    state.lastNtpSyncMs = 0;
    core.clock.valid = true;
    applyTimezone(0);
    nowMs = NTP_SYNC_INTERVALS[0];
    clock_app::update(core);
    assert(syncs == 1);
    const uint32_t firstAttemptEnd = nowMs;
    delay(1);
    assert(!sntpActive);
    nowMs = firstAttemptEnd + NTP_SYNC_INTERVALS[0] - 1;
    clock_app::update(core);
    assert(syncs == 1);
    ++nowMs;
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
    nowMs = NTP_SYNC_INTERVALS[0];
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
    nowMs += NTP_SYNC_INTERVALS[0];
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
    nowMs = NTP_SYNC_INTERVALS[0];
    app.poll(core);
    app.press(core, Button::Back);
    app.poll(core);
    assert(writes == 1 && settingsFile.find("utcOffset=1\n") != std::string::npos);
    assert(statusFrames == 0 && nowMs == NTP_SYNC_INTERVALS[0]);
    assert(syncs == 0 && core.wifi.connects == 0);
    seedCredential("home", "secret");
    nowMs += NTP_SYNC_INTERVALS[0];
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
      onMenuButton(core, Button::Right);
      assert(state.face == face);
    }
    onMenuButton(core, Button::Left);
    assert(state.face == clock_faces::Face::DayNight);
    onMenuButton(core, Button::Back);
    assert(settingsFile.find("face=7\n") != std::string::npos);
  } else if (scenario == "face-render") {
    core.battery.reads = 0;
    clock_faces::renderCalls = 0;
    state.face = clock_faces::Face::Flip;
    state.use24h = false;
    state.dateFormat = 3;
    render(core);
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
    elements = (ROOT / "src/ui/Elements.cpp").read_text()
    state = clock[clock.index("static constexpr const char* SETTINGS_PATH"):
                  clock.index("static void applyTimezone")]
    replacements = {
        "BATTERY_BODY": block(elements, "static void drawBatteryBody("),
        "BATTERY_ICON": block(elements, "void batteryIcon("),
        "BATTERY": block(elements, "void battery("),
        "TITLE": block(elements, "void title("),
        "ENUM_VALUE": block(elements, "void enumValue("),
        "MENU_ITEM": block(elements, "void menuItem("),
        "BUTTON_BOUNDS": block(elements, "touch::Rect buttonBarButtonBounds("),
        "DRAW_BUTTON_BAR": block(elements, "static void drawButtonBar("),
        "WIFI_SESSION": block(wifi, "class WifiSession final") + ";",
        "STATE": state,
        "TIMEZONE": block(clock, "static void applyTimezone("),
        "LOAD": block(clock, "static void loadSettings("),
        "SAVE": block(clock, "static void saveSettings("),
        "PARSE_NTP": block(clock, "static int parseNtpServers("),
        "SYNC_NTP": block(clock, "static void syncNtpWithConnection("),
        "SYNC_AUTO": block(clock, "void syncNtpAutoConnect("),
        "ENTER": block(clock, "void enter("),
        "UPDATE": block(clock, "bool update("),
        "RENDER": block(clock, "bool render("),
        "RENDER_MENU": block(clock, "void renderMenu("),
        "MENU": block(clock, "void onMenuButton("),
        "EXIT": block(clock, "void exit("),
        "POLL": block(launcher, "if ((mode_ == Mode::App"),
        "OVERLAY": launcher[launcher.index("case Mode::Overlay:"):].split(
            "if (e.type == EventType::ButtonRepeat) break;", 1
        )[1].split("\n        break;", 1)[0],
        "ACTIVATE": block(launcher, "void AppLauncherState::activateMenuItem("),
        "LAUNCHER_ENTER": block(launcher, "void AppLauncherState::enter("),
        "NETWORK_BACK": block(network, "if (goBack_)"),
        "NETWORK_ROUTE": launcher[launcher.index("  if (core.pendingSync == SyncMode::WifiSetup)"):
                                  launcher.index("  if (goNetwork_)")],
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
            "manual-sync", "manual-wifi", "manual-wifi-cancel",
            "battery-icon", "face-migration", "face-selection", "face-render",
            "layout",
        )
        for scenario in scenarios:
            result = subprocess.run([str(binary), scenario])
            failed |= result.returncode != 0
            print(f"{'PASS' if result.returncode == 0 else 'FAIL'}: Clock {scenario}", flush=True)
        if failed:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
