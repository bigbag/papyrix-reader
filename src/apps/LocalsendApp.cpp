#include "LocalsendApp.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <InputManager.h>
#include <LocalsendHttp.h>
#include <LocalsendService.h>
#include <Logging.h>
#include <SDCardManager.h>
#include <SdFat.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <cstring>
#include <new>

#include "../core/Core.h"
#include "../network/WifiCredentialStore.h"
#include "../ui/Elements.h"
#include "MiniApp.h"
#include "ThemeManager.h"

#define TAG "LOCALSEND_APP"

extern GfxRenderer renderer;
extern InputManager inputManager;  // defined in main.cpp; refreshed here while serving

namespace papyrix {
namespace localsend_app {

namespace {

constexpr const char* DEVICE_ALIAS = "PapyriX";
constexpr uint16_t LOCALSEND_PORT = 53317;
constexpr const char* RECEIVE_DIR = "/received";
constexpr size_t JSON_BODY_CAP = 16384;  // prepare-upload/register bodies
constexpr unsigned long CLIENT_READ_TIMEOUT_MS = 2500;
constexpr uint32_t CONNECTION_BUDGET_MS = 120000;
constexpr uint32_t ANNOUNCE_INTERVAL_MS = 3000;
constexpr int ANNOUNCE_COUNT = 3;
// 224.0.0.167 is the LocalSend default multicast group.
const IPAddress MULTICAST_GROUP(224, 0, 0, 167);

enum class Screen : uint8_t { Connecting, Receiving, Failed };

static struct {
  Screen screen = Screen::Connecting;
  bool needsRender = true;
  bool serverStarted = false;
  bool wifiLost = false;
  bool apMode = false;
  char ssid[33] = {0};
  char ip[46] = {0};
  char fingerprint[17] = {0};
  uint32_t filesReceived = 0;
  char lastFileName[129] = {0};
  uint32_t lastAnnounceMs = 0;
  int announcesLeft = 0;
} state;

Core* s_core = nullptr;
std::unique_ptr<hal::WifiSession> wifiSession;
std::unique_ptr<WiFiServer> server;
WiFiUDP udp;
LocalsendService service;
uint32_t rngSource() { return esp_random(); }

// Minimal request-scoped reader over WiFiClient. It pumps input while it
// waits for bytes so Back can abort a slow transfer.
class LineReader {
 public:
  bool backPressed = false;
  bool powerLongPressed = false;
  uint32_t startedMs = 0;
  // handleUpload sets this to 0 once the file body is validated: a slow
  // but progressing transfer must not hit the cap. The stall timeout and
  // Back/Power aborts still bound it.
  uint32_t budgetMs = CONNECTION_BUDGET_MS;
  explicit LineReader(WiFiClient& client) : client_(client) {}

  bool aborted() const { return backPressed || powerLongPressed; }

  void pumpInput() {
    if (!s_core) return;
    s_core->input.resetIdleTimer();  // an active transfer must not trigger auto-sleep
    inputManager.update();           // the main loop is blocked while we serve
    s_core->input.poll();
    Event e;
    while (s_core->events.pop(e)) {
      if (e.type == EventType::ButtonPress && e.button == Button::Back) backPressed = true;
      if (e.type == EventType::ButtonLongPress && e.button == Button::Power) powerLongPressed = true;
      if (e.type == EventType::Tap) {
        const bool frontLrbc = s_core->settings.frontButtonLayout == Settings::FrontLRBC;
        const int action = ui::touch::semanticButtonBarIndex({e.touch.x, e.touch.y}, renderer.getScreenWidth(),
                                                             renderer.getScreenHeight(), frontLrbc);
        if (action == 0) backPressed = true;
      }
    }
  }

