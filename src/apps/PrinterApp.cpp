#include "PrinterApp.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <BoundedNameQueue.h>
#include <ESPmDNS.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HttpIppConnection.h>
#include <InputManager.h>
#include <IppPrintService.h>
#include <IppTransport.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <WiFi.h>
#include <mdns.h>  // subtype API; ESPmDNS does not expose it

#include <algorithm>
#include <new>
#include <string>
#include <vector>

#include "../core/Core.h"
#include "../network/WifiCredentialStore.h"
#include "../ui/Elements.h"
#include "MiniApp.h"
#include "MiniAppInput.h"
#include "ThemeManager.h"

#define TAG "PRINTER_APP"

extern GfxRenderer renderer;
extern InputManager inputManager;  // defined in main.cpp; refreshed here while serving

namespace papyrix {
namespace printer_app {

namespace {
class PrinterSink;
constexpr const char* PRINTER_NAME = "PapyriX";
constexpr const char* MDNS_HOSTNAME = "papyrix";
constexpr const char* AP_SSID = "PapyriX";  // matches NetworkState hotspot name
constexpr uint16_t IPP_PORT = 631;
constexpr size_t MAX_QUEUE_ENTRIES = 64;
constexpr unsigned long CLIENT_READ_TIMEOUT_MS = 2500;
constexpr uint32_t CONNECTION_BUDGET_MS = 120000;
constexpr const char* QUEUE_DIR = "/printouts";

uint32_t upTimeSeconds() { return millis() / 1000; }

enum class Screen : uint8_t { Connecting, Waiting, PageShowing, Failed };

static struct {
  Screen screen = Screen::Connecting;
  bool needsRender = true;
  bool serverStarted = false;
  bool wifiLost = false;
  bool apMode = false;
  char ssid[33] = {0};
  char ip[46] = {0};
  uint32_t jobsPrinted = 0;
  uint32_t nextPrintSeq = 0;
  char printerUri[80] = {0};
  char printerUuid[46] = {0};     // bare form for the DNS-SD TXT record
  char printerUuidUrn[51] = {0};  // urn:uuid: form for IPP
  std::vector<std::string> queue;
  int queueIndex = -1;
} state;

Core* s_core = nullptr;
std::unique_ptr<hal::WifiSession> wifiSession;
std::unique_ptr<WiFiServer> server;
std::unique_ptr<PrinterSink> sink;
std::unique_ptr<IppPrintService> service;
std::unique_ptr<HttpIppConnection> connection;

// IppTransport over a connected WiFiClient. While waiting for bytes it polls
// input: Back aborts the job and exits the app afterwards.
class ClientTransport final : public IppTransport {
  static constexpr int YIELD_EVERY_READS = 32;

  WiFiClient& client;
  int readsSinceYield = 0;

 public:
  bool backPressed = false;
  bool powerLongPressed = false;
  uint32_t startedMs = 0;
  uint32_t budgetMs = 0;  // absolute connection budget in ms; 0 disables it

  explicit ClientTransport(WiFiClient& client) : client(client) {}

  bool aborted() const { return backPressed || powerLongPressed; }
  void pumpInput() {
    if (!s_core) return;
    s_core->input.resetIdleTimer();  // an active job must not trigger auto-sleep
    inputManager.update();           // main loop is blocked while we serve
    s_core->input.poll();
    Event e;
    while (s_core->events.pop(e)) {
      if (e.type == EventType::ButtonPress && e.button == Button::Back) backPressed = true;
      if (e.type == EventType::ButtonLongPress && e.button == Button::Power) powerLongPressed = true;
      if (e.type == EventType::Tap) {
        // The on-screen Back control must still abort the job.
        const bool frontLrbc = s_core->settings.frontButtonLayout == Settings::FrontLRBC;
        const int action = ui::touch::semanticButtonBarIndex({e.touch.x, e.touch.y}, renderer.getScreenWidth(),
                                                             renderer.getScreenHeight(), frontLrbc);
        if (action == 0) backPressed = true;
      }
    }
  }
  int read(uint8_t* buf, size_t maxLen) override {
    const unsigned long start = millis();
    while (client.connected()) {
      // Unsigned subtraction is wrap-safe: budget elapsed survives millis()
      // rollover.
      if (budgetMs != 0 && millis() - startedMs >= budgetMs) return -1;
      if (aborted()) return -1;  // stop before consuming more of the stream
      const int avail = client.available();
      if (avail > 0) {
        if (++readsSinceYield >= YIELD_EVERY_READS) {
          readsSinceYield = 0;
          pumpInput();  // a streaming client must not starve input polling
          if (aborted()) return -1;
          yield();
        }
        return client.read(buf, maxLen);
      }
      if (millis() - start > CLIENT_READ_TIMEOUT_MS) return -1;
      pumpInput();
      if (aborted()) return -1;
      delay(2);
    }
    return 0;
  }

