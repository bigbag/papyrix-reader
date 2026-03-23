#include "ClockApp.h"

#include <Arduino.h>
#include <EInkDisplay.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdFat.h>
#include <esp_sntp.h>

#include <ctime>

#include "../Battery.h"
#include "../core/Core.h"
#include "../network/WifiCredentialStore.h"
#include "../ui/Elements.h"
#include "MiniApp.h"
#include "ThemeManager.h"

#define TAG "CLOCK_APP"

extern GfxRenderer renderer;

namespace papyrix {
namespace clock_app {

static constexpr const char* SETTINGS_PATH = "/.papyrix/apps/clock.txt";

static constexpr uint32_t NTP_SYNC_INTERVALS[] = {10800000, 21600000, 86400000, 0};
static constexpr const char* NTP_SYNC_LABELS[] = {"3h", "6h", "24h", "Off"};
static constexpr int NTP_SYNC_COUNT = 4;

static constexpr const char* DATE_FORMAT_LABELS[] = {"YYYY/MM/DD", "DD/MM/YYYY", "MM/DD/YYYY", "DD.MM.YYYY"};
static constexpr int DATE_FORMAT_COUNT = 4;

static constexpr int NTP_SERVER_MAX = 3;
static constexpr const char* DEFAULT_NTP_SERVERS[] = {"pool.ntp.org", "time.nist.gov", "time.google.com"};

static struct {
  int8_t utcOffset = 0;
  bool use24h = true;
  int8_t lastRenderedMin = -1;
  unsigned long lastNtpSyncMs = 0;
  int8_t ntpSyncSetting = 0;  // default: 3h
  int8_t dateFormat = 0;      // default: YYYY/MM/DD
  int8_t menuSelected = 0;
  char ntpServers[128] = "pool.ntp.org,time.nist.gov";
} state;

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
  char buf[200];
  auto result = core.storage.readToBuffer(SETTINGS_PATH, buf, sizeof(buf));
  if (!result.ok()) return;

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
    }

    line = nl ? nl + 1 : nullptr;
  }
}

static void saveSettings(Core& core) {
  core.storage.mkdir("/.papyrix/apps");

  FsFile file;
  auto result = core.storage.openWrite(SETTINGS_PATH, file);
  if (!result.ok()) return;

  char buf[200];
  snprintf(buf, sizeof(buf), "utcOffset=%d\nuse24h=%d\nntpSync=%d\ndateFmt=%d\nntpServers=%s\n", state.utcOffset,
           state.use24h ? 1 : 0, state.ntpSyncSetting, state.dateFormat, state.ntpServers);
  file.write(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
  file.close();
}

static void syncNtpWithConnection() {
  const char* servers[NTP_SERVER_MAX];
  int count = parseNtpServers(state.ntpServers, servers);
  if (count == 0) {
    servers[0] = DEFAULT_NTP_SERVERS[0];
    servers[1] = DEFAULT_NTP_SERVERS[1];
    count = 2;
  }
  LOG_INF(TAG, "NTP servers: %s", state.ntpServers);
  configTime(state.utcOffset * 3600, 0, servers[0], count > 1 ? servers[1] : nullptr, count > 2 ? servers[2] : nullptr);

  bool synced = false;
  for (int i = 0; i < 20; i++) {
    if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      synced = true;
      break;
    }
    delay(500);
  }
  struct tm timeinfo;
  if (synced && getLocalTime(&timeinfo, 0)) {
    LOG_INF(TAG, "NTP sync OK: %04d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1,
            timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    LOG_ERR(TAG, "NTP sync timeout");
  }
}

static void syncNtpAutoConnect(Core& core) {
  ui::centeredMessage(renderer, THEME, THEME.uiFontId, "Syncing time...");
  renderer.displayBuffer();

  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCount() == 0) {
    LOG_ERR(TAG, "No saved WiFi credentials");
    ui::centeredMessage(renderer, THEME, THEME.uiFontId, "No saved WiFi");
    renderer.displayBuffer();
    delay(2000);
    return;
  }

  const auto& creds = WIFI_STORE.getCredentials();
  int credCount = WIFI_STORE.getCount();
  bool connected = false;
  for (int i = 0; i < credCount; i++) {
    LOG_INF(TAG, "Trying WiFi: %s", creds[i].ssid);
    auto connResult = core.network.connect(creds[i].ssid, creds[i].password);
    if (connResult.ok()) {
      connected = true;
      break;
    }
    core.network.shutdown();
  }

  if (!connected) {
    LOG_ERR(TAG, "All WiFi credentials failed");
    ui::centeredMessage(renderer, THEME, THEME.uiFontId, "WiFi connection failed");
    renderer.displayBuffer();
    delay(2000);
    core.network.shutdown();
    return;
  }

  syncNtpWithConnection();
  core.network.shutdown();
}

