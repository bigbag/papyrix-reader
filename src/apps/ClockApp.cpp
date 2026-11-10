#include "ClockApp.h"

#include <Arduino.h>
#include <Display.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdFat.h>
#include <esp_sntp.h>
#include <time.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iterator>

#include "../core/Core.h"
#include "../network/WifiCredentialStore.h"
#include "../ui/Elements.h"
#include "../ui/views/SettingsViews.h"
#include "ClockFaces.h"
#include "ClockSettings.h"
#include "ClockStandby.h"
#include "MiniApp.h"
#include "MiniAppInput.h"
#include "ThemeManager.h"

#define TAG "CLOCK_APP"

extern GfxRenderer renderer;

namespace papyrix {
namespace clock_app {

static constexpr int NTP_SYNC_COUNT = static_cast<int>(std::size(NTP_SYNC_SECONDS));

static constexpr const char* DATE_FORMAT_LABELS[] = {"YYYY/MM/DD", "DD/MM/YYYY", "MM/DD/YYYY", "DD.MM.YYYY"};
static constexpr int DATE_FORMAT_COUNT = static_cast<int>(std::size(DATE_FORMAT_LABELS));

enum class MenuItem : int8_t { Face, UtcOffset, TimeFormat, DateFormat, NtpSync, PowerMode, SyncNow, Count };
static constexpr int MENU_ITEM_COUNT = static_cast<int>(MenuItem::Count);

static constexpr int NTP_SERVER_MAX = 3;
static constexpr const char* DEFAULT_NTP_SERVERS[] = {"pool.ntp.org", "time.nist.gov", "time.google.com"};

static struct : ClockSettings {
  int8_t lastRenderedMin = -1;
  int64_t nextNtpUtc = 0;
  uint32_t interactionStartedMs = 0;
  int8_t menuSelected = 0;
  bool settingsChanged = false;
  bool saveFailed = false;
} state;

static void applyTimezone(int offsetHours) {
  char tz[16];
  if (offsetHours == 0) {
    snprintf(tz, sizeof(tz), "UTC0");
  } else {
    snprintf(tz, sizeof(tz), "UTC%d", -offsetHours);
  }
  setenv("TZ", tz, 1);
  tzset();
}

static int parseNtpServers(const char* servers, const char* out[NTP_SERVER_MAX]) {
  int count = 0;
  static char buf[128];
  strncpy(buf, servers, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* tok = strtok(buf, ",");
  while (tok && count < NTP_SERVER_MAX) {
    while (*tok == ' ') tok++;
    char* end = tok + strlen(tok);
    while (end > tok && end[-1] == ' ') *--end = '\0';
    if (*tok) out[count++] = tok;
    tok = strtok(nullptr, ",");
  }
  return count;
}

static void loadSettings(Core& core) {
  if (loadSettings(core.storage, static_cast<ClockSettings&>(state))) {
    state.settingsChanged = false;
    state.saveFailed = false;
  } else {
    LOG_ERR(TAG, "Clock settings read failed");
  }
}

static bool saveSettings(Core& core) {
  if (!state.settingsChanged) return true;
  state.saveFailed = !saveSettings(core.storage, state);
  if (state.saveFailed) {
    LOG_ERR(TAG, "Clock settings save failed");
    return false;
  }
  applyTimezone(state.utcOffset);
  state.settingsChanged = false;
  return true;
}

static void showSyncMessage(const char* message) {
  renderer.clearScreen(THEME.backgroundColor);
  const int y = renderer.getScreenHeight() / 2 - renderer.getLineHeight(THEME.uiFontId) / 2;
  renderer.drawCenteredText(THEME.uiFontId, y, message, THEME.primaryTextBlack, EpdFontFamily::BOLD);
  renderer.displayBuffer(hal::Display::FAST_REFRESH, true);
}

SyncResult synchronize(Core& core, const ClockSettings& settings, bool interactive) {
  hal::WifiSession wifiSession(core.wifi, core.cpu);
  const auto cancelled = [interactive]() { return !interactive && powerWakeRequested(); };
  if (cancelled()) return SyncResult::Cancelled;
  if (!core.wifi.isConnected()) {
    if (!WIFI_STORE.loadFromFile() || WIFI_STORE.getCount() == 0) {
      if (interactive) {
        core.pendingSync = SyncMode::NtpSync;
        core.pendingAppId = APP_CLOCK;
      }
      return SyncResult::Unavailable;
    }
    if (interactive) showSyncMessage("Syncing time...");
    const uint32_t start = millis();
    const auto& credentials = WIFI_STORE.getCredentials();
    for (int i = 0; i < WIFI_STORE.getCount(); ++i) {
      if (cancelled()) return SyncResult::Cancelled;
      const uint32_t elapsed = millis() - start;
      if (!interactive && elapsed >= 15000) break;
      const uint32_t budget = interactive ? 15000 : 15000 - elapsed;
      if (core.wifi
              .connect(credentials[i].ssid, credentials[i].password, budget, interactive ? nullptr : powerWakeRequested)
              .ok()) {
        if (interactive && !WIFI_STORE.promoteCredential(credentials[i].ssid)) {
          LOG_ERR(TAG, "Could not save WiFi priority");
        }
        break;
      }
      core.wifi.shutdown();
    }
    if (cancelled()) return SyncResult::Cancelled;
    if (!core.wifi.isConnected()) {
      if (interactive) showSyncMessage("WiFi connection failed");
      return SyncResult::Failed;
    }
  } else if (interactive) {
    showSyncMessage("Syncing time...");
  }

  const char* servers[NTP_SERVER_MAX];
  int count = parseNtpServers(settings.ntpServers, servers);
  if (count == 0) {
    servers[0] = DEFAULT_NTP_SERVERS[0];
    servers[1] = DEFAULT_NTP_SERVERS[1];
    count = 2;
  }
  configTime(0, 0, servers[0], count > 1 ? servers[1] : nullptr, count > 2 ? servers[2] : nullptr);
  applyTimezone(settings.utcOffset);
  const uint32_t start = millis();
  bool synced = false;
  while (millis() - start < 10000 && !cancelled()) {
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      synced = true;
      break;
    }
    delay(std::min<uint32_t>(20, 10000 - std::min<uint32_t>(10000, millis() - start)));
  }
  esp_sntp_stop();
  if (cancelled()) return SyncResult::Cancelled;
  if (!synced) return SyncResult::Failed;
  return core.clock.updateFromSystem() ? SyncResult::Success : SyncResult::TimeError;
}

bool prepareStandby(Core& core, ResumeRecord& record) {
  if (!saveSettings(core)) return false;
  record.settings = state;
  record.nextNtpUtc = state.nextNtpUtc;
  record.lastFrame = captureFrame(core, std::time(nullptr), false);
  return true;
}

bool standbyDue(Core& core, uint32_t nowMs, bool operationPending) {
  return state.powerMode == PowerMode::PowerSaving && !state.saveFailed && core.input.userIdleTimeMs() >= 15000 &&
         interactionExpired(nowMs, state.interactionStartedMs, operationPending);
}

void resumeInteractive(Core& core, const ResumeRecord* record) {
  if (record) {
    static_cast<ClockSettings&>(state) = record->settings;
    state.nextNtpUtc = record->nextNtpUtc;
    state.settingsChanged = false;
    renderer.setOrientation(static_cast<GfxRenderer::Orientation>(record->lastFrame.appearance.orientation));
  }
  state.lastRenderedMin = -1;
  state.menuSelected = 0;
  state.interactionStartedMs = millis();
  applyTimezone(state.utcOffset);
  core.cpu.unthrottle();
  core.input.suppressPowerUntilRelease();
}

void enter(Core& core) {
  LOG_INF(TAG, "Clock app enter");
  state.lastRenderedMin = -1;
  state.menuSelected = 0;

  loadSettings(core);
  applyTimezone(state.utcOffset);

  std::tm timeinfo{};
  if (core.pendingSync == SyncMode::NtpSync || !core.clock.localTime(timeinfo)) {
    synchronize(core, state, true);
  } else {
    core.wifi.shutdown();
  }
  state.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[state.ntpSyncSetting]);
  state.interactionStartedMs = millis();
}

bool update(Core& core) {
  const int64_t now = std::time(nullptr);
  if (NTP_SYNC_SECONDS[state.ntpSyncSetting] && state.nextNtpUtc && now >= state.nextNtpUtc) {
    core.cpu.unthrottle();
    synchronize(core, state, false);
    state.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[state.ntpSyncSetting]);
    return true;
  }