  bool write(const uint8_t* buf, size_t len) override {
    while (len > 0) {
      const size_t n = client.write(buf, len);
      if (n == 0) return false;
      buf += n;
      len -= n;
    }
    return true;
  }
};

void requestWifiPicker() {
  s_core->pendingSync = SyncMode::PrinterSetup;
  s_core->pendingAppId = APP_PRINTER;
}

void drawPageHints() {
  const bool hasPrev = state.queueIndex > 0;
  const bool hasNext = state.queueIndex >= 0 && state.queueIndex < static_cast<int>(state.queue.size()) - 1;
  ui::ButtonBar buttons("Back", "", hasPrev ? "<" : "", hasNext ? ">" : "");
  const int btnY = renderer.getScreenHeight() - 50;
  renderer.clearArea(0, btnY, renderer.getScreenWidth(), 50, THEME.backgroundColor);
  ui::buttonBar(renderer, THEME, buttons);
}

// 1-bpp BMP writer. Streams logical rows straight out of the panel
// framebuffer through the same orientation mapping the renderer uses, so no
// second full-page buffer is needed (RAM budget does not allow one).
bool saveFramebufferAsBmp(const char* path) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const size_t rowBytes = (static_cast<size_t>(width) + 7) / 8;
  const size_t rowPadded = (rowBytes + 3) & ~static_cast<size_t>(3);
  if (rowPadded > 128) return false;  // panel max width 800 -> 100 row bytes
  const size_t pixelBytes = rowPadded * static_cast<size_t>(height);
  const uint32_t dataSize = static_cast<uint32_t>(14 + 40 + 8 + pixelBytes);
  uint8_t rowPad[128];

