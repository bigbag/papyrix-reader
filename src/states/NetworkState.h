#pragma once

#include <cstdint>
#include <memory>

#include "../ui/views/NetworkViews.h"
#include "../ui/views/UtilityViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {
struct Event;

class PapyrixWebServer;

enum class NetworkScreen : uint8_t {
  ModeSelect,
  SavedNetworks,
  SavedActions,
  EditSsid,
  EditPassword,
  ForgetPrompt,
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

  // Views (all stack-allocated)
  ui::NetworkModeView modeView_ = {};
  ui::WifiMenuView savedMenu_ = {};
  ui::WifiMenuView actionsMenu_ = {};
  ui::WifiListView wifiListView_ = {};
  ui::KeyboardView keyboardView_ = {};
  ui::WifiConnectingView connectingView_ = {};
  ui::ConfirmView confirmView_ = {};
  ui::WebServerView serverView_ = {};

  // WebServer: heap-allocated ONLY when running
  std::unique_ptr<PapyrixWebServer> server_;

  // State tracking
  char selectedSSID_[33];
  char editOriginalSSID_[33] = {};
  char editSSID_[33] = {};
  bool connectingFromSaved_ = false;
  bool passwordJustEntered_;
  bool goCalibreSync_;
  bool goApp_;
  StateId returnState_ = StateId::AppLauncher;
  uint8_t scanRetryCount_ = 0;
  uint32_t scanRetryAt_ = 0;
  uint32_t connectStartedAt_ = 0;
  int8_t autoConnectIndex_ = -1;
  bool connectionStarted_ = false;
  static constexpr uint8_t MAX_SCAN_RETRIES = 2;

  // Screen handlers
  void handleModeSelect(Core& core, Button button);
  void handleWifiList(Core& core, Button button);
  void handleSavedNetworks(Core& core, Button button);
  void handleSavedActions(Core& core, Button button);
  void handleEditSsid(Core& core, Button button);
  void handleEditPassword(Core& core, Button button);
  void handleForgetPrompt(Core& core, Button button);
  void handlePasswordEntry(Core& core, Button button);
  void handleConnecting(Core& core, Button button);
  void handleSavePrompt(Core& core, Button button);
  void handleServerRunning(Core& core, Button button);
  void handleTap(Core& core, const Event& event);

  // Actions
  void startWifiScan(Core& core);
  void refreshSavedNetworks();
  void startEdit(const char* ssid);
  void connectToNetwork(Core& core, const char* ssid, const char* password);
  void updateConnection(Core& core);
  void tryAutoConnect(Core& core);
  void startHotspot(Core& core);
  void startWebServer(Core& core);
  void stopWebServer(Core& core);
};

}  // namespace papyrix