  std::tm timeinfo{};
  const int minute =
      core.clock.localTime(timeinfo) && (state.powerMode == PowerMode::Normal || core.clock.canRetainTime())
          ? timeinfo.tm_min
          : -1;
  if (minute != state.lastRenderedMin) {
    core.cpu.unthrottle();
    return true;
  }
  core.cpu.throttle();
  return false;
}

ClockFrame captureFrame(Core& core, int64_t utcSeconds, bool interactive) {
  const Theme& theme = THEME;
  const auto battery = core.battery.readStatus();
  ClockFrame frame{};
  frame.utcSeconds = utcSeconds;
  frame.face = state.face;
  frame.utcOffset = state.utcOffset;
  frame.dateFormat = state.dateFormat;
  frame.use24h = state.use24h;
  frame.appearance = {theme.invertedMode,
                      theme.selectionFillBlack,
                      theme.selectionTextBlack,
                      theme.primaryTextBlack,
                      theme.secondaryTextBlack,
                      theme.backgroundColor,
                      theme.screenMarginTop,
                      theme.screenMarginSide,
                      theme.itemHeight,
                      theme.itemSpacing,
                      theme.itemPaddingX,
                      theme.itemValuePadding,
                      static_cast<uint8_t>(renderer.getOrientation()),
                      core.settings.frontButtonLayout};
  frame.batteryPercent = battery.percentageKnown ? static_cast<int16_t>(battery.percentage) : -1;
  frame.charging = battery.chargingKnown && battery.charging;
  frame.usbConnected = core.usb.isConnected();
  frame.interactive = interactive;
  return frame;
}