  // Exclusive create: a false-negative existence check must never truncate
  // the retained printout.
  FsFile file = SdMan.open(path, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;

  const uint8_t fileHdr[14] = {
      'B',
      'M',
      static_cast<uint8_t>(dataSize),
      static_cast<uint8_t>(dataSize >> 8),
      static_cast<uint8_t>(dataSize >> 16),
      static_cast<uint8_t>(dataSize >> 24),
      0,
      0,
      0,
      0,
      62,
      0,
      0,
      0  // pixel data offset: 14 + 40 + 8
  };
  const uint32_t w32 = static_cast<uint32_t>(width);
  const uint32_t h32 = static_cast<uint32_t>(height);
  const uint32_t imageSize32 = static_cast<uint32_t>(pixelBytes);
  const uint8_t infoHdr[40] = {
      40,
      0,
      0,
      0,
      static_cast<uint8_t>(w32),
      static_cast<uint8_t>(w32 >> 8),
      static_cast<uint8_t>(w32 >> 16),
      static_cast<uint8_t>(w32 >> 24),
      static_cast<uint8_t>(h32),
      static_cast<uint8_t>(h32 >> 8),
      static_cast<uint8_t>(h32 >> 16),
      static_cast<uint8_t>(h32 >> 24),
      1,
      0,  // planes
      1,
      0,  // bits per pixel
      0,
      0,
      0,
      0,  // BI_RGB
      static_cast<uint8_t>(imageSize32),
      static_cast<uint8_t>(imageSize32 >> 8),
      static_cast<uint8_t>(imageSize32 >> 16),
      static_cast<uint8_t>(imageSize32 >> 24),
      0x2E,
      0x0B,
      0,
      0,  // 72 dpi
      0x2E,
      0x0B,
      0,
      0,
      2,
      0,
      0,
      0,  // colors used
      2,
      0,
      0,
      0  // colors important
  };
  const uint8_t palette[8] = {0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0};  // 0=black, 1=white

  bool ok = file.write(fileHdr, sizeof(fileHdr)) == sizeof(fileHdr) &&
            file.write(infoHdr, sizeof(infoHdr)) == sizeof(infoHdr) &&
            file.write(palette, sizeof(palette)) == sizeof(palette);

  const uint8_t* fb = renderer.getFrameBuffer();
  const auto orientation = static_cast<board::DisplayOrientation>(renderer.getOrientation());
  const int16_t panelW = s_core->display.getDisplayWidth();
  const int16_t panelH = s_core->display.getDisplayHeight();
  const size_t panelStride = static_cast<size_t>(s_core->display.getDisplayWidthBytes());
  for (int y = height - 1; ok && y >= 0; y--) {  // BMP rows are bottom-up
    memset(rowPad, 0, rowPadded);
    for (int lx = 0; lx < width; lx++) {
      board::PanelPoint p{};
      if (!board::panelFromLogical(orientation, panelW, panelH, static_cast<int16_t>(lx), static_cast<int16_t>(y), p))
        continue;
      if (p.x < 0 || p.x >= panelW || p.y < 0 || p.y >= panelH) continue;
      if (fb[p.y * panelStride + (p.x >> 3)] & (0x80 >> (p.x & 7)))
        rowPad[lx >> 3] |= static_cast<uint8_t>(0x80 >> (lx & 7));
    }
    ok = file.write(rowPad, rowPadded) == rowPadded;
  }
  // SdFat defers the last sector and directory metadata until sync.
  ok = ok && file.sync();
  file.close();
  if (!ok) SdMan.remove(path);  // never leave a truncated printout in the queue
  return ok;
}

void savePrintout() {
  SdMan.ensureDirectoryExists(QUEUE_DIR);
  char path[96];
  // A persistent sequence keeps names unique and monotonic across reboots.
  // millis() restarts at zero on every boot.
  // Refuse an existing path: never truncate a stored printout. An
  // exhausted counter stays unchanged and never wraps to zero.
  char chosenName[24];
  uint32_t nextSeq = 0;
  // The exists check is a hint; exclusive create decides. A hint's false
  // negative cannot destroy a stored printout.
  const auto nameTaken = [](const char* n) {
    char full[96];
    snprintf(full, sizeof(full), "%s/%s", QUEUE_DIR, n);
    return SdMan.exists(full);
  };
  if (!allocatePrintName(state.nextPrintSeq, 64, nameTaken, chosenName, sizeof(chosenName), nextSeq)) {
    LOG_ERR(TAG, "No free printout name");
    return;
  }
  state.nextPrintSeq = nextSeq;
  snprintf(path, sizeof(path), "%s/%s", QUEUE_DIR, chosenName);
  const uint32_t startMs = millis();
  // The exclusive create can fail when the exists hint was wrong. Retry
  // from the next name before giving up on the page.
  bool saved = saveFramebufferAsBmp(path);
  for (int retry = 0; !saved && retry < 8; retry++) {
    if (!allocatePrintName(state.nextPrintSeq, 64, nameTaken, chosenName, sizeof(chosenName), nextSeq)) break;
    state.nextPrintSeq = nextSeq;
    snprintf(path, sizeof(path), "%s/%s", QUEUE_DIR, chosenName);
    saved = saveFramebufferAsBmp(path);
  }
  if (!saved) {
    LOG_ERR(TAG, "Printout save failed");
    return;
  }
  LOG_INF(TAG, "Saved %s in %lu ms", path, static_cast<unsigned long>(millis() - startMs));
  const char* slash = strrchr(path, '/');
  const char* name = slash ? slash + 1 : path;
  const int index = retainNewestName(state.queue, MAX_QUEUE_ENTRIES, name);
  if (index >= 0) state.queueIndex = index;
}

// Sink: rows accumulate silently in the framebuffer; the finished page is
// shown with a single refresh at page end. Intermediate refreshes cost ~0.5s
// each on e-ink, which makes the whole print feel slow.
class PrinterSink final : public ScaledPageSink {
 public:
  bool onScaledPageBegin(uint32_t pageIndex, int boxX, int boxY, int boxW, int boxH) override {
    pageStartMs_ = millis();
    LOG_INF(TAG, "Receiving page %u: %dx%d at (%d,%d), free heap: %d", static_cast<unsigned>(pageIndex), boxW, boxH,
            boxX, boxY, static_cast<int>(ESP.getFreeHeap()));
    renderer.clearScreen(0xFF);  // print surface is always white, whatever the UI theme
    return true;
  }