  // Reads one CRLF-terminated header line without the break. Returns false at
  // EOF, on timeout, when the line exceeds maxLen-1, or on abort.
  bool readLine(char* out, size_t maxLen) {
    size_t n = 0;
    const unsigned long start = millis();
    while (n + 1 < maxLen) {
      if (budgetMs != 0 && millis() - startedMs >= budgetMs) return false;
      if (aborted()) return false;
      if (client_.available() > 0) {
        const int c = client_.read();
        if (c < 0) return false;
        if (c == '\n') {
          if (n > 0 && out[n - 1] == '\r') n--;
          out[n] = '\0';
          return true;
        }
        out[n++] = static_cast<char>(c);
        continue;
      }
      if (!client_.connected()) return false;
      if (millis() - start > CLIENT_READ_TIMEOUT_MS) return false;
      pumpInput();
      delay(2);
    }
    return false;
  }

  // Streams exactly len bytes. Three modes: with a sink, each chunk goes to
  // the file. Without a sink but with a written counter, bytes accumulate in
  // buf at the written offset (JSON bodies span TCP segments). With neither,
  // the body is drained and discarded. Returns false on any failure. The
  // timeout is a stall timeout: the clock restarts on every byte received.
  bool readBodyTo(uint8_t* buf, size_t bufLen, uint64_t len, FsFile* sink, uint64_t* written) {
    uint64_t left = len;
    unsigned long stallStart = millis();
    while (left > 0) {
      if (budgetMs != 0 && millis() - startedMs >= budgetMs) return false;
      if (aborted()) return false;
      if (client_.available() > 0) {
        uint8_t* dst = buf;
        size_t room = bufLen;
        if (!sink && written && !bodyDest(buf, bufLen, *written, &dst, &room)) return false;
        const size_t want = left < room ? static_cast<size_t>(left) : room;
        const int n = client_.read(dst, want);
        if (n <= 0) return false;
        if (sink && sink->write(buf, n) != n) return false;
        if (written) *written += n;
        left -= n;
        stallStart = millis();
        pumpInput();  // a streaming sender must not starve input polling
        continue;
      }
      if (!client_.connected()) return false;
      if (millis() - stallStart > CLIENT_READ_TIMEOUT_MS) return false;
      pumpInput();
      delay(2);
    }
    return true;
  }
  bool readChunkedBodyTo(FsFile* sink, uint64_t expectedBytes, uint8_t* buf, size_t bufLen, uint64_t* written) {
    LocalsendChunkDecoder decoder(expectedBytes);
    unsigned long stallStart = millis();
    *written = 0;
    for (;;) {
      if (budgetMs != 0 && millis() - startedMs >= budgetMs) return false;
      if (aborted()) return false;
      if (client_.available() > 0) {
        const int n = client_.read(buf, bufLen);
        if (n <= 0) return false;
        stallStart = millis();
        size_t used = 0;
        size_t produced = 0;
        const ChunkFeed result = decoder.feed(buf, static_cast<size_t>(n), &used, buf, bufLen, &produced);
        if (result == ChunkFeed::Invalid) return false;
        if (produced > 0) {
          if (sink->write(buf, produced) != produced) return false;
          *written += produced;
        }
        if (result == ChunkFeed::Complete) return true;
        if (used != static_cast<size_t>(n)) return false;
        pumpInput();
        continue;
      }
      if (!client_.connected()) return false;
      if (millis() - stallStart > CLIENT_READ_TIMEOUT_MS) return false;
      pumpInput();
      delay(2);
    }
  }

  bool writeAll(const char* data, size_t len) {
    while (len > 0) {
      const size_t n = client_.write(reinterpret_cast<const uint8_t*>(data), len);
      if (n == 0) return false;
      data += n;
      len -= n;
    }
    return true;
  }

 private:
  WiFiClient& client_;
};

class PrepareBodyReader {
 public:
  PrepareBodyReader(LineReader& io, uint64_t length) : io_(io), remaining_(length) {}

  int read() {
    if (pos_ == filled_ && !refill()) return -1;
    return buffer_[pos_++];
  }

