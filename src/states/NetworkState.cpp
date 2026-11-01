#include "NetworkState.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <new>

#include "../config.h"
#include "../core/Core.h"
#include "../network/PapyrixWebServer.h"
#include "../network/WifiCredentialStore.h"
#include "../ui/Elements.h"
#include "ThemeManager.h"

#define TAG "NET_STATE"

namespace papyrix {

namespace {
constexpr const char* AP_SSID = "PapyriX";
}  // namespace

NetworkState::NetworkState(GfxRenderer& renderer)
    : renderer_(renderer),
      currentScreen_(NetworkScreen::ModeSelect),
      needsRender_(true),
      goBack_(false),
      server_(nullptr),
      passwordJustEntered_(false),
      goCalibreSync_(false),
      goApp_(false) {
  selectedSSID_[0] = '\0';
}

NetworkState::~NetworkState() {
  if (server_) {
    server_->stop();
    server_.reset();
  }
}

void NetworkState::enter(Core& core) {
  LOG_INF(TAG, "Entering");

  currentScreen_ = NetworkScreen::ModeSelect;
  modeView_.selected = 0;
  modeView_.needsRender = true;
  savedMenu_.selected = 0;
  needsRender_ = true;
  goBack_ = false;
  passwordJustEntered_ = false;
  goCalibreSync_ = false;
  goApp_ = false;
  scanRetryCount_ = 0;
  scanRetryAt_ = 0;
  selectedSSID_[0] = '\0';
  connectingFromSaved_ = false;
  editOriginalSSID_[0] = '\0';
  editSSID_[0] = '\0';

  returnState_ = core.pendingSync == SyncMode::WifiSetup ? StateId::Settings : StateId::AppLauncher;

  // Load saved credentials
  WIFI_STORE.loadFromFile();
  modeView_.showSaved = core.pendingSync == SyncMode::WifiSetup;
  modeView_.itemCount = (WIFI_STORE.getCount() > 0 ? 3 : 2) + (modeView_.showSaved ? 1 : 0);
  // NtpSync: skip mode select, go straight to WiFi scan (hotspot is useless for NTP)
  if (core.pendingSync == SyncMode::NtpSync) {
    startWifiScan(core);
    currentScreen_ = NetworkScreen::WifiList;
  }
}

void NetworkState::exit(Core& core) {
  LOG_INF(TAG, "Exiting");

  // Stop web server if running
  stopWebServer(core);
  memset(keyboardView_.input, 0, sizeof(keyboardView_.input));
  keyboardView_.inputLen = 0;

  // Don't shutdown WiFi if transitioning to CalibreSync or app that needs connection
  if (!goCalibreSync_ && !goApp_) {
    core.wifi.shutdown();
  }
}

void NetworkState::handleTap(Core& core, const Event& event) {
  const int width = renderer_.getScreenWidth();
  const int height = renderer_.getScreenHeight();
  const bool frontLrbc = core.settings.frontButtonLayout == Settings::FrontLRBC;
  Button button = Button::Power;
  bool dispatch = false;

  if (currentScreen_ == NetworkScreen::ModeSelect) {
    const auto hit = modeView_.hitTest({event.touch.x, event.touch.y}, width, height,
                                       THEME.itemHeight + ui::NetworkModeView::ROW_SPACING, frontLrbc);
    if (hit.type == ui::NetworkModeView::Hit::Type::Row) {
      modeView_.selected = static_cast<int8_t>(hit.index);
      button = Button::Center;
      dispatch = true;
    } else if (hit.type == ui::NetworkModeView::Hit::Type::Open) {
      button = Button::Center;
      dispatch = true;
    } else if (hit.type == ui::NetworkModeView::Hit::Type::Back) {
      button = Button::Back;
      dispatch = true;
    }
    if (dispatch) handleModeSelect(core, button);
    return;
  }

  if (currentScreen_ == NetworkScreen::SavedNetworks || currentScreen_ == NetworkScreen::SavedActions) {
    auto& menu = currentScreen_ == NetworkScreen::SavedNetworks ? savedMenu_ : actionsMenu_;
    const auto hit =
        menu.hitTest({event.touch.x, event.touch.y}, width, height, THEME.itemHeight + THEME.itemSpacing, frontLrbc);
    if (hit.type == ui::WifiMenuView::Hit::Row) {
      menu.selected = static_cast<int8_t>(hit.index);
      button = Button::Center;
    } else if (hit.type == ui::WifiMenuView::Hit::Open) {
      button = Button::Center;
    } else if (hit.type == ui::WifiMenuView::Hit::Back) {
      button = Button::Back;
    } else if (hit.type == ui::WifiMenuView::Hit::MoveUp) {
      button = Button::Left;
    } else if (hit.type == ui::WifiMenuView::Hit::MoveDown) {
      button = Button::Right;
    } else {
      return;
    }
    if (currentScreen_ == NetworkScreen::SavedNetworks)
      handleSavedNetworks(core, button);
    else
      handleSavedActions(core, button);
    return;
  }

  if (currentScreen_ == NetworkScreen::WifiList) {
    const auto hit = wifiListView_.hitTest({event.touch.x, event.touch.y}, width, height,
                                           THEME.itemHeight + THEME.itemSpacing, frontLrbc);
    if (hit.type == ui::WifiListView::Hit::Type::Row) {
      wifiListView_.selected = static_cast<uint8_t>(hit.index);
      button = Button::Center;
      dispatch = true;
    } else if (hit.type == ui::WifiListView::Hit::Type::Connect) {
      button = Button::Center;
      dispatch = true;
    } else if (hit.type == ui::WifiListView::Hit::Type::Scan) {
      button = Button::Right;
      dispatch = true;
    } else if (hit.type == ui::WifiListView::Hit::Type::Back) {
      button = Button::Back;
      dispatch = true;
    }
    if (dispatch) handleWifiList(core, button);
    return;
  }

  if (currentScreen_ == NetworkScreen::PasswordEntry || currentScreen_ == NetworkScreen::EditSsid ||
      currentScreen_ == NetworkScreen::EditPassword) {
    const auto hit =
        keyboardView_.hitTest({event.touch.x, event.touch.y}, width, height, THEME.screenMarginSide, frontLrbc);
    if (hit.type == ui::KeyboardView::Hit::Type::Key) {
      keyboardView_.keyboard.cursorY = static_cast<int8_t>(hit.row);
      keyboardView_.keyboard.cursorX =
          static_cast<int8_t>(hit.row == 0 ? (hit.column <= 2 ? 1 : (hit.column <= 6 ? 4 : 8)) : hit.column);
    } else if (hit.type != ui::KeyboardView::Hit::Type::Back) {
      return;
    }
    const Button pressed = hit.type == ui::KeyboardView::Hit::Type::Back ? Button::Back : Button::Center;
    if (currentScreen_ == NetworkScreen::EditSsid)
      handleEditSsid(core, pressed);
    else if (currentScreen_ == NetworkScreen::EditPassword)
      handleEditPassword(core, pressed);
    else
      handlePasswordEntry(core, pressed);
    return;
  }

  if (currentScreen_ == NetworkScreen::Connecting) {
    const auto hit = connectingView_.hitTest({event.touch.x, event.touch.y}, width, height, frontLrbc);
    if (hit == ui::WifiConnectingView::Hit::Back) handleConnecting(core, Button::Back);
    if (hit == ui::WifiConnectingView::Hit::Primary) handleConnecting(core, Button::Center);
    return;
  }

  if (currentScreen_ == NetworkScreen::SavePrompt) {
    const auto layout = ui::confirmationDialogBounds(renderer_, THEME, confirmView_.message);
    const auto hit = confirmView_.hitTest({event.touch.x, event.touch.y}, layout, width, height, frontLrbc);
    if (hit == ui::ConfirmView::Hit::Yes || hit == ui::ConfirmView::Hit::No) {
      confirmView_.selected = hit == ui::ConfirmView::Hit::Yes ? 0 : 1;
      handleSavePrompt(core, Button::Center);
    } else if (hit == ui::ConfirmView::Hit::Back) {
      handleSavePrompt(core, Button::Back);
    } else if (hit == ui::ConfirmView::Hit::Select) {
      handleSavePrompt(core, Button::Center);
    }
    return;
  }

  if (currentScreen_ == NetworkScreen::ForgetPrompt) {
    const auto layout = ui::confirmationDialogBounds(renderer_, THEME, confirmView_.message);
    const auto hit = confirmView_.hitTest({event.touch.x, event.touch.y}, layout, width, height, frontLrbc);
    if (hit == ui::ConfirmView::Hit::Yes || hit == ui::ConfirmView::Hit::No) {
      confirmView_.selected = hit == ui::ConfirmView::Hit::Yes ? 0 : 1;
      handleForgetPrompt(core, Button::Center);
    } else if (hit == ui::ConfirmView::Hit::Back) {
      handleForgetPrompt(core, Button::Back);
    } else if (hit == ui::ConfirmView::Hit::Select) {
      handleForgetPrompt(core, Button::Center);
    }
    return;
  }

  if (currentScreen_ == NetworkScreen::ServerRunning &&
      serverView_.hitTest({event.touch.x, event.touch.y}, width, height, frontLrbc) == ui::WebServerView::Hit::Stop) {
    handleServerRunning(core, Button::Back);
  }
}

StateTransition NetworkState::update(Core& core) {
  // Handle server client processing if running
  if (server_ && currentScreen_ == NetworkScreen::ServerRunning) {
    server_->handleClient();
  }

  // Check for deferred scan retry
  if (scanRetryAt_ != 0 && millis() >= scanRetryAt_) {
    scanRetryAt_ = 0;
    if (currentScreen_ != NetworkScreen::WifiList) {
      scanRetryCount_ = 0;
    } else if (core.wifi.startScan().ok()) {
      wifiListView_.setScanning(true, tr(SCANNING));
      needsRender_ = true;
    } else {
      wifiListView_.setScanning(false);
      needsRender_ = true;
    }
  }

  // Check for scan completion (skip while retry is pending)
  if (currentScreen_ == NetworkScreen::WifiList && wifiListView_.scanning && scanRetryAt_ == 0) {
    if (core.wifi.isScanComplete()) {
      hal::WifiNetwork networks[ui::WifiListView::MAX_NETWORKS];
      int count = core.wifi.getScanResults(networks, ui::WifiListView::MAX_NETWORKS);

      if (count == 0 && scanRetryCount_ < MAX_SCAN_RETRIES) {
        scanRetryCount_++;
        LOG_DBG(TAG, "Scan returned 0 results, retry %d/%d", scanRetryCount_, MAX_SCAN_RETRIES);
        wifiListView_.setScanning(true, tr(INITIALIZING_WIFI));
        scanRetryAt_ = millis() + 500;
        needsRender_ = true;
        return StateTransition::stay(StateId::Network);
      }

      wifiListView_.clear();
      for (int i = 0; i < count; i++) {
        // Convert RSSI to percentage (roughly -100 to -30 dBm -> 0-100%)
        int signal = constrain(map(networks[i].rssi, -100, -30, 0, 100), 0, 100);
        wifiListView_.addNetwork(networks[i].ssid, signal, networks[i].secured);
      }

      scanRetryCount_ = 0;
      wifiListView_.setScanning(false);
      needsRender_ = true;
    }
  }

  Event e;
  while (core.events.pop(e)) {
    if (e.type == EventType::Tap) {
      handleTap(core, e);
      continue;
    }
    if (e.type == EventType::ButtonRepeat) {
      // Repeat only for navigational screens
      if (currentScreen_ != NetworkScreen::ModeSelect && currentScreen_ != NetworkScreen::WifiList &&
          currentScreen_ != NetworkScreen::PasswordEntry && currentScreen_ != NetworkScreen::SavedNetworks &&
          currentScreen_ != NetworkScreen::SavedActions && currentScreen_ != NetworkScreen::EditSsid &&
          currentScreen_ != NetworkScreen::EditPassword)
        continue;
    } else if (e.type != EventType::ButtonPress) {
      continue;
    }

    switch (currentScreen_) {
      case NetworkScreen::ModeSelect:
        handleModeSelect(core, e.button);
        break;
      case NetworkScreen::SavedNetworks:
        handleSavedNetworks(core, e.button);
        break;
      case NetworkScreen::SavedActions:
        handleSavedActions(core, e.button);
        break;
      case NetworkScreen::EditSsid:
        handleEditSsid(core, e.button);
        break;
      case NetworkScreen::EditPassword:
        handleEditPassword(core, e.button);
        break;
      case NetworkScreen::ForgetPrompt:
        handleForgetPrompt(core, e.button);
        break;
      case NetworkScreen::WifiList:
        handleWifiList(core, e.button);
        break;
      case NetworkScreen::PasswordEntry:
        handlePasswordEntry(core, e.button);
        break;
      case NetworkScreen::Connecting:
        handleConnecting(core, e.button);
        break;
      case NetworkScreen::SavePrompt:
        handleSavePrompt(core, e.button);
        break;
      case NetworkScreen::ServerRunning:
        handleServerRunning(core, e.button);
        break;
    }
  }

  if (goBack_) {
    goBack_ = false;
    core.pendingSync = SyncMode::None;
    core.pendingAppId = -1;
    return StateTransition::to(returnState_);
  }

  if (goCalibreSync_) {
    // goCalibreSync_ stays true so exit() knows not to shutdown WiFi
    return StateTransition::to(StateId::CalibreSync);
  }

  if (goApp_) {
    // goApp_ stays true so exit() knows not to shutdown WiFi
    return StateTransition::to(StateId::AppLauncher);
  }

  return StateTransition::stay(StateId::Network);
}

void NetworkState::render(Core& core) {
  if (!needsRender_) {
    bool viewNeedsRender = false;
    switch (currentScreen_) {
      case NetworkScreen::ModeSelect:
        viewNeedsRender = modeView_.needsRender;
        break;
      case NetworkScreen::SavedNetworks:
        viewNeedsRender = savedMenu_.needsRender;
        break;
      case NetworkScreen::SavedActions:
        viewNeedsRender = actionsMenu_.needsRender;
        break;
      case NetworkScreen::EditSsid:
      case NetworkScreen::EditPassword:
        viewNeedsRender = keyboardView_.needsRender;
        break;
      case NetworkScreen::ForgetPrompt:
        viewNeedsRender = confirmView_.needsRender;
        break;
      case NetworkScreen::WifiList:
        viewNeedsRender = wifiListView_.needsRender;
        break;
      case NetworkScreen::PasswordEntry:
        viewNeedsRender = keyboardView_.needsRender;
        break;
      case NetworkScreen::Connecting:
        viewNeedsRender = connectingView_.needsRender;
        break;
      case NetworkScreen::SavePrompt:
        viewNeedsRender = confirmView_.needsRender;
        break;
      case NetworkScreen::ServerRunning:
        viewNeedsRender = serverView_.needsRender;
        break;
    }
    if (!viewNeedsRender) return;
  }

  switch (currentScreen_) {
    case NetworkScreen::ModeSelect:
      ui::render(renderer_, THEME, modeView_);
      modeView_.needsRender = false;
      break;
    case NetworkScreen::SavedNetworks:
      ui::render(renderer_, THEME, savedMenu_);
      savedMenu_.needsRender = false;
      break;
    case NetworkScreen::SavedActions:
      ui::render(renderer_, THEME, actionsMenu_);
      actionsMenu_.needsRender = false;
      break;
    case NetworkScreen::EditSsid:
    case NetworkScreen::EditPassword:
      ui::render(renderer_, THEME, keyboardView_);
      keyboardView_.needsRender = false;
      break;
    case NetworkScreen::ForgetPrompt:
      ui::render(renderer_, THEME, confirmView_);
      confirmView_.needsRender = false;
      break;
    case NetworkScreen::WifiList:
      ui::render(renderer_, THEME, wifiListView_);
      wifiListView_.needsRender = false;
      break;
    case NetworkScreen::PasswordEntry:
      ui::render(renderer_, THEME, keyboardView_);
      keyboardView_.needsRender = false;
      break;
    case NetworkScreen::Connecting:
      ui::render(renderer_, THEME, connectingView_);
      connectingView_.needsRender = false;
      break;
    case NetworkScreen::SavePrompt:
      ui::render(renderer_, THEME, confirmView_);
      confirmView_.needsRender = false;
      break;
    case NetworkScreen::ServerRunning:
      ui::render(renderer_, THEME, serverView_);
      serverView_.needsRender = false;
      break;
  }

  needsRender_ = false;
}

void NetworkState::handleModeSelect(Core& core, Button button) {
  switch (button) {
    case Button::Up:
      modeView_.moveUp();
      needsRender_ = true;
      break;

    case Button::Down:
      modeView_.moveDown();
      needsRender_ = true;
      break;

    case Button::Center: {
      const int joinIdx = modeView_.itemCount - 2;
      const int hotspotIdx = modeView_.itemCount - 1;

      if (modeView_.showSaved && modeView_.selected == 0) {
        refreshSavedNetworks();
        currentScreen_ = NetworkScreen::SavedNetworks;
        needsRender_ = true;
      } else if (modeView_.selected < joinIdx) {
        tryAutoConnect(core);
      } else if (modeView_.selected == joinIdx) {
        startWifiScan(core);
        currentScreen_ = NetworkScreen::WifiList;
        needsRender_ = true;
      } else if (modeView_.selected == hotspotIdx) {
        startHotspot(core);
      }
      break;
    }

    case Button::Back:
      goBack_ = true;
      break;

    default:
      break;
  }
}
void NetworkState::refreshSavedNetworks() {
  savedMenu_.title = tr(SAVED_NETWORKS);
  savedMenu_.buttons = ui::ButtonBar{tr(BACK), tr(OPEN), "<", ">"};
  savedMenu_.count = static_cast<uint8_t>(WIFI_STORE.getCount() + 1);
  const auto* credentials = WIFI_STORE.getCredentials();
  for (int i = 0; i < WIFI_STORE.getCount(); i++) savedMenu_.items[i] = credentials[i].ssid;
  savedMenu_.items[WIFI_STORE.getCount()] = tr(ADD);
  if (savedMenu_.selected >= savedMenu_.count) savedMenu_.selected = savedMenu_.count - 1;
  savedMenu_.needsRender = true;
  modeView_.itemCount = (WIFI_STORE.getCount() > 0 ? 3 : 2) + (modeView_.showSaved ? 1 : 0);
}

void NetworkState::startEdit(const char* ssid) {
  snprintf(editOriginalSSID_, sizeof(editOriginalSSID_), "%s", ssid ? ssid : "");
  keyboardView_.clear();
  if (ssid) {
    snprintf(keyboardView_.input, sizeof(keyboardView_.input), "%s", ssid);
    keyboardView_.inputLen = strlen(keyboardView_.input);
  }
  keyboardView_.setPassword(false);
  keyboardView_.setTitle(tr(ENTER_SSID));
  currentScreen_ = NetworkScreen::EditSsid;
  needsRender_ = true;
}

void NetworkState::handleSavedNetworks(Core& core, Button button) {
  switch (button) {
    case Button::Up:
      savedMenu_.moveUp();
      break;
    case Button::Down:
      savedMenu_.moveDown();
      break;
    case Button::Left:
    case Button::Right: {
      const int direction = button == Button::Left ? -1 : 1;
      const int next = savedMenu_.selected + direction;
      if (savedMenu_.selected < WIFI_STORE.getCount() && next >= 0 && next < WIFI_STORE.getCount()) {
        const char* ssid = WIFI_STORE.getCredentials()[savedMenu_.selected].ssid;
        const bool moved = WIFI_STORE.moveCredential(ssid, direction);
        if (moved) savedMenu_.selected = static_cast<int8_t>(next);
        refreshSavedNetworks();
        if (!moved) savedMenu_.title = tr(SAVE_FAILED);
      }
      break;
    }
    case Button::Center:
      if (savedMenu_.selected == WIFI_STORE.getCount()) {
        if (WIFI_STORE.getCount() >= WifiCredentialStore::MAX_NETWORKS) {
          savedMenu_.title = tr(NETWORK_LIMIT);
          savedMenu_.needsRender = true;
        } else {
          startEdit(nullptr);
        }
      } else {
        const auto& credential = WIFI_STORE.getCredentials()[savedMenu_.selected];
        snprintf(selectedSSID_, sizeof(selectedSSID_), "%s", credential.ssid);
        actionsMenu_.title = selectedSSID_;
        actionsMenu_.items[0] = tr(CONNECT);
        actionsMenu_.items[1] = tr(EDIT_NETWORK);
        actionsMenu_.items[2] = tr(FORGET_NETWORK);
        actionsMenu_.count = 3;
        actionsMenu_.selected = 0;
        actionsMenu_.buttons = ui::ButtonBar{tr(BACK), tr(OPEN)};
        actionsMenu_.needsRender = true;
        currentScreen_ = NetworkScreen::SavedActions;
      }
      break;
    case Button::Back:
      currentScreen_ = NetworkScreen::ModeSelect;
      connectingFromSaved_ = false;
      modeView_.needsRender = true;
      break;
    default:
      break;
  }
  needsRender_ = true;
}

void NetworkState::handleSavedActions(Core& core, Button button) {
  switch (button) {
    case Button::Up:
      actionsMenu_.moveUp();
      break;
    case Button::Down:
      actionsMenu_.moveDown();
      break;
    case Button::Center:
      switch (actionsMenu_.selected) {
        case 0: {
          const auto* credential = WIFI_STORE.findCredential(selectedSSID_);
          if (!credential) {
            refreshSavedNetworks();
            currentScreen_ = NetworkScreen::SavedNetworks;
            break;
          }
          connectingFromSaved_ = true;
          passwordJustEntered_ = false;
          connectToNetwork(core, selectedSSID_, credential->password);
          break;
        }
        case 1:
          startEdit(selectedSSID_);
          break;
        case 2:
          confirmView_.setTitle(tr(FORGET_NETWORK));
          confirmView_.setMessage(selectedSSID_);
          confirmView_.selectNo();
          confirmView_.needsRender = true;
          currentScreen_ = NetworkScreen::ForgetPrompt;
          break;
      }
      break;
    case Button::Back:
      currentScreen_ = NetworkScreen::SavedNetworks;
      savedMenu_.needsRender = true;
      break;
    default:
      break;
  }
  needsRender_ = true;
}

void NetworkState::handleEditSsid(Core& core, Button button) {
  switch (button) {
    case Button::Up:
      keyboardView_.moveUp();
      break;
    case Button::Down:
      keyboardView_.moveDown();
      break;
    case Button::Left:
      keyboardView_.moveLeft();
      break;
    case Button::Right:
      keyboardView_.moveRight();
      break;
    case Button::Center:
      if (!keyboardView_.confirmKey()) break;
      if (keyboardView_.inputLen == 0 || keyboardView_.inputLen > 32) {
        keyboardView_.setTitle(tr(INVALID_SSID));
        break;
      }
      if (strcmp(editOriginalSSID_, keyboardView_.input) != 0 && WIFI_STORE.hasSavedCredential(keyboardView_.input)) {
        keyboardView_.setTitle(tr(SSID_ALREADY_SAVED));
        break;
      }
      snprintf(editSSID_, sizeof(editSSID_), "%s", keyboardView_.input);
      keyboardView_.clear();
      if (const auto* credential = WIFI_STORE.findCredential(editOriginalSSID_)) {
        snprintf(keyboardView_.input, sizeof(keyboardView_.input), "%s", credential->password);
        keyboardView_.inputLen = strlen(keyboardView_.input);
      }
      keyboardView_.setPassword(true);
      keyboardView_.setTitle(tr(ENTER_PASSWORD));
      currentScreen_ = NetworkScreen::EditPassword;
      break;
    case Button::Back:
      currentScreen_ = editOriginalSSID_[0] ? NetworkScreen::SavedActions : NetworkScreen::SavedNetworks;
      break;
    default:
      break;
  }
  keyboardView_.needsRender = true;
  needsRender_ = true;
}

void NetworkState::handleEditPassword(Core& core, Button button) {
  if (button != Button::Center && button != Button::Back) {
    handleEditSsid(core, button);
    return;
  }
  if (button == Button::Back) {
    keyboardView_.clear();
    snprintf(keyboardView_.input, sizeof(keyboardView_.input), "%s", editSSID_);
    keyboardView_.inputLen = strlen(keyboardView_.input);
    keyboardView_.setPassword(false);
    keyboardView_.setTitle(tr(ENTER_SSID));
    currentScreen_ = NetworkScreen::EditSsid;
  } else if (keyboardView_.confirmKey()) {
    const bool saved = editOriginalSSID_[0]
                           ? WIFI_STORE.updateCredential(editOriginalSSID_, editSSID_, keyboardView_.input)
                           : WIFI_STORE.addCredential(editSSID_, keyboardView_.input);
    if (!saved) {
      keyboardView_.setTitle(tr(SAVE_FAILED));
    } else {
      keyboardView_.clear();
      refreshSavedNetworks();
      if (!editOriginalSSID_[0]) savedMenu_.selected = WIFI_STORE.getCount() - 1;
      currentScreen_ = NetworkScreen::SavedNetworks;
    }
  }
  keyboardView_.needsRender = true;
  needsRender_ = true;
}

void NetworkState::handleForgetPrompt(Core& core, Button button) {
  switch (button) {
    case Button::Left:
      confirmView_.selectYes();
      break;
    case Button::Right:
      confirmView_.selectNo();
      break;
    case Button::Center:
      if (confirmView_.isYesSelected()) {
        if (!WIFI_STORE.removeCredential(selectedSSID_)) {
          confirmView_.setTitle(tr(SAVE_FAILED));
          confirmView_.needsRender = true;
          break;
        }
        refreshSavedNetworks();
        if (WIFI_STORE.getCount() > 0 && savedMenu_.selected >= WIFI_STORE.getCount())
          savedMenu_.selected = WIFI_STORE.getCount() - 1;
        currentScreen_ = NetworkScreen::SavedNetworks;
        break;
      }
      [[fallthrough]];
    case Button::Back:
      currentScreen_ = NetworkScreen::SavedActions;
      actionsMenu_.needsRender = true;
      break;
    default:
      break;
  }
  needsRender_ = true;
}

void NetworkState::handleWifiList(Core& core, Button button) {
  switch (button) {
    case Button::Up:
      wifiListView_.moveUp();
      needsRender_ = true;
      break;

    case Button::Down:
      wifiListView_.moveDown();
      needsRender_ = true;
      break;

    case Button::Center:
      if (wifiListView_.networkCount > 0 && !wifiListView_.scanning &&
          wifiListView_.selected < wifiListView_.networkCount) {
        strncpy(selectedSSID_, wifiListView_.networks[wifiListView_.selected].ssid, sizeof(selectedSSID_) - 1);
        selectedSSID_[sizeof(selectedSSID_) - 1] = '\0';

        const auto* cred = WIFI_STORE.findCredential(selectedSSID_);
        if (cred) {
          passwordJustEntered_ = false;
          connectToNetwork(core, cred->ssid, cred->password);
        } else if (wifiListView_.networks[wifiListView_.selected].secured) {
          keyboardView_.setTitle(tr(ENTER_PASSWORD));
          keyboardView_.setPassword(true);
          keyboardView_.clear();
          keyboardView_.needsRender = true;
          currentScreen_ = NetworkScreen::PasswordEntry;
          needsRender_ = true;
        } else {
          passwordJustEntered_ = false;
          connectToNetwork(core, selectedSSID_, "");
        }
      }
      break;

    case Button::Right:
      startWifiScan(core);
      needsRender_ = true;
      break;

    case Button::Back:
      if (core.pendingSync == SyncMode::NtpSync) {
        goBack_ = true;
      } else {
        currentScreen_ = NetworkScreen::ModeSelect;
        modeView_.needsRender = true;
        needsRender_ = true;
      }
      break;

    default:
      break;
  }
}

void NetworkState::handlePasswordEntry(Core& core, Button button) {
  switch (button) {
    case Button::Up:
      keyboardView_.moveUp();
      needsRender_ = true;
      break;

    case Button::Down:
      keyboardView_.moveDown();
      needsRender_ = true;
      break;

    case Button::Left:
      keyboardView_.moveLeft();
      needsRender_ = true;
      break;

    case Button::Right:
      keyboardView_.moveRight();
      needsRender_ = true;
      break;

    case Button::Center:
      if (keyboardView_.confirmKey()) {
        // Input confirmed - try to connect
        passwordJustEntered_ = true;
        connectToNetwork(core, selectedSSID_, keyboardView_.input);
      }
      needsRender_ = true;
      break;

    case Button::Back:
      currentScreen_ = NetworkScreen::WifiList;
      wifiListView_.needsRender = true;
      needsRender_ = true;
      break;

    default:
      break;
  }
}

void NetworkState::handleConnecting(Core& core, Button button) {
  if (button == Button::Back) {
    if (connectingView_.buttons.isActive(0)) {
      if (connectingView_.status == ui::WifiConnectingView::Status::Failed ||
          connectingView_.status == ui::WifiConnectingView::Status::Connected) {
        currentScreen_ = connectingFromSaved_ ? NetworkScreen::SavedActions : NetworkScreen::WifiList;
        if (connectingFromSaved_)
          actionsMenu_.needsRender = true;
        else
          wifiListView_.needsRender = true;
        needsRender_ = true;
      }
    }
  } else if (button == Button::Center) {
    if (connectingView_.buttons.isActive(1)) {
      if (connectingView_.status == ui::WifiConnectingView::Status::Connected) {
        if (core.pendingSync == SyncMode::WifiSetup) {
          if (!WIFI_STORE.hasSavedCredential(selectedSSID_) && passwordJustEntered_) {
            if (WIFI_STORE.addCredential(selectedSSID_, keyboardView_.input))
              WIFI_STORE.promoteCredential(selectedSSID_);
          }
          startWebServer(core);
          return;
        }

        if (core.pendingSync == SyncMode::CalibreWireless) {
          if (!WIFI_STORE.hasSavedCredential(selectedSSID_) && passwordJustEntered_) {
            if (WIFI_STORE.addCredential(selectedSSID_, keyboardView_.input))
              WIFI_STORE.promoteCredential(selectedSSID_);
          }
          memset(keyboardView_.input, 0, sizeof(keyboardView_.input));
          keyboardView_.inputLen = 0;
          goCalibreSync_ = true;
          return;
        }

        if (core.pendingSync == SyncMode::NtpSync) {
          if (!WIFI_STORE.hasSavedCredential(selectedSSID_) && passwordJustEntered_) {
            if (WIFI_STORE.addCredential(selectedSSID_, keyboardView_.input))
              WIFI_STORE.promoteCredential(selectedSSID_);
          }
          memset(keyboardView_.input, 0, sizeof(keyboardView_.input));
          keyboardView_.inputLen = 0;
          goApp_ = true;
          return;
        }

        if (core.pendingSync == SyncMode::PrinterSetup) {
          if (!WIFI_STORE.hasSavedCredential(selectedSSID_) && passwordJustEntered_) {
            if (WIFI_STORE.addCredential(selectedSSID_, keyboardView_.input))
              WIFI_STORE.promoteCredential(selectedSSID_);
          }
          memset(keyboardView_.input, 0, sizeof(keyboardView_.input));
          keyboardView_.inputLen = 0;
          goApp_ = true;
          return;
        }

        if (core.pendingSync == SyncMode::LocalsendSetup) {
          if (!WIFI_STORE.hasSavedCredential(selectedSSID_) && passwordJustEntered_) {
            if (WIFI_STORE.addCredential(selectedSSID_, keyboardView_.input))
              WIFI_STORE.promoteCredential(selectedSSID_);
          }
          memset(keyboardView_.input, 0, sizeof(keyboardView_.input));
          keyboardView_.inputLen = 0;
          goApp_ = true;
          return;
        }

        if (!WIFI_STORE.hasSavedCredential(selectedSSID_) && passwordJustEntered_) {
          confirmView_.setTitle(tr(SAVE_PASSWORD_Q));
          confirmView_.setMessage(tr(SAVE_PASSWORD_MSG));
          confirmView_.selectYes();
          confirmView_.needsRender = true;
          currentScreen_ = NetworkScreen::SavePrompt;
          needsRender_ = true;
        } else {
          startWebServer(core);
        }
      } else if (connectingView_.status == ui::WifiConnectingView::Status::Failed) {
        if (connectingFromSaved_) {
          currentScreen_ = NetworkScreen::SavedActions;
          actionsMenu_.selected = 1;
          actionsMenu_.needsRender = true;
        } else {
          keyboardView_.clear();
          keyboardView_.needsRender = true;
          currentScreen_ = NetworkScreen::PasswordEntry;
        }
        needsRender_ = true;
      }
    }
  }
}

void NetworkState::handleSavePrompt(Core& core, Button button) {
  switch (button) {
    case Button::Left:
      confirmView_.selectYes();
      needsRender_ = true;
      break;

    case Button::Right:
      confirmView_.selectNo();
      needsRender_ = true;
      break;

    case Button::Center:
      if (confirmView_.isYesSelected()) {
        if (WIFI_STORE.addCredential(selectedSSID_, keyboardView_.input)) WIFI_STORE.promoteCredential(selectedSSID_);
      }
      if (core.pendingSync == SyncMode::NtpSync) {
        goApp_ = true;
      } else if (core.pendingSync == SyncMode::PrinterSetup) {
        goApp_ = true;
      } else if (core.pendingSync == SyncMode::LocalsendSetup) {
        goApp_ = true;
      } else {
        startWebServer(core);
      }
      break;

    case Button::Back:
      if (core.pendingSync == SyncMode::NtpSync) {
        goApp_ = true;
      } else if (core.pendingSync == SyncMode::PrinterSetup) {
        goApp_ = true;
      } else if (core.pendingSync == SyncMode::LocalsendSetup) {
        goApp_ = true;
      } else {
        startWebServer(core);
      }
      break;

    default:
      break;
  }
}

void NetworkState::handleServerRunning(Core& core, Button button) {
  if (button == Button::Back) {
    stopWebServer(core);
    goBack_ = true;
  }
}

void NetworkState::startWifiScan(Core& core) {
  LOG_INF(TAG, "Starting WiFi scan");
  connectingFromSaved_ = false;

  scanRetryCount_ = 0;
  scanRetryAt_ = 0;
  wifiListView_.clear();
  wifiListView_.setScanning(true, tr(SCANNING));

  auto result = core.wifi.startScan();
  if (!result.ok()) {
    LOG_ERR(TAG, "Failed to start scan");
    wifiListView_.setScanning(false);
  }
}

void NetworkState::connectToNetwork(Core& core, const char* ssid, const char* password) {
  LOG_INF(TAG, "Connecting to: %s", ssid);

  connectingView_.setSsid(ssid);
  connectingView_.setConnecting();
  currentScreen_ = NetworkScreen::Connecting;
  needsRender_ = true;

  // Render the connecting screen before blocking connect
  ui::render(renderer_, THEME, connectingView_);

  auto result = core.wifi.connect(ssid, password, 15000, nullptr);

  if (result.ok()) {
    char ip[46];  // INET6_ADDRSTRLEN = 46 for IPv6 addresses
    core.wifi.getIpAddress(ip, sizeof(ip));
    connectingView_.setConnected(ip);
    LOG_INF(TAG, "Connected, IP: %s", ip);
    if (WIFI_STORE.hasSavedCredential(selectedSSID_)) {
      if (!WIFI_STORE.promoteCredential(selectedSSID_)) LOG_ERR(TAG, "Could not save connected network priority");
      if (connectingFromSaved_) savedMenu_.selected = 0;
    }
  } else {
    connectingView_.setFailed(tr(CONNECTION_FAILED));
    LOG_ERR(TAG, "Connection failed");
  }

  needsRender_ = true;
}

void NetworkState::tryAutoConnect(Core& core) {
  LOG_INF(TAG, "Auto-connect: trying %d saved credentials", WIFI_STORE.getCount());
  passwordJustEntered_ = false;

  const auto* creds = WIFI_STORE.getCredentials();
  int count = WIFI_STORE.getCount();

  for (int i = 0; i < count; i++) {
    LOG_INF(TAG, "Auto-connect: trying %s", creds[i].ssid);

    connectingView_.setSsid(creds[i].ssid);
    connectingView_.setConnecting();
    currentScreen_ = NetworkScreen::Connecting;

    ui::render(renderer_, THEME, connectingView_);

    auto result = core.wifi.connect(creds[i].ssid, creds[i].password, 15000, nullptr);

    if (result.ok()) {
      char ip[46];
      core.wifi.getIpAddress(ip, sizeof(ip));
      connectingView_.setConnected(ip);
      LOG_INF(TAG, "Auto-connected to %s, IP: %s", creds[i].ssid, ip);

      strncpy(selectedSSID_, creds[i].ssid, sizeof(selectedSSID_) - 1);
      selectedSSID_[sizeof(selectedSSID_) - 1] = '\0';
      if (!WIFI_STORE.promoteCredential(selectedSSID_)) LOG_ERR(TAG, "Could not save connected network priority");

      // Route to the correct next screen based on why NetworkState was entered
      if (core.pendingSync == SyncMode::CalibreWireless) {
        goCalibreSync_ = true;
      } else if (core.pendingSync == SyncMode::NtpSync) {
        goApp_ = true;
      } else if (core.pendingSync == SyncMode::PrinterSetup) {
        goApp_ = true;
      } else if (core.pendingSync == SyncMode::LocalsendSetup) {
        goApp_ = true;
      } else {
        startWebServer(core);
      }
      return;
    }

    LOG_INF(TAG, "Auto-connect failed: %s", creds[i].ssid);
    core.wifi.shutdown();
  }

  LOG_INF(TAG, "Auto-connect: all credentials failed, falling back to ModeSelect");
  currentScreen_ = NetworkScreen::ModeSelect;
  modeView_.selected = modeView_.itemCount - 2;
  modeView_.needsRender = true;
  needsRender_ = true;
}

void NetworkState::startHotspot(Core& core) {
  LOG_INF(TAG, "Starting hotspot");

  // Show connecting message
  connectingView_.setSsid(AP_SSID);
  connectingView_.setConnecting();
  currentScreen_ = NetworkScreen::Connecting;
  needsRender_ = true;

  // Render before blocking operation
  ui::render(renderer_, THEME, connectingView_);

  auto result = core.wifi.startAP(AP_SSID);

  if (result.ok()) {
    char ip[16];
    core.wifi.getAPIP(ip, sizeof(ip));
    connectingView_.setConnected(ip);
    LOG_INF(TAG, "AP started, IP: %s", ip);

    delay(500);
    if (core.pendingSync == SyncMode::PrinterSetup) {
      // The printer app serves on the AP itself.
      goApp_ = true;
      return;
    }
    if (core.pendingSync == SyncMode::LocalsendSetup) {
      // The LocalSend app serves on the AP itself.
      goApp_ = true;
      return;
    }
    startWebServer(core);
  } else {
    connectingView_.setFailed(tr(HOTSPOT_FAILED));
    LOG_ERR(TAG, "Failed to start AP");
    needsRender_ = true;
  }
}

void NetworkState::startWebServer(Core& core) {
  LOG_INF(TAG, "Starting web server");

  // Allow ARP/DHCP to settle before binding server socket (STA mode)
  if (!core.wifi.isAPMode()) {
    delay(300);
  }

  if (!server_) {
    server_.reset(new PapyrixWebServer());
    if (!server_) {
      LOG_ERR(TAG, "Failed to allocate web server");
      goBack_ = true;
      return;
    }
  }

  server_->begin();

  // Set up server view
  char ip[16];
  bool isApMode = core.wifi.isAPMode();
  if (isApMode) {
    core.wifi.getAPIP(ip, sizeof(ip));
    serverView_.setServerInfo(AP_SSID, ip, true);
  } else {
    core.wifi.getIpAddress(ip, sizeof(ip));
    serverView_.setServerInfo(selectedSSID_, ip, false);
  }

  currentScreen_ = NetworkScreen::ServerRunning;
  needsRender_ = true;
}

void NetworkState::stopWebServer(Core& /* core */) {
  if (server_) {
    LOG_INF(TAG, "Stopping web server");
    server_->stop();
    server_.reset();
  }

  serverView_.setStopped();
}

}  // namespace papyrix
