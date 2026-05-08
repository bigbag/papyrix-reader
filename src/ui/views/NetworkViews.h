#pragma once

#include <GfxRenderer.h>
#include <I18n.h>
#include <Theme.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../Elements.h"

namespace ui {

// ============================================================================
// NetworkModeView - WiFi mode selection (Join/Hotspot)
// ============================================================================

struct NetworkModeView {
  static constexpr int ITEM_COUNT = 2;

  ButtonBar buttons;
  int8_t selected = 0;
  bool needsRender = true;

  void moveUp() {
    if (selected > 0) {
      selected--;
      needsRender = true;
    }
  }

  void moveDown() {
    if (selected < ITEM_COUNT - 1) {
      selected++;
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const NetworkModeView& v);

// ============================================================================
// WifiListView - Available network list
// ============================================================================

struct WifiListView {
  static constexpr int MAX_NETWORKS = 16;
  static constexpr int SSID_LEN = 33;  // Max SSID length + null
  static constexpr int PAGE_SIZE = 10;

  struct Network {
    char ssid[SSID_LEN];
    int8_t signal;  // 0-100
    bool secured;
  };

  ButtonBar buttons;
  Network networks[MAX_NETWORKS];
  uint8_t networkCount = 0;
  uint8_t selected = 0;
  uint8_t page = 0;
  bool scanning = false;
  char statusText[32] = "";
  bool needsRender = true;

  void clear() {
    networkCount = 0;
    selected = 0;
    page = 0;
    needsRender = true;
  }

  bool addNetwork(const char* ssid, int signal, bool secured) {
    if (networkCount < MAX_NETWORKS) {
      strncpy(networks[networkCount].ssid, ssid, SSID_LEN - 1);
      networks[networkCount].ssid[SSID_LEN - 1] = '\0';
      networks[networkCount].signal = static_cast<int8_t>(signal);
      networks[networkCount].secured = secured;
      networkCount++;
      return true;
    }
    return false;
  }

  void setScanning(bool s, const char* text = nullptr) {
    scanning = s;
    if (text) {
      strncpy(statusText, text, sizeof(statusText) - 1);
      statusText[sizeof(statusText) - 1] = '\0';
    }
    needsRender = true;
  }

  int getPageStart() const { return page * PAGE_SIZE; }

  int getPageEnd() const {
    int end = (page + 1) * PAGE_SIZE;
    return end > networkCount ? networkCount : end;
  }

  void moveUp() {
    if (selected > 0) {
      selected--;
      if (selected < getPageStart()) {
        page--;
      }
      needsRender = true;
    }
  }

  void moveDown() {
    if (networkCount > 0 && selected < networkCount - 1) {
      selected++;
      if (selected >= getPageEnd()) {
        page++;
      }
      needsRender = true;
    }
  }
};

void render(const GfxRenderer& r, const Theme& t, const WifiListView& v);

// ============================================================================
// WifiConnectingView - Connection status with progress
// ============================================================================

struct WifiConnectingView {
  static constexpr int SSID_MAX_LEN = 33;
  static constexpr int MAX_STATUS_LEN = 48;

  enum class Status : uint8_t { Connecting, Connected, Failed, GettingIP };

  ButtonBar buttons;
  char ssid[SSID_MAX_LEN] = {0};
  char statusMsg[MAX_STATUS_LEN] = "";
  char ipAddress[16] = {0};
  Status status = Status::Connecting;
  bool needsRender = true;

  void setSsid(const char* s) {
    strncpy(ssid, s, SSID_MAX_LEN - 1);
    ssid[SSID_MAX_LEN - 1] = '\0';
    needsRender = true;
  }

  void setConnecting() {
    status = Status::Connecting;
    strncpy(statusMsg, tr(CONNECTING), MAX_STATUS_LEN);
    buttons = ButtonBar{tr(CANCEL)};
    needsRender = true;
  }

  void setGettingIP() {
    status = Status::GettingIP;
    strncpy(statusMsg, tr(GETTING_IP), MAX_STATUS_LEN);
    buttons = ButtonBar{tr(CANCEL)};
    needsRender = true;
  }

  void setConnected(const char* ip) {
    status = Status::Connected;
    strncpy(statusMsg, tr(CONNECTED), MAX_STATUS_LEN);
    strncpy(ipAddress, ip, sizeof(ipAddress) - 1);
    ipAddress[sizeof(ipAddress) - 1] = '\0';
    buttons = ButtonBar{tr(BACK), tr(DONE)};
    needsRender = true;
  }

  void setFailed(const char* reason) {
    status = Status::Failed;
    strncpy(statusMsg, reason, MAX_STATUS_LEN - 1);
    statusMsg[MAX_STATUS_LEN - 1] = '\0';
    buttons = ButtonBar{tr(BACK), tr(RETRY)};
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const WifiConnectingView& v);

// ============================================================================
// WebServerView - Hotspot web server status
// ============================================================================

struct WebServerView {
  static constexpr int SSID_MAX_LEN = 33;
  static constexpr int MAX_IP_LEN = 16;

  ButtonBar buttons;
  char ssid[SSID_MAX_LEN] = {0};
  char ipAddress[MAX_IP_LEN] = {0};
  uint8_t clientCount = 0;
  bool serverRunning = false;
  bool isApMode = false;
  bool needsRender = true;

  void setServerInfo(const char* ap_ssid, const char* ip, bool apMode) {
    strncpy(ssid, ap_ssid, SSID_MAX_LEN - 1);
    ssid[SSID_MAX_LEN - 1] = '\0';
    strncpy(ipAddress, ip, MAX_IP_LEN - 1);
    ipAddress[MAX_IP_LEN - 1] = '\0';
    serverRunning = true;
    isApMode = apMode;
    needsRender = true;
  }

  void setClientCount(uint8_t count) {
    if (clientCount != count) {
      clientCount = count;
      needsRender = true;
    }
  }

  void setStopped() {
    serverRunning = false;
    needsRender = true;
  }
};

void render(const GfxRenderer& r, const Theme& t, const WebServerView& v);

}  // namespace ui
