#pragma once

#include <Arduino.h>

#include <cstdint>
#include <string>
#include <vector>

using wifi_mode_t = uint8_t;
constexpr wifi_mode_t WIFI_OFF = 0;
constexpr wifi_mode_t WIFI_MODE_NULL = WIFI_OFF;
constexpr wifi_mode_t WIFI_STA = 1;
constexpr wifi_mode_t WIFI_AP = 2;
constexpr wifi_mode_t WIFI_MODE_STA = WIFI_STA;
constexpr wifi_mode_t WIFI_MODE_AP = WIFI_AP;
constexpr int WL_IDLE_STATUS = 0;
constexpr int WL_NO_SSID_AVAIL = 1;
constexpr int WL_CONNECTED = 3;
constexpr int WL_CONNECT_FAILED = 4;
constexpr int WL_DISCONNECTED = 6;
constexpr int WIFI_SCAN_RUNNING = -1;
constexpr int WIFI_SCAN_FAILED = -2;
constexpr int WIFI_AUTH_OPEN = 0;
using wl_status_t = int;

struct TestIpAddress {
  String toString() const { return String("192.0.2.1"); }
};

struct TestWifi {
  wifi_mode_t driverMode = WIFI_OFF;
  bool failMode = false;
  bool failAP = false;
  bool failBandwidth = false;
  bool failBegin = false;
  bool attemptActive = false;
  uint32_t connectDelayMs = 0;
  uint32_t startedAt = 0;
  wl_status_t linkStatus = WL_DISCONNECTED;
  int16_t scanCount = 0;
  const char* scannedSsid = "fixture";
  std::vector<std::string> attemptedSsids;

  void reset() { *this = TestWifi{}; }
  bool mode(wifi_mode_t next) {
    if (failMode) return false;
    driverMode = next;
    if (next == WIFI_OFF) {
      attemptActive = false;
      linkStatus = WL_DISCONNECTED;
    }
    return true;
  }
  wifi_mode_t getMode() const { return driverMode; }
  void persistent(bool) {}
  bool disconnect(bool off = false, bool = false) {
    attemptActive = false;
    linkStatus = WL_DISCONNECTED;
    return !off || mode(WIFI_OFF);
  }
  wl_status_t begin(const char* ssid, const char*) {
    attemptedSsids.emplace_back(ssid);
    if (!mode(WIFI_STA) || failBegin) return WL_CONNECT_FAILED;
    attemptActive = true;
    startedAt = static_cast<uint32_t>(millis());
    linkStatus = WL_IDLE_STATUS;
    return linkStatus;
  }
  wl_status_t status() {
    if (attemptActive && static_cast<uint32_t>(millis() - startedAt) >= connectDelayMs) linkStatus = WL_CONNECTED;
    return linkStatus;
  }
  void scanDelete() {}
  int16_t scanNetworks(bool, bool = false, bool = false, uint32_t = 0) { return scanCount; }
  int16_t scanComplete() const { return scanCount; }
  String SSID(int) const { return String(scannedSsid); }
  int RSSI(int = 0) const { return -45; }
  int encryptionType(int) const { return WIFI_AUTH_OPEN; }
  bool softAP(const char*, const char* = nullptr) { return mode(WIFI_AP) && !failAP; }
  bool softAPdisconnect(bool off) { return !off || mode(WIFI_OFF); }
  TestIpAddress localIP() const { return {}; }
  TestIpAddress softAPIP() const { return {}; }
};

inline TestWifi WiFi;