static void renderMenu(Core& core);

miniapps::Frame render(Core& core, bool menuOpen) {
  if (menuOpen) {
    renderMenu(core);
    return {miniapps::Refresh::Fast, false};
  }
  std::tm timeinfo{};
  const bool valid =
      core.clock.localTime(timeinfo) && (state.powerMode == PowerMode::Normal || core.clock.canRetainTime());
  state.lastRenderedMin = valid ? timeinfo.tm_min : -1;
  renderFrame(renderer, captureFrame(core, valid ? std::time(nullptr) : 0, true));
  return {miniapps::Refresh::Fast, true};
}

void exit(Core& core) {
  clearResume();
  core.cpu.unthrottle();
  saveSettings(core);
  renderer.clearScreen(THEME.backgroundColor);
  renderer.displayBuffer(papyrix::hal::Display::HALF_REFRESH);
  LOG_INF(TAG, "Clock app exit");
}

static void renderMenu(Core& core) {
  const Theme& theme = THEME;
  renderer.clearScreen(theme.backgroundColor);
  ui::title(renderer, theme, theme.screenMarginTop, "Clock Settings");

  char timezone[8];
  snprintf(timezone, sizeof(timezone), "UTC%+d", state.utcOffset);
  static constexpr const char* syncValues[] = {"Every 3 hours", "Every 6 hours", "Every 24 hours", "Off"};
  static_assert(std::size(syncValues) == NTP_SYNC_COUNT);
  static constexpr const char* labels[] = {"Face", "Time zone", "Time format", "Date format", "Auto sync", "Mode"};
  const char* values[] = {
      clock_faces::name(state.face),        timezone,
      state.use24h ? "24-hour" : "12-hour", DATE_FORMAT_LABELS[state.dateFormat],
      syncValues[state.ntpSyncSetting],     state.powerMode == PowerMode::Normal ? "Normal" : "Power saving"};
  static_assert(std::size(labels) == std::size(values));
  static_assert(std::size(labels) == static_cast<int>(MenuItem::SyncNow));

  int y = ui::SettingsListHit::LIST_START_Y;
  for (int i = 0; i < static_cast<int>(std::size(labels)); ++i) {
    ui::enumValue(renderer, theme, y, labels[i], values[i], state.menuSelected == i, theme.itemHeight);
    y += theme.itemHeight + theme.itemSpacing;
  }
  const bool syncSelected = static_cast<MenuItem>(state.menuSelected) == MenuItem::SyncNow;
  ui::menuItem(renderer, theme, y, "Sync Now", syncSelected);
  if (state.saveFailed) {
    renderer.drawCenteredText(theme.smallFontId, y + theme.itemHeight + theme.itemSpacing,
                              "Save failed. Check SD card.", theme.primaryTextBlack);
  }
  ui::ButtonBar buttons("Back", "", syncSelected ? "" : "<", syncSelected ? "Sync" : ">");
  ui::buttonBar(renderer, theme, buttons);
}