  bool onScaledRow(int y, int xOffset, const uint8_t* rowBits, int width) override {
    for (int x = 0; x < width; x++) {
      if (rowBits[x >> 3] & (0x80 >> (x & 7))) renderer.drawPixel(xOffset + x, y, true);
    }
    return true;
  }

  void onScaledPageEnd(bool ok, uint32_t) override {
    if (!ok) {
      LOG_ERR(TAG, "Print job failed");
      state.screen = Screen::Waiting;
      state.needsRender = true;
    }
    // A successful page end is provisional. The service may still reject the
    // job on the remaining body, so the commit waits for onJobAccepted().
  }

  void onJobAccepted() override {
    state.jobsPrinted++;
    // Persist before stamping hints so the stored page stays clean.
    savePrintout();
    state.screen = Screen::PageShowing;
    drawPageHints();
    renderer.displayBuffer(papyrix::hal::Display::FULL_REFRESH);
    LOG_INF(TAG, "Page complete in %lu ms, free heap: %d", static_cast<unsigned long>(millis() - pageStartMs_),
            static_cast<int>(ESP.getFreeHeap()));
  }

 private:
  uint32_t pageStartMs_ = 0;
};

void loadQueue() {
  state.queue.clear();
  FsFile dir = SdMan.open(QUEUE_DIR);
  if (!dir) {
    state.queueIndex = -1;
    return;
  }
  if (dir.isDirectory()) {
    char name[128];
    // Scan every entry. Keep only the MAX_QUEUE_ENTRIES newest names.
    // This bounds the heap and never hides a later printout.
    for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      uint32_t parsedSeq = 0;
      if (name[0] == '.' || !parsePrintSequence(name, parsedSeq)) continue;
      retainNewestName(state.queue, MAX_QUEUE_ENTRIES, name);
    }
  }
  dir.close();
  // New names continue past the greatest stored sequence number. Only
  // fully numeric print-<digits>.bmp names count.
  state.nextPrintSeq = nextPrintSequence(state.queue);
  // ponytail: entries beyond the cap stay on the SD card, reachable via USB
  // or pruning. Browse the directory on demand if the cap ever matters.
  state.queueIndex = state.queue.empty() ? -1 : static_cast<int>(state.queue.size()) - 1;
}

void showQueueEntry(int index) {
  if (index < 0 || index >= static_cast<int>(state.queue.size())) return;
  state.queueIndex = index;

  FsFile file;
  const std::string path = std::string(QUEUE_DIR) + "/" + state.queue[index];
  if (!SdMan.openFileForRead(TAG, path.c_str(), file)) {
    LOG_ERR(TAG, "Cannot open %s", path.c_str());
    return;
  }

  Bitmap bitmap(file, true);
  renderer.clearScreen(THEME.backgroundColor);
  if (bitmap.parseHeaders() == BmpReaderError::Ok) {
    const int x = (renderer.getScreenWidth() - bitmap.getWidth()) / 2;
    const int y = (renderer.getScreenHeight() - bitmap.getHeight()) / 2;
    // The streamed path needs no full-page preload and applies the palette.
    if (!renderer.drawBitmapStreamed(bitmap, x < 0 ? 0 : x, y < 0 ? 0 : y)) {
      renderer.drawBitmap(bitmap, x < 0 ? 0 : x, y < 0 ? 0 : y, renderer.getScreenWidth(), renderer.getScreenHeight());
    }
  } else {
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Invalid image");
  }
  file.close();

  state.screen = Screen::PageShowing;
  drawPageHints();
  renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
}

bool startMdns() {
  if (!MDNS.begin(MDNS_HOSTNAME)) {
    LOG_ERR(TAG, "mDNS failed to start");
    MDNS.end();  // free a partially initialized mDNS
    return false;
  }
  MDNS.addService("ipp", "tcp", IPP_PORT);
  const struct {
    const char* key;
    const char* value;
  } txt[] = {
      {"txtvers", "1"},
      {"qtotal", "1"},
      {"rp", "ipp/print"},
      {"ty", PRINTER_NAME},
      {"adminurl", "http://papyrix.local:631/"},
      {"pdl", "image/urf,image/pwg-raster"},
      {"URF", "V1.4,W8,SRGB24,CP1,RS300,DM1"},
      {"Color", "F"},
      {"Duplex", "F"},
  };
  for (const auto& row : txt) MDNS.addServiceTxt("ipp", "tcp", row.key, row.value);
  MDNS.addServiceTxt("ipp", "tcp", "UUID", static_cast<const char*>(state.printerUuid));

  // AirPrint clients only treat an _ipp._tcp service as a driverless printer
  // when it also advertises the "_universal" subtype; without it macOS lists
  // the printer but demands a driver. ESPmDNS has no subtype API, so call the
  // underlying ESP-IDF mDNS component directly (nullptr instance/hostname =
  // first matching service on the local host).
  const esp_err_t subErr = mdns_service_subtype_add_for_host(nullptr, "_ipp", "_tcp", nullptr, "_universal");
  if (subErr != ESP_OK) {
    LOG_ERR(TAG, "mDNS _universal subtype failed (%d)", static_cast<int>(subErr));
    MDNS.end();
    return false;  // without it macOS demands a driver
  }
  LOG_INF(TAG, "mDNS: _ipp._tcp,_universal advertised as '%s'", PRINTER_NAME);
  return true;
}

void startServices() {
  state.apMode = s_core->wifi.isAPMode();
  if (state.apMode) {
    s_core->wifi.getAPIP(state.ip, sizeof(state.ip));
    strncpy(state.ssid, AP_SSID, sizeof(state.ssid) - 1);
    state.ssid[sizeof(state.ssid) - 1] = '\0';
  } else {
    s_core->wifi.getIpAddress(state.ip, sizeof(state.ip));
  }
  snprintf(state.printerUri, sizeof(state.printerUri), "ipp://%s:%u/ipp/print", state.ip, IPP_PORT);
  // One stable UUID per device: two readers on one network must not share an
  // identity. The radio MAC supplies the bytes; the version and variant
  // nibbles keep the RFC 4122 shape.
  {
    uint64_t mac = ESP.getEfuseMac();
    const uint8_t b[6] = {static_cast<uint8_t>(mac >> 40), static_cast<uint8_t>(mac >> 32),
                          static_cast<uint8_t>(mac >> 24), static_cast<uint8_t>(mac >> 16),
                          static_cast<uint8_t>(mac >> 8),  static_cast<uint8_t>(mac)};
    snprintf(state.printerUuid, sizeof(state.printerUuid), "5c01d2%02x-41%02x-4a%02x-8b%02x-%02x%02x9c3af00d", b[0],
             b[1], b[2], b[3], b[4], b[5]);
  }
  snprintf(state.printerUuidUrn, sizeof(state.printerUuidUrn), "urn:uuid:%s", state.printerUuid);
  IppServiceConfig cfg;
  cfg.printerName = PRINTER_NAME;
  cfg.makeAndModel = "PapyriX E-Reader";
  cfg.printerUri = state.printerUri;
  cfg.uuidUri = state.printerUuidUrn;             // must match the DNS-SD UUID TXT record
  cfg.moreInfoUrl = "http://papyrix.local:631/";  // the IPP socket answers GET

  sink.reset(new (std::nothrow) PrinterSink());
  if (sink)
    service.reset(new (std::nothrow)
                      IppPrintService(cfg, *sink, renderer.getScreenWidth(), renderer.getScreenHeight()));
  if (service) connection.reset(new (std::nothrow) HttpIppConnection(*service, cfg.maxJobBytes));
  server.reset(new (std::nothrow) WiFiServer(IPP_PORT));
  if (!connection || !server) {
    LOG_ERR(TAG, "OOM allocating IPP service");
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }

  server->begin();
  if (!*server) {
    LOG_ERR(TAG, "IPP listener failed to start");
    connection.reset();
    service.reset();
    sink.reset();
    server.reset();
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }
  server->setNoDelay(true);
  loadQueue();
  if (!startMdns()) {
    // Without the subtype, macOS sees a printer that demands a driver.
    server->end();
    MDNS.end();
    connection.reset();
    service.reset();
    sink.reset();
    server.reset();
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }
  state.serverStarted = true;
  state.screen = Screen::Waiting;
  state.needsRender = true;
  LOG_INF(TAG, "IPP printer at %s, free heap: %d", state.printerUri, static_cast<int>(ESP.getFreeHeap()));
}

// Clock-app pattern: silently try stored credentials; if none work, hand off
// to the Network picker (recent / join / hotspot) and relaunch on success.
void connectAndServe() {
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCount() == 0) {
    requestWifiPicker();
    return;
  }

