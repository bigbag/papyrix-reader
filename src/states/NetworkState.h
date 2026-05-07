#pragma once

#include <cstdint>
#include <memory>

#include "../ui/views/NetworkViews.h"
#include "../ui/views/UtilityViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

class PapyrixWebServer;

enum class NetworkScreen : uint8_t {
  ModeSelect,
  WifiList,
  PasswordEntry,
  Connecting,
  SavePrompt,
  ServerRunning,
};

class NetworkState : public State {
 public:
  explicit NetworkState(GfxRenderer& renderer);
  ~NetworkState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Network; }

 private:
  GfxRenderer& renderer_;
  NetworkScreen currentScreen_;
  bool needsRender_;
  bool goBack_;
  bool goHome_;

  // Views (all stack-allocated)
  ui::NetworkModeView modeView_ = {};
  ui::WifiListView wifiListView_ = {};
  ui::KeyboardView keyboardView_ = {};
  ui::WifiConnectingView connectingView_ = {};
  ui::ConfirmView confirmView_ = {};
  ui::WebServerView serverView_ = {};

  // WebServer: heap-allocated ONLY when running
  std::unique_ptr<PapyrixWebServer> server_;

  // State tracking
  char selectedSSID_[33];
  bool passwordJustEntered_;
  bool goCalibreSync_;
  bool goApp_;
  bool directHotspotFlow_;
  uint8_t scanRetryCount_ = 0;
  uint32_t scanRetryAt_ = 0;
  static constexpr uint8_t MAX_SCAN_RETRIES = 2;

  // Screen handlers
  void handleModeSelect(Core& core, Button button);
  void handleWifiList(Core& core, Button button);
  void handlePasswordEntry(Core& core, Button button);
  void handleConnecting(Core& core, Button button);
  void handleSavePrompt(Core& core, Button button);
  void handleServerRunning(Core& core, Button button);

  // Actions
  void startWifiScan(Core& core);
  void connectToNetwork(Core& core, const char* ssid, const char* password);
  void startHotspot(Core& core);
  void startWebServer(Core& core);
  void stopWebServer(Core& core);
};

}  // namespace papyrix