void enter(Core& core) {
  LOG_INF(TAG, "Clock app enter");
  state.lastRenderedMin = -1;
  state.menuSelected = 0;

  loadSettings(core);

  ui::centeredMessage(renderer, THEME, THEME.uiFontId, "Syncing time...");
  renderer.displayBuffer();

  if (core.network.isConnected()) {
    syncNtpWithConnection();
    core.network.shutdown();
  } else {
    syncNtpAutoConnect(core);
  }
  state.lastNtpSyncMs = millis();
}

bool update(Core& core) {
  const unsigned long now = millis();

  // Periodic NTP sync
  uint32_t ntpInterval = NTP_SYNC_INTERVALS[state.ntpSyncSetting];
  if (ntpInterval > 0 && now - state.lastNtpSyncMs >= ntpInterval) {
    core.cpu.unthrottle();
    syncNtpAutoConnect(core);
    state.lastNtpSyncMs = now;
    return true;
  }

  // Refresh when the minute changes
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    if (timeinfo.tm_min != state.lastRenderedMin) {
      core.cpu.unthrottle();
      return true;
    }
  } else if (now - state.lastNtpSyncMs >= 10000) {
    // No time yet — refresh periodically so display updates after NTP sync
    core.cpu.unthrottle();
    state.lastNtpSyncMs = now;
    return true;
  }

  core.cpu.throttle();
  return false;
}

void onButton(Core& core, Button btn) {
  (void)core;
  (void)btn;
}

// 7-segment display constants
static constexpr int DIGIT_W = 80;
static constexpr int DIGIT_H = 160;
static constexpr int SEG_T = 14;
static constexpr int SEG_GAP = 4;
static constexpr int DIGIT_SPACING = 12;
static constexpr int COLON_W = 30;

// Bits: 6=A(top), 5=B(top-right), 4=C(bot-right), 3=D(bottom), 2=E(bot-left), 1=F(top-left), 0=G(middle)
static constexpr uint8_t SEGMENT_MAP[10] = {0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B};

static void draw7SegDigit(int x, int y, int digit, bool color) {
  const uint8_t segs = SEGMENT_MAP[digit];
  const int w = DIGIT_W;
  const int h = DIGIT_H;
  const int t = SEG_T;
  const int g = SEG_GAP;
  const int halfH = h / 2;

  if (segs & 0x40) renderer.fillRect(x + g, y, w - 2 * g, t, color);                      // A: top
  if (segs & 0x20) renderer.fillRect(x + w - t, y + g, t, halfH - 2 * g, color);          // B: top-right
  if (segs & 0x10) renderer.fillRect(x + w - t, y + halfH + g, t, halfH - 2 * g, color);  // C: bot-right
  if (segs & 0x08) renderer.fillRect(x + g, y + h - t, w - 2 * g, t, color);              // D: bottom
  if (segs & 0x04) renderer.fillRect(x, y + halfH + g, t, halfH - 2 * g, color);          // E: bot-left
  if (segs & 0x02) renderer.fillRect(x, y + g, t, halfH - 2 * g, color);                  // F: top-left
  if (segs & 0x01) renderer.fillRect(x + g, y + halfH - t / 2, w - 2 * g, t, color);      // G: middle
}

static void drawColon(int x, int y, bool color) {
  const int dotY1 = y + DIGIT_H / 3 - SEG_T / 2;
  const int dotY2 = y + 2 * DIGIT_H / 3 - SEG_T / 2;
  const int dotX = x + (COLON_W - SEG_T) / 2;
  renderer.fillRect(dotX, dotY1, SEG_T, SEG_T, color);
  renderer.fillRect(dotX, dotY2, SEG_T, SEG_T, color);
}