  size_t readBytes(char* out, size_t length) {
    size_t copied = 0;
    while (copied < length) {
      if (pos_ == filled_ && !refill()) break;
      const size_t available = filled_ - pos_;
      const size_t take = available < length - copied ? available : length - copied;
      memcpy(out + copied, buffer_ + pos_, take);
      copied += take;
      pos_ += take;
    }
    return copied;
  }

  bool finish() {
    if (failed_) return false;
    int c;
    while ((c = read()) >= 0) {
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return !failed_ && remaining_ == 0;
  }

 private:
  bool refill() {
    if (failed_ || remaining_ == 0) return false;
    const size_t want = remaining_ < sizeof(buffer_) ? static_cast<size_t>(remaining_) : sizeof(buffer_);
    uint64_t got = 0;
    if (!io_.readBodyTo(buffer_, sizeof(buffer_), want, nullptr, &got) || got != want) {
      failed_ = true;
      return false;
    }
    remaining_ -= got;
    pos_ = 0;
    filled_ = static_cast<size_t>(got);
    return true;
  }

  LineReader& io_;
  uint64_t remaining_;
  uint8_t buffer_[256];
  size_t pos_ = 0;
  size_t filled_ = 0;
  bool failed_ = false;
};

uint32_t nowMs() { return millis(); }

void sendJson(LineReader& io, int code, const char* body, size_t bodyLen) {
  char head[160];
  const int n = snprintf(head, sizeof(head),
                         "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %u\r\n"
                         "Connection: close\r\n\r\n",
                         code, reasonPhrase(code), static_cast<unsigned>(bodyLen));
  if (n <= 0 || !io.writeAll(head, n)) return;
  if (bodyLen > 0) io.writeAll(body, bodyLen);
}

void sendEmpty(LineReader& io, int code) {
  char head[96];
  const int n = snprintf(head, sizeof(head), "HTTP/1.1 %d %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", code,
                         reasonPhrase(code));
  if (n > 0) io.writeAll(head, n);
}

void sendAnnounce(bool announce) {
  char body[384];
  const size_t n = service.buildAnnounce(body, sizeof(body), announce);
  if (n == 0) return;
  udp.beginPacket(MULTICAST_GROUP, LOCALSEND_PORT);
  udp.write(reinterpret_cast<const uint8_t*>(body), n);
  udp.endPacket();
  LOG_INF(TAG, "LocalSend announce(%d) sent", announce ? 1 : 0);
}

// Answers a sender's multicast announce with our own announce:false reply.
void pollDiscovery() {
  const int len = udp.parsePacket();
  if (len <= 0) return;
  char packet[384];
  const int n = udp.read(packet, sizeof(packet) - 1);
  if (n <= 0) return;
  packet[n] = 0;
  if (strstr(packet, "\"announce\":true") == nullptr) return;
  if (strstr(packet, state.fingerprint) != nullptr) return;  // our own echo
  LOG_INF(TAG, "LocalSend announce from %s", udp.remoteIP().toString().c_str());
  sendAnnounce(false);
}

void handleRegister(LineReader& io, uint64_t contentLength) {
  // The body only carries the sender's identity. Drain it, then answer.
  uint8_t scratch[256];
  if (contentLength > JSON_BODY_CAP) {
    sendEmpty(io, 400);
    return;
  }
  if (!io.readBodyTo(scratch, sizeof(scratch), contentLength, nullptr, nullptr)) return;
  char info[384];
  const size_t n = service.buildInfo(info, sizeof(info));
  if (n == 0) {
    sendEmpty(io, 500);
    return;
  }
  sendJson(io, 200, info, n);
}

void handlePrepareUpload(LineReader& io, uint64_t contentLength, const char* clientIp) {
  if (contentLength == 0 || contentLength > JSON_BODY_CAP) {
    sendEmpty(io, 400);
    return;
  }
  PrepareBodyReader body(io, contentLength);

  // Filter: only the three used fields allocate. Sender previews and
  // metadata stay out of the heap.
  JsonDocument filter;
  filter["files"]["*"]["id"] = true;
  filter["files"]["*"]["fileName"] = true;
  filter["files"]["*"]["size"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok || !body.finish() ||
      !doc["files"].is<JsonObject>()) {
    sendEmpty(io, 400);
    return;
  }
  JsonObject files = doc["files"].as<JsonObject>();
  if (files.size() == 0) {
    sendEmpty(io, 204);
    return;
  }

  LocalsendIncomingFile incoming[LocalsendService::MAX_FILES];
  int count = 0;
  for (JsonPair kv : files) {
    if (count >= LocalsendService::MAX_FILES) break;
    JsonObject f = kv.value().as<JsonObject>();
    if (f.isNull()) continue;
    const char* id = f["id"] | kv.key().c_str();
    const char* name = f["fileName"] | "";
    incoming[count] = {id, name, static_cast<uint64_t>(f["size"] | 0)};
    count++;
  }

  const LocalsendPrepareStatus status = service.prepareUpload(incoming, count, nowMs(), clientIp);
  switch (status) {
    case LocalsendPrepareStatus::NoFiles:
      sendEmpty(io, 204);
      return;
    case LocalsendPrepareStatus::Invalid:
      sendEmpty(io, 400);
      return;
    case LocalsendPrepareStatus::Busy:
      sendEmpty(io, 409);
      return;
    case LocalsendPrepareStatus::Ok:
      break;
  }
  char resp[3 * 1024];
  const size_t n = service.buildPrepareResponse(resp, sizeof(resp));
  if (n == 0) {
    service.endSession();
    sendEmpty(io, 500);
    return;
  }
  sendJson(io, 200, resp, n);
  LOG_INF(TAG, "LocalSend session started");
}

void handleUpload(LineReader& io, const char* target, uint64_t contentLength, bool chunked, const char* clientIp) {
  char sid[17], fileId[65], token[17];
  if (!queryParam(target, "sessionId", sid, sizeof(sid)) || !queryParam(target, "fileId", fileId, sizeof(fileId)) ||
      !queryParam(target, "token", token, sizeof(token))) {
    sendEmpty(io, 400);
    return;
  }
  const LocalsendFileEntry* entry = service.validateUpload(sid, fileId, token, nowMs(), clientIp);
  if (!entry) {
    sendEmpty(io, 403);
    return;
  }
  if (!chunked && contentLength != entry->size) {
    sendEmpty(io, 400);
    return;
  }
  // A validated file body gets no absolute budget: slow SD writes stay under
  // the 2.5s stall timeout, and Back/Power aborts a transfer the user wants
  // gone. The 120s cap stays in force for headers and control bodies.
  io.budgetMs = 0;

  char relName[160];
  int index = 0;
  char full[200];
  for (;; index++) {
    if (index > 99 || !LocalsendService::makeCollisionName(entry->fileName, relName, sizeof(relName), index)) {
      sendEmpty(io, 500);
      return;
    }
    snprintf(full, sizeof(full), "%s/%s", RECEIVE_DIR, relName);
    if (!SdMan.exists(full)) break;
  }

  FsFile out = SdMan.open(full, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) {
    sendEmpty(io, 500);
    return;
  }
  static uint8_t buf[4096];
  uint64_t written = 0;
  const bool ok = chunked ? io.readChunkedBodyTo(&out, entry->size, buf, sizeof(buf), &written)
                          : io.readBodyTo(buf, sizeof(buf), contentLength, &out, &written);
  const bool synced = ok && written == entry->size && out.sync();
  out.close();
  if (!synced) {
    sendEmpty(io, 500);
    SdMan.remove(full);  // a partial file is not a received file: drop it
    return;
  }
  service.markReceived(entry->id, nowMs());
  state.filesReceived++;
  strncpy(state.lastFileName, relName, sizeof(state.lastFileName) - 1);
  state.lastFileName[sizeof(state.lastFileName) - 1] = 0;
  state.needsRender = true;
  sendEmpty(io, 200);
  LOG_INF(TAG, "LocalSend received %s (%llu bytes)", relName, static_cast<unsigned long long>(written));
}

void serveClient(WiFiClient& client, LineReader& io) {
  client.setNoDelay(true);
  char clientIp[46];  // INET6_ADDRSTRLEN
  strncpy(clientIp, client.remoteIP().toString().c_str(), sizeof(clientIp) - 1);
  clientIp[sizeof(clientIp) - 1] = 0;

  constexpr size_t REQUEST_LINE_CAP = 320;  // method + target + " HTTP/1.1"
  static_assert(REQUEST_LINE_CAP > MAX_TARGET_LEN + 18, "request line holds the worst-case upload target");
  char line[REQUEST_LINE_CAP];
  if (!io.readLine(line, sizeof(line))) return;
  // Copy method and target out of line: the header loop below reuses the
  // buffer, so the request-line contents are overwritten before dispatch.
  char method[8];
  char target[MAX_TARGET_LEN + 1];
  if (!localsendParseRequestLine(line, method, sizeof(method), target, sizeof(target))) return;

  LocalsendHeaderParser headers;
  for (;;) {
    if (!io.readLine(line, sizeof(line))) return;
    const HeaderFeed fed = headers.feed(line);
    if (fed == HeaderFeed::Done) break;
    if (fed == HeaderFeed::Overflow) {
      // Unread header lines parse as body bytes.
      sendEmpty(io, 400);
      return;
    }
  }
  if (headers.invalid) {
    sendEmpty(io, 400);
    return;
  }
  const uint64_t contentLength = headers.contentLength;
  const LocalsendRoute route = matchLocalsendRoute(method, target);
  if (headers.chunked && route != LocalsendRoute::Upload) {
    sendEmpty(io, 400);
    return;
  }
  switch (route) {
    case LocalsendRoute::Register:
      handleRegister(io, contentLength);
      break;
    case LocalsendRoute::PrepareUpload:
      handlePrepareUpload(io, contentLength, clientIp);
      break;
    case LocalsendRoute::Upload:
      handleUpload(io, target, contentLength, headers.chunked, clientIp);
      break;
    case LocalsendRoute::Cancel: {
      char sid[17];
      if (queryParam(target, "sessionId", sid, sizeof(sid))) service.cancel(sid, clientIp);
      sendEmpty(io, 200);
      break;
    }
    case LocalsendRoute::Info: {
      char info[384];
      const size_t n = service.buildInfo(info, sizeof(info));
      if (n == 0) {
        sendEmpty(io, 500);
        return;
      }
      sendJson(io, 200, info, n);
      break;
    }
    case LocalsendRoute::Unknown:
      sendEmpty(io, 404);
      break;
  }
}

void startServices() {
  // The fingerprint identifies this device; derive it from the MAC so a
  // sender remembers us between sessions.
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(state.fingerprint, sizeof(state.fingerprint), "%04x%08x", static_cast<unsigned>(mac >> 32),
           static_cast<unsigned>(mac & 0xFFFFFFFF));
  service.begin(DEVICE_ALIAS, state.fingerprint, LOCALSEND_PORT, &rngSource);

  server.reset(new (std::nothrow) WiFiServer(LOCALSEND_PORT));
  if (!server) {
    LOG_ERR(TAG, "OOM allocating LocalSend server");
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }
  server->begin();
  if (!*server) {
    LOG_ERR(TAG, "LocalSend listener failed to start");
    server.reset();
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }
  server->setNoDelay(true);

  if (state.apMode) {
    s_core->wifi.getAPIP(state.ip, sizeof(state.ip));
  } else {
    s_core->wifi.getIpAddress(state.ip, sizeof(state.ip));
  }

  SdMan.mkdir(RECEIVE_DIR);

  // Multicast discovery: announce on start, then listen for senders.
  if (!udp.beginMulticast(MULTICAST_GROUP, LOCALSEND_PORT)) {
    LOG_ERR(TAG, "LocalSend multicast join failed");
    server->end();
    server.reset();
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }
  sendAnnounce(true);
  state.announcesLeft = ANNOUNCE_COUNT - 1;
  state.lastAnnounceMs = millis();

  state.serverStarted = true;
  state.screen = Screen::Receiving;
  state.needsRender = true;
  LOG_INF(TAG, "LocalSend receiver at %s, free heap: %d", state.ip, static_cast<int>(ESP.getFreeHeap()));
}

// Clock-app pattern: silently try stored credentials; if none work, hand off
// to the Network picker (recent / join / hotspot) and relaunch on success.
void connectAndServe() {
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCount() == 0) {
    s_core->pendingSync = SyncMode::LocalsendSetup;
    s_core->pendingAppId = APP_LOCALSEND;
    return;
  }