  renderer.clearScreen(THEME.backgroundColor);
  ui::centeredMessage(renderer, THEME, THEME.uiFontId, "Connecting WiFi...");
  renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH, true);

  const auto& creds = WIFI_STORE.getCredentials();
  const int count = WIFI_STORE.getCount();
  for (int i = 0; i < count; i++) {
    LOG_INF(TAG, "Trying WiFi: %s", creds[i].ssid);
    if (s_core->wifi.connect(creds[i].ssid, creds[i].password, 15000, nullptr).ok()) {
      strncpy(state.ssid, creds[i].ssid, sizeof(state.ssid) - 1);
      state.ssid[sizeof(state.ssid) - 1] = '\0';
      if (!WIFI_STORE.promoteCredential(state.ssid)) LOG_ERR(TAG, "Could not save WiFi priority");
      startServices();
      return;
    }
    s_core->wifi.shutdown();
  }

  requestWifiPicker();
}

}  // namespace
void enter(Core& core) {
  LOG_INF(TAG, "Printer app enter, free heap: %d", static_cast<int>(ESP.getFreeHeap()));
  s_core = &core;
  state = {};
  wifiSession.reset(new (std::nothrow) hal::WifiSession(core.wifi, core.cpu));
  if (!wifiSession) {
    core.wifi.shutdown();  // the relaunch left the radio on
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }

  if (core.wifi.isConnected() || core.wifi.isAPMode()) {
    startServices();
    return;
  }
  connectAndServe();
}

