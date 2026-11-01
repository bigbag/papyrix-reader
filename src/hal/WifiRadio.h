#pragma once

#include <cstddef>
#include <cstdint>

#include "../core/Result.h"
#include "Cpu.h"

namespace papyrix::hal {

// WiFi network info from scan (fixed-size, no heap)
struct WifiNetwork {
  char ssid[33];  // 32 chars + null
  int8_t rssi;
  bool secured;
};

// Network driver - ONLY used for book sync (Calibre, HTTP transfer)
// WiFi fragments heap - shutdown() disables radio and frees stack memory
class WifiRadio {
 public:
  Result<void> init();
  void shutdown();

  bool isInitialized() const { return initialized_; }
  bool isConnected() const { return connected_; }

  Result<void> connect(const char* ssid, const char* password, uint32_t timeoutMs, bool (*abort)());
  void disconnect();

  // Get signal strength (RSSI)
  int8_t signalStrength() const;

  // Get IP address as string
  void getIpAddress(char* buffer, size_t bufferSize) const;

  // WiFi scanning
  Result<void> startScan();
  bool isScanComplete() const;
  int getScanResults(WifiNetwork* out, int maxCount);

  // Access Point mode
  Result<void> startAP(const char* ssid, const char* password = nullptr);
  void stopAP();
  bool isAPMode() const { return apMode_; }
  void getAPIP(char* buffer, size_t bufferSize) const;

 private:
  bool initialized_ = false;
  bool connected_ = false;
  bool apMode_ = false;
  bool scanInProgress_ = false;
};

class WifiSession final {
 public:
  WifiSession(WifiRadio& radio, Cpu& cpu) : radio_(radio), performanceLock_(cpu) {}
  ~WifiSession() { radio_.shutdown(); }

  WifiSession(const WifiSession&) = delete;
  WifiSession& operator=(const WifiSession&) = delete;

 private:
  WifiRadio& radio_;
  Cpu::PerformanceLock performanceLock_;
};

}  // namespace papyrix::hal