  renderer.clearScreen(THEME.backgroundColor);
  ui::centeredMessage(renderer, THEME, THEME.uiFontId, "Connecting WiFi...");
  renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH, true);

  const auto& creds = WIFI_STORE.getCredentials();
  const int count = WIFI_STORE.getCount();
  for (int i = 0; i < count; i++) {
    LOG_INF(TAG, "Trying WiFi: %s", creds[i].ssid);
    if (s_core->wifi.connect(creds[i].ssid, creds[i].password).ok()) {
      strncpy(state.ssid, creds[i].ssid, sizeof(state.ssid) - 1);
      state.ssid[sizeof(state.ssid) - 1] = '\0';
      if (!WIFI_STORE.promoteCredential(state.ssid)) LOG_ERR(TAG, "Could not save WiFi priority");
      startServices();
      return;
    }
    s_core->wifi.shutdown();
  }

  s_core->pendingSync = SyncMode::LocalsendSetup;
  s_core->pendingAppId = APP_LOCALSEND;
}

}  // namespace

void enter(Core& core) {
  LOG_INF(TAG, "LocalSend app enter, free heap: %d", static_cast<int>(ESP.getFreeHeap()));
  s_core = &core;
  state = {};
  wifiSession.reset(new (std::nothrow) hal::WifiSession(core.wifi, core.cpu));
  if (!wifiSession) {
    core.wifi.shutdown();  // the relaunch left the radio on
    state.screen = Screen::Failed;
    state.needsRender = true;
    return;
  }
  state.apMode = core.wifi.isAPMode();

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
    LOG_ERR(TAG, "WiFi connection lost");
    if (server) server->end();
    udp.stop();
    server.reset();
    wifiSession.reset();  // shuts the radio down and frees the WiFi heap
    state.serverStarted = false;
    state.wifiLost = true;
    state.screen = Screen::Failed;
    state.needsRender = true;
    return true;
  }
  if (state.serverStarted) {
    pollDiscovery();
    // Repeat the announce a few times so a sender that opened after us still
    // sees the device.
    if (state.announcesLeft > 0 && millis() - state.lastAnnounceMs >= ANNOUNCE_INTERVAL_MS) {
      sendAnnounce(true);
      state.announcesLeft--;
      state.lastAnnounceMs = millis();
    }
    // Official senders upload two files in parallel; the second connection
    // waits in the TCP backlog until this one ends. This is safe: the
    // LocalSend upload isolate passes no timeout for uploads (the client
    // factory says to leave it out), so a queued sender does not time out.
    WiFiClient client = server->accept();
    if (client) {
      const uint32_t startMs = millis();
      LOG_INF(TAG, "LocalSend client connected");
      LineReader io(client);
      io.startedMs = startMs;
      serveClient(client, io);
      client.stop();
      LOG_INF(TAG, "Client done in %lu ms, free heap: %d", static_cast<unsigned long>(millis() - startMs),
              static_cast<int>(ESP.getFreeHeap()));
      if (io.powerLongPressed) {
        // Sleep applies in every mode. Hand the event to the launcher.
        core.events.push(Event::buttonLongPress(Button::Power));
      }
      if (io.backPressed) {
        // Hand Back to the launcher so the app stops and the radio shuts down.
        core.events.push(Event::buttonPress(Button::Back));
      }
    }
  }
  return state.needsRender;
}