bool update(Core& core) {
  // WifiRadio::isConnected() caches the ownership flag; WiFi.status()
  // observes the live station link.
  const bool linkUp = state.apMode || (core.wifi.isConnected() && WiFi.status() == WL_CONNECTED);
  if (state.serverStarted && !linkUp) {
    // The station link died. Stop serving; Back leaves the app.
    LOG_ERR(TAG, "WiFi connection lost");
    if (server) server->end();
    MDNS.end();
    connection.reset();
    service.reset();
    sink.reset();
    server.reset();
    wifiSession.reset();  // shuts the radio down and frees the WiFi heap
    state.serverStarted = false;
    state.wifiLost = true;
    state.screen = Screen::Failed;
    state.needsRender = true;
    return true;
  }
  if ((state.screen == Screen::Waiting || state.screen == Screen::PageShowing) && state.serverStarted) {
    WiFiClient client = server->accept();
    if (client) {
      const uint32_t startMs = millis();
      LOG_INF(TAG, "Client connected");
      client.setNoDelay(true);
      ClientTransport transport(client);
      // The per-read timeout resets on every byte. An absolute budget is the
      // only bound on a slow single request.
      transport.startedMs = startMs;
      transport.budgetMs = CONNECTION_BUDGET_MS;
      connection->serve(transport, upTimeSeconds);
      client.stop();
      LOG_INF(TAG, "Client done in %lu ms, free heap: %d", static_cast<unsigned long>(millis() - startMs),
              static_cast<int>(ESP.getFreeHeap()));
      if (transport.powerLongPressed) {
        // Sleep applies in every mode. Hand the event to the launcher.
        core.events.push(Event::buttonLongPress(Button::Power));
      }
      if (transport.backPressed) {
        // Hand Back to the launcher so the app stops and the radio shuts down.
        core.events.push(Event::buttonPress(Button::Back));
      }
    }
  }
  return state.needsRender;
}

