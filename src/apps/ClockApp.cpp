#include "ClockApp.h"

#include <Arduino.h>
#include <Display.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdFat.h>
#include <esp_sntp.h>
#include <time.h>

#include <cstdlib>
#include <ctime>
#include <iterator>

#include "../core/Core.h"
#include "../network/WifiCredentialStore.h"
#include "../ui/Elements.h"
#include "../ui/views/SettingsViews.h"
#include "ClockFaces.h"
#include "MiniApp.h"
#include "ThemeManager.h"

#define TAG "CLOCK_APP"

extern GfxRenderer renderer;

namespace papyrix {
namespace clock_app {

static constexpr const char* SETTINGS_PATH = "/.papyrix/apps/clock.txt";

static constexpr uint32_t NTP_SYNC_INTERVALS[] = {10800000, 21600000, 86400000, 0};
static constexpr int NTP_SYNC_COUNT = static_cast<int>(std::size(NTP_SYNC_INTERVALS));

static constexpr const char* DATE_FORMAT_LABELS[] = {"YYYY/MM/DD", "DD/MM/YYYY", "MM/DD/YYYY", "DD.MM.YYYY"};
static constexpr int DATE_FORMAT_COUNT = static_cast<int>(std::size(DATE_FORMAT_LABELS));

enum class MenuItem : int8_t { Face, UtcOffset, TimeFormat, DateFormat, NtpSync, SyncNow, Count };
static constexpr int MENU_ITEM_COUNT = static_cast<int>(MenuItem::Count);

static constexpr int NTP_SERVER_MAX = 3;
static constexpr const char* DEFAULT_NTP_SERVERS[] = {"pool.ntp.org", "time.nist.gov", "time.google.com"};

static struct {
  clock_faces::Face face = clock_faces::Face::Big;
  int8_t utcOffset = 0;
  bool use24h = true;
  int8_t lastRenderedMin = -1;
  uint32_t lastNtpSyncMs = 0;
  int8_t ntpSyncSetting = 0;  // default: 3h
  int8_t dateFormat = 0;      // default: YYYY/MM/DD
  int8_t menuSelected = 0;
  bool settingsChanged = false;
  char ntpServers[128] = "pool.ntp.org,time.nist.gov";
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
    char* end = tok + strlen(tok) - 1;
    while (end > tok && *end == ' ') *end-- = '\0';
    if (*tok) out[count++] = tok;
    tok = strtok(nullptr, ",");
  }
  return count;
}

static void loadSettings(Core& core) {
  state.face = clock_faces::Face::Big;
  char buf[320];
  auto result = core.storage.readToBuffer(SETTINGS_PATH, buf, sizeof(buf));
  if (!result.ok()) return;
  state.face = clock_faces::Face::Retro;

  size_t len = result.value;
  buf[len < sizeof(buf) ? len : sizeof(buf) - 1] = '\0';

  // Parse key=value pairs
  char* line = buf;
  while (line && *line) {
    char* nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    if (strncmp(line, "utcOffset=", 10) == 0) {
      state.utcOffset = static_cast<int8_t>(atoi(line + 10));
      if (state.utcOffset < -12) state.utcOffset = -12;
      if (state.utcOffset > 14) state.utcOffset = 14;
    } else if (strncmp(line, "use24h=", 7) == 0) {
      state.use24h = (line[7] == '1');
    } else if (strncmp(line, "ntpSync=", 8) == 0) {
      int val = atoi(line + 8);
      if (val >= 0 && val < NTP_SYNC_COUNT) {
        state.ntpSyncSetting = static_cast<int8_t>(val);
      }
    } else if (strncmp(line, "dateFmt=", 8) == 0) {
      int val = atoi(line + 8);
      if (val >= 0 && val < DATE_FORMAT_COUNT) {
        state.dateFormat = static_cast<int8_t>(val);
      }
    } else if (strncmp(line, "ntpServers=", 11) == 0) {
      size_t vlen = strlen(line + 11);
      if (vlen >= sizeof(state.ntpServers)) vlen = sizeof(state.ntpServers) - 1;
      memcpy(state.ntpServers, line + 11, vlen);
      state.ntpServers[vlen] = '\0';
    } else if (strncmp(line, "face=", 5) == 0) {
      const char* text = line + 5;
      char* end = nullptr;
      const long value = std::strtol(text, &end, 10);
      if (end != text && *end == '\0' && value >= 0 && value < static_cast<long>(clock_faces::Face::Count)) {
        const auto face = static_cast<clock_faces::Face>(value);
        if (clock_faces::isSelectable(face)) state.face = face;
      }
    }

    line = nl ? nl + 1 : nullptr;
  }
}

static void saveSettings(Core& core) {
  if (!state.settingsChanged) return;
  applyTimezone(state.utcOffset);
  core.storage.mkdir("/.papyrix/apps");

  FsFile file;
  auto result = core.storage.openWrite(SETTINGS_PATH, file);
  if (!result.ok()) return;

  char buf[320];
  snprintf(buf, sizeof(buf), "utcOffset=%d\nuse24h=%d\nntpSync=%d\ndateFmt=%d\nntpServers=%s\nface=%u\n",
           state.utcOffset, state.use24h ? 1 : 0, state.ntpSyncSetting, state.dateFormat, state.ntpServers,
           static_cast<unsigned>(state.face));
  file.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  file.close();
  state.settingsChanged = false;
}

static void syncNtpWithConnection(Core& core) {
  const char* servers[NTP_SERVER_MAX];
  int count = parseNtpServers(state.ntpServers, servers);
  if (count == 0) {
    servers[0] = DEFAULT_NTP_SERVERS[0];
    servers[1] = DEFAULT_NTP_SERVERS[1];
    count = 2;
  }
  LOG_INF(TAG, "NTP servers: %s", state.ntpServers);
  configTime(0, 0, servers[0], count > 1 ? servers[1] : nullptr, count > 2 ? servers[2] : nullptr);
  applyTimezone(state.utcOffset);

  bool synced = false;
  for (int i = 0; i < 20; i++) {
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      synced = true;
      break;
    }
    delay(500);
  }
  esp_sntp_stop();
  struct tm timeinfo;
  if (synced && core.clock.updateFromSystem()) {
    if (!core.clock.localTime(timeinfo)) {
      LOG_ERR(TAG, "NTP system time unavailable");
      return;
    }
    LOG_INF(TAG, "NTP sync OK: %04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
            timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    LOG_ERR(TAG, "NTP sync timeout");
  }
}

static void syncNtpAutoConnect(Core& core, bool manualSync) {
  hal::WifiSession wifiSession(core.wifi, core.cpu);
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCount() == 0) {
    if (manualSync) {
      core.pendingSync = SyncMode::NtpSync;
      core.pendingAppId = APP_CLOCK;
    }
    return;
  }