static bool onMenuTap(Core& core, int16_t x, int16_t y) {
  const auto hit = ui::SettingsListLayout{MENU_ITEM_COUNT}.hitTest(
      {x, y}, renderer.getScreenWidth(), renderer.getScreenHeight(), THEME.itemHeight + THEME.itemSpacing,
      core.settings.frontButtonLayout == Settings::FrontLRBC);
  if (hit.type != ui::SettingsListHit::Type::Row) return false;
  state.menuSelected = static_cast<int8_t>(hit.index);
  state.interactionStartedMs = millis();
  return true;
}

static void onMenuButton(Core& core, Button btn) {
  state.interactionStartedMs = millis();
  switch (btn) {
    case Button::Up:
      state.menuSelected = (state.menuSelected == 0) ? MENU_ITEM_COUNT - 1 : state.menuSelected - 1;
      break;
    case Button::Down:
      state.menuSelected = (state.menuSelected + 1) % MENU_ITEM_COUNT;
      break;
    case Button::Left:
    case Button::Right: {
      int delta = (btn == Button::Left) ? -1 : 1;
      switch (static_cast<MenuItem>(state.menuSelected)) {
        case MenuItem::Face:
          state.face = clock_faces::selectRelative(state.face, delta);
          break;
        case MenuItem::UtcOffset:
          state.utcOffset = static_cast<int8_t>(state.utcOffset + delta);
          if (state.utcOffset > 14) state.utcOffset = -12;
          if (state.utcOffset < -12) state.utcOffset = 14;
          break;
        case MenuItem::TimeFormat:
          state.use24h = !state.use24h;
          break;
        case MenuItem::DateFormat:
          state.dateFormat = static_cast<int8_t>((state.dateFormat + delta + DATE_FORMAT_COUNT) % DATE_FORMAT_COUNT);
          break;
        case MenuItem::NtpSync:
          state.ntpSyncSetting = static_cast<int8_t>((state.ntpSyncSetting + delta + NTP_SYNC_COUNT) % NTP_SYNC_COUNT);
          state.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[state.ntpSyncSetting]);
          break;
        case MenuItem::PowerMode:
          state.powerMode = state.powerMode == PowerMode::Normal ? PowerMode::PowerSaving : PowerMode::Normal;
          break;
        case MenuItem::SyncNow:
          if (btn == Button::Right) {
            synchronize(core, state, true);
            state.nextNtpUtc = nextSyncUtc(std::time(nullptr), NTP_SYNC_SECONDS[state.ntpSyncSetting]);
            state.interactionStartedMs = millis();
          }
          return;
        case MenuItem::Count:
          return;
      }
      state.settingsChanged = true;
      break;
    }
    default:
      break;
  }
}

miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen) {
  if (menuOpen && event.type == EventType::Tap && onMenuTap(core, event.touch.x, event.touch.y))
    return miniapps::Action::Redraw;
  if (menuOpen) {
    Button button;
    if (miniAppButton(core, event, button) && button == Button::Center) return miniapps::Action::None;
  }
  return handleAppButtons(core, event, menuOpen, nullptr, onMenuButton);
}

miniapps::ExitResult prepareExit(Core& core, miniapps::ExitTarget target) {
  if (!saveSettings(core)) return miniapps::ExitResult::OpenMenu;
  if (target == miniapps::ExitTarget::Menu) state.interactionStartedMs = millis();
  return miniapps::ExitResult::Ready;
}

}  // namespace clock_app
}  // namespace papyrix