static void onButton(Core& core, Button btn) {
  if (state.screen == Screen::Waiting && btn == Button::Right) {
    // Open the printout directory in the file manager. Back stops the app.
    SdMan.ensureDirectoryExists(QUEUE_DIR);
    strncpy(core.pendingDirectory, QUEUE_DIR, sizeof(core.pendingDirectory) - 1);
    core.events.push(Event::buttonPress(Button::Back));
    return;
  }
  const bool prev = btn == Button::Left || btn == Button::Up;
  const bool next = btn == Button::Right || btn == Button::Down;
  if (state.queue.empty() || (!prev && !next)) return;
  if (state.screen == Screen::Waiting) {
    showQueueEntry(state.queueIndex < 0 ? static_cast<int>(state.queue.size()) - 1 : state.queueIndex);
  } else if (state.screen == Screen::PageShowing && prev && state.queueIndex > 0) {
    showQueueEntry(state.queueIndex - 1);
  } else if (state.screen == Screen::PageShowing && next &&
             state.queueIndex < static_cast<int>(state.queue.size()) - 1) {
    showQueueEntry(state.queueIndex + 1);
  }
}

miniapps::Action handleEvent(Core& core, const Event& event, bool menuOpen) {
  return handleAppButtons(core, event, menuOpen, onButton, nullptr);
}

miniapps::Frame render(Core& core, bool) {
  state.needsRender = false;
  if (state.screen == Screen::PageShowing) {
    // The page is drawn straight into the framebuffer as rows decode.
    return {};
  }

  renderer.clearScreen(THEME.backgroundColor);
  const bool ink = THEME.primaryTextBlack;
  if (state.screen == Screen::Connecting) {
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Connecting...", ink);
    renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
    return {};
  }
  if (state.screen == Screen::Failed) {
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2,
                              state.wifiLost ? "WiFi connection lost" : "Printer start failed", ink);
    ui::ButtonBar buttons("Back", "", "", "");
    ui::buttonBar(renderer, THEME, buttons);
    renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
    return {};
  }

  ui::title(renderer, THEME, THEME.screenMarginTop, "Printer");
  const int lineH = renderer.getLineHeight(THEME.uiFontId);
  int y = THEME.screenMarginTop + renderer.getLineHeight(THEME.readerFontId) + 16;
  char line[96];
  snprintf(line, sizeof(line), "WiFi: %s", state.apMode ? AP_SSID : (state.ssid[0] ? state.ssid : "connected"));
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  snprintf(line, sizeof(line), "IP: %s", state.ip);
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  ui::text(renderer, THEME, y, "Protocol: IPP");
  y += lineH + 6;
  snprintf(line, sizeof(line), "URI: %s", state.printerUri);
  y += ui::textWrapped(renderer, THEME, y, line, 2) * lineH + 16;

  if (state.jobsPrinted > 0) {
    snprintf(line, sizeof(line), "%lu page(s) printed", static_cast<unsigned long>(state.jobsPrinted));
    ui::text(renderer, THEME, y, line);
    y += lineH + 6;
  }
  snprintf(line, sizeof(line), "%d printout(s) in %s", static_cast<int>(state.queue.size()), QUEUE_DIR);
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  renderer.drawText(THEME.uiFontId, THEME.screenMarginSide + THEME.itemPaddingX, y, "Waiting for print...", ink,
                    EpdFontFamily::ITALIC);

  const int factsBottom = THEME.screenMarginTop + renderer.getLineHeight(THEME.readerFontId) + 16 + 7 * (lineH + 6);
  const int barTop = renderer.getScreenHeight() - 50;
  ui::printerLogo(renderer, THEME, renderer.getScreenWidth() / 2, (factsBottom + barTop) / 2);
  ui::ButtonBar buttons("Exit", "", state.queue.empty() ? "" : "<", "Files");
  ui::buttonBar(renderer, THEME, buttons);
  renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
  return {};
}

void exit(Core& core) {
  LOG_INF(TAG, "Printer app exit");
  if (state.serverStarted && server) server->end();
  MDNS.end();
  connection.reset();
  service.reset();
  sink.reset();
  server.reset();
  state.serverStarted = false;
  std::vector<std::string>().swap(state.queue);  // free the printout list's heap
  wifiSession.reset();                           // shuts the radio down and frees the WiFi heap
  s_core = nullptr;
}

}  // namespace printer_app
}  // namespace papyrix