  ui::centeredMessage(renderer, THEME, THEME.uiFontId, "Syncing time...");
  renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH, true);

  const auto& creds = WIFI_STORE.getCredentials();
  int credCount = WIFI_STORE.getCount();
  bool connected = false;
  for (int i = 0; i < credCount; i++) {
    LOG_INF(TAG, "Trying WiFi: %s", creds[i].ssid);
    auto connResult = core.wifi.connect(creds[i].ssid, creds[i].password);
    if (connResult.ok()) {
      if (!WIFI_STORE.promoteCredential(creds[i].ssid)) LOG_ERR(TAG, "Could not save WiFi priority");
      connected = true;
      break;
    }
    core.wifi.shutdown();
  }

  if (!connected) {
    LOG_ERR(TAG, "All WiFi credentials failed");
    ui::centeredMessage(renderer, THEME, THEME.uiFontId, "WiFi connection failed");
    renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH, true);
    delay(2000);
    return;
  }

  syncNtpWithConnection(core);
}

void enter(Core& core) {
  LOG_INF(TAG, "Clock app enter");
  state.lastRenderedMin = -1;
  state.menuSelected = 0;

  loadSettings(core);
  applyTimezone(state.utcOffset);

  std::tm timeinfo{};
  if (core.pendingSync != SyncMode::NtpSync && core.clock.localTime(timeinfo)) {
    hal::WifiSession wifiSession(core.wifi, core.cpu);
    state.lastNtpSyncMs = millis();
    return;
  }

  if (core.wifi.isConnected()) {
    hal::WifiSession wifiSession(core.wifi, core.cpu);
    ui::centeredMessage(renderer, THEME, THEME.uiFontId, "Syncing time...");
    renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH, true);
    syncNtpWithConnection(core);
  } else {
    syncNtpAutoConnect(core, false);
  }
  state.lastNtpSyncMs = millis();
}