static void drawDayOfWeek(const Theme& theme, int x, int y, int wday) {
  static const char* const DAYS[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  renderer.drawText(theme.uiFontId, x, y, DAYS[wday], theme.secondaryTextBlack);
}

void render(Core& core) {
  (void)core;

  const Theme& theme = THEME;
  renderer.clearScreen(theme.backgroundColor);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    state.lastRenderedMin = timeinfo.tm_min;

    // Date string top-left
    char dateStr[16];
    const int yr = timeinfo.tm_year + 1900;
    const int mo = timeinfo.tm_mon + 1;
    const int dy = timeinfo.tm_mday;
    switch (state.dateFormat) {
      case 1:
        snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", dy, mo, yr);
        break;
      case 2:
        snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", mo, dy, yr);
        break;
      case 3:
        snprintf(dateStr, sizeof(dateStr), "%02d.%02d.%04d", dy, mo, yr);
        break;
      default:
        snprintf(dateStr, sizeof(dateStr), "%04d/%02d/%02d", yr, mo, dy);
        break;
    }
    renderer.drawText(theme.uiFontId, 20, 26, dateStr, theme.primaryTextBlack);
    drawDayOfWeek(theme, 20, 50, timeinfo.tm_wday);

    // Battery top-right
    ui::battery(renderer, theme, 380, 26, batteryMonitor.readPercentage());

    // Determine digits
    int hour = timeinfo.tm_hour;
    bool showLeadingZero = true;
    if (!state.use24h) {
      hour = hour % 12;
      if (hour == 0) hour = 12;
      showLeadingZero = false;
    }
    const int d0 = hour / 10;
    const int d1 = hour % 10;
    const int d2 = timeinfo.tm_min / 10;
    const int d3 = timeinfo.tm_min % 10;

    // Calculate actual drawn width based on whether leading digit is shown
    const bool skipLeading = !showLeadingZero && d0 == 0;
    const int totalW =
        skipLeading ? (DIGIT_W * 3 + DIGIT_SPACING + COLON_W) : (DIGIT_W * 4 + DIGIT_SPACING * 2 + COLON_W);

    // Center horizontally, vertically between DOW bar and button bar
    const int clockX = (renderer.getScreenWidth() - totalW) / 2;
    const int areaTop = 100;
    const int areaBot = 760;
    const int clockY = areaTop + (areaBot - areaTop - DIGIT_H) / 2;

    int cx = clockX;

    // Hour digits
    if (!skipLeading) {
      draw7SegDigit(cx, clockY, d0, theme.primaryTextBlack);
      cx += DIGIT_W + DIGIT_SPACING;
    }
    draw7SegDigit(cx, clockY, d1, theme.primaryTextBlack);
    cx += DIGIT_W;

    // Colon
    drawColon(cx, clockY, theme.primaryTextBlack);
    cx += COLON_W;

    // Minute digits
    draw7SegDigit(cx, clockY, d2, theme.primaryTextBlack);
    cx += DIGIT_W + DIGIT_SPACING;
    draw7SegDigit(cx, clockY, d3, theme.primaryTextBlack);

    // AM/PM indicator in 12h mode
    if (!state.use24h) {
      const char* ampm = (timeinfo.tm_hour < 12) ? "AM" : "PM";
      renderer.drawCenteredText(theme.uiFontId, clockY + DIGIT_H + 20, ampm, theme.primaryTextBlack);
    }
  } else {
    ui::centeredMessage(renderer, theme, theme.uiFontId, "Time not synced");
  }

  ui::ButtonBar buttons("Back", "Menu", "", "");
  ui::buttonBar(renderer, theme, buttons);
}

void exit(Core& core) {
  core.cpu.unthrottle();
  renderer.clearScreen(THEME.backgroundColor);
  renderer.displayBuffer(EInkDisplay::HALF_REFRESH);
  LOG_INF(TAG, "Clock app exit");
}

void renderMenu(Core& core) {
  (void)core;

  char tzLabel[24];
  snprintf(tzLabel, sizeof(tzLabel), "UTC%+d", state.utcOffset);

  const char* fmtLabel = state.use24h ? "24h" : "12h";

  char ntpLabel[24];
  snprintf(ntpLabel, sizeof(ntpLabel), "NTP: %s", NTP_SYNC_LABELS[state.ntpSyncSetting]);

  const char* dateFmtLabel = DATE_FORMAT_LABELS[state.dateFormat];

  const char* items[] = {tzLabel, fmtLabel, dateFmtLabel, ntpLabel, "Sync Now"};
  static constexpr int ITEM_COUNT = 5;

  ui::popupMenu(renderer, THEME, "Clock Settings", items, ITEM_COUNT, state.menuSelected);
}

void onMenuButton(Core& core, Button btn) {
  static constexpr int MENU_ITEM_COUNT = 5;

  switch (btn) {
    case Button::Up:
      state.menuSelected = (state.menuSelected == 0) ? MENU_ITEM_COUNT - 1 : state.menuSelected - 1;
      break;
    case Button::Down:
      state.menuSelected = (state.menuSelected + 1) % MENU_ITEM_COUNT;
      break;
    case Button::Center:
      if (state.menuSelected == 4) {
        core.cpu.unthrottle();
        syncNtpAutoConnect(core);
        state.lastNtpSyncMs = millis();
      }
      break;
    case Button::Left:
    case Button::Right: {
      int delta = (btn == Button::Right) ? 1 : -1;
      if (state.menuSelected == 0) {
        state.utcOffset = static_cast<int8_t>(state.utcOffset + delta);
        if (state.utcOffset > 14) state.utcOffset = -12;
        if (state.utcOffset < -12) state.utcOffset = 14;
        configTime(state.utcOffset * 3600, 0, "pool.ntp.org");  // just changes offset, servers don't matter
      } else if (state.menuSelected == 1) {
        state.use24h = !state.use24h;
      } else if (state.menuSelected == 2) {
        state.dateFormat = static_cast<int8_t>((state.dateFormat + delta + DATE_FORMAT_COUNT) % DATE_FORMAT_COUNT);
      } else if (state.menuSelected == 3) {
        state.ntpSyncSetting = static_cast<int8_t>((state.ntpSyncSetting + delta + NTP_SYNC_COUNT) % NTP_SYNC_COUNT);
      }
      saveSettings(core);
      break;
    }
    default:
      break;
  }
}

}  // namespace clock_app
}  // namespace papyrix