void onButton(Core& core, Button btn) {
  if (state.screen != Screen::Receiving || btn != Button::Right) return;
  // Open the receive directory in the file manager. Back stops the app.
  strncpy(core.pendingDirectory, RECEIVE_DIR, sizeof(core.pendingDirectory) - 1);
  core.events.push(Event::buttonPress(Button::Back));
}

bool render(Core& core) {
  state.needsRender = false;
  renderer.clearScreen(THEME.backgroundColor);
  const bool ink = THEME.primaryTextBlack;
  if (state.screen == Screen::Connecting) {
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2, "Connecting...", ink);
    renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
    return true;
  }
  if (state.screen == Screen::Failed) {
    renderer.drawCenteredText(THEME.uiFontId, renderer.getScreenHeight() / 2,
                              state.wifiLost ? "WiFi connection lost" : "LocalSend start failed", ink);
    ui::ButtonBar buttons("Back", "", "", "");
    ui::buttonBar(renderer, THEME, buttons);
    renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
    return true;
  }

  ui::title(renderer, THEME, THEME.screenMarginTop, "LocalSend");
  const int lineH = renderer.getLineHeight(THEME.uiFontId);
  int y = THEME.screenMarginTop + renderer.getLineHeight(THEME.readerFontId) + 16;
  char line[96];
  snprintf(line, sizeof(line), "WiFi: %s", state.apMode ? "PapyriX" : (state.ssid[0] ? state.ssid : "connected"));
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  snprintf(line, sizeof(line), "IP: %s", state.ip);
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  snprintf(line, sizeof(line), "Device: %s", DEVICE_ALIAS);
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  ui::text(renderer, THEME, y, "Protocol: LocalSend");
  y += lineH + 16;

  snprintf(line, sizeof(line), "%lu file(s) received to %s", static_cast<unsigned long>(state.filesReceived),
           RECEIVE_DIR);
  ui::text(renderer, THEME, y, line);
  y += lineH + 6;
  if (state.lastFileName[0]) {
    y += ui::textWrapped(renderer, THEME, y, state.lastFileName, 2) * lineH + 6;
  }
  renderer.drawText(THEME.uiFontId, THEME.screenMarginSide + THEME.itemPaddingX, y, "Waiting for files...", ink,
                    EpdFontFamily::ITALIC);
  y += lineH + 6;
  renderer.drawText(THEME.uiFontId, THEME.screenMarginSide + THEME.itemPaddingX, y,
                    "Turn encryption off in LocalSend settings.", ink, EpdFontFamily::ITALIC);

  const int factsBottom = THEME.screenMarginTop + renderer.getLineHeight(THEME.readerFontId) + 16 + 7 * (lineH + 6);
  const int barTop = renderer.getScreenHeight() - 50;
  ui::localsendLogo(renderer, THEME, renderer.getScreenWidth() / 2, (factsBottom + barTop) / 2);
  ui::ButtonBar buttons("Exit", "", "", "Files");
  ui::buttonBar(renderer, THEME, buttons);
  renderer.displayBuffer(papyrix::hal::Display::FAST_REFRESH);
  return true;
}

void exit(Core& core) {
  LOG_INF(TAG, "LocalSend app exit");
  if (state.serverStarted && server) server->end();
  udp.stop();
  server.reset();
  state.serverStarted = false;
  wifiSession.reset();  // shuts the radio down and frees the WiFi heap
  s_core = nullptr;
}

}  // namespace localsend_app
}  // namespace papyrix