bool update(Core& core) {
  const uint32_t now = millis();
  const uint32_t ntpInterval = NTP_SYNC_INTERVALS[state.ntpSyncSetting];
  if (ntpInterval > 0 && now - state.lastNtpSyncMs >= ntpInterval) {
    core.cpu.unthrottle();
    syncNtpAutoConnect(core, false);
    state.lastNtpSyncMs = millis();
    return true;
  }

  std::tm timeinfo{};
  const int minute = core.clock.localTime(timeinfo) ? timeinfo.tm_min : -1;
  if (minute != state.lastRenderedMin) {
    core.cpu.unthrottle();
    return true;
  }
  core.cpu.throttle();
  return false;
}

bool render(Core& core) {
  const Theme& theme = THEME;
  const auto battery = core.battery.readStatus();
  const int batteryPercent = battery.percentageKnown ? static_cast<int>(battery.percentage) : -1;
  renderer.clearScreen(theme.backgroundColor);
  state.lastRenderedMin = -1;

  struct tm timeinfo;
  if (core.clock.localTime(timeinfo)) {
    state.lastRenderedMin = timeinfo.tm_min;
    clock_faces::render({renderer, theme, timeinfo, state.use24h, state.dateFormat, state.utcOffset}, state.face);
  } else {
    ui::centeredMessage(renderer, theme, theme.uiFontId, "Time not synced");
  }

  constexpr int batteryBodyWidth = 30;
  constexpr int batteryTipWidth = 3;
  constexpr int batteryRightMargin = 20;
  ui::batteryIcon(renderer, theme, renderer.getScreenWidth() - batteryRightMargin - batteryBodyWidth - batteryTipWidth,
                  22, batteryPercent, core.usb.isConnected());
  ui::ButtonBar buttons("Back", "Menu", "", "");
  ui::buttonBar(renderer, theme, buttons);
  return false;
}

void exit(Core& core) {
  core.cpu.unthrottle();
  saveSettings(core);
  renderer.clearScreen(THEME.backgroundColor);
  renderer.displayBuffer(papyrix::hal::Display::HALF_REFRESH);
  LOG_INF(TAG, "Clock app exit");
}

void renderMenu(Core&) {
  LOG_DBG(TAG, "Menu render: selected=%d", state.menuSelected);
  const Theme& theme = THEME;
  ui::title(renderer, theme, theme.screenMarginTop, "Clock Settings");

  char timezone[8];
  snprintf(timezone, sizeof(timezone), "UTC%+d", state.utcOffset);
  static constexpr const char* syncValues[] = {"Every 3 hours", "Every 6 hours", "Every 24 hours", "Off"};
  static_assert(std::size(syncValues) == NTP_SYNC_COUNT);
  static constexpr const char* labels[] = {"Face", "Time zone", "Time format", "Date format", "Auto sync"};
  const char* values[] = {clock_faces::name(state.face), timezone, state.use24h ? "24-hour" : "12-hour",
                          DATE_FORMAT_LABELS[state.dateFormat], syncValues[state.ntpSyncSetting]};
  static_assert(std::size(labels) == std::size(values));
  static_assert(std::size(labels) == static_cast<int>(MenuItem::SyncNow));

  int y = ui::SettingsListHit::LIST_START_Y;
  for (int i = 0; i < static_cast<int>(std::size(labels)); ++i) {
    ui::enumValue(renderer, theme, y, labels[i], values[i], state.menuSelected == i, theme.itemHeight);
    y += theme.itemHeight + theme.itemSpacing;
  }
  const bool syncSelected = static_cast<MenuItem>(state.menuSelected) == MenuItem::SyncNow;
  ui::menuItem(renderer, theme, y, "Sync Now", syncSelected);
  ui::ButtonBar buttons("Back", "", syncSelected ? "" : "<", syncSelected ? "Sync" : ">");
  ui::buttonBar(renderer, theme, buttons);
}

void onMenuButton(Core& core, Button btn) {
  LOG_DBG(TAG, "Menu input: button=%u selected=%d cpu=%u MHz", static_cast<unsigned>(btn), state.menuSelected,
          static_cast<unsigned>(getCpuFrequencyMhz()));
  switch (btn) {
    case Button::Back:
      saveSettings(core);
      break;
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
          break;
        case MenuItem::SyncNow:
          if (btn == Button::Right) {
            syncNtpAutoConnect(core, true);
            state.lastNtpSyncMs = millis();
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

}  // namespace clock_app
}  // namespace papyrix
