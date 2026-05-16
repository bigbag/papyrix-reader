#pragma once

#include <cstdint>

#include "../ui/views/SettingsViews.h"
#include "State.h"

class GfxRenderer;

namespace papyrix {

enum class SettingsScreen : uint8_t {
  Menu,
  Reader,
  Device,
  Cleanup,
  SystemInfo,
  ConfirmDialog,
  FirmwareUpdate,
};

class SettingsState : public State {
 public:
  explicit SettingsState(GfxRenderer& renderer);
  ~SettingsState() override;

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  void render(Core& core) override;
  StateId id() const override { return StateId::Settings; }

 private:
  GfxRenderer& renderer_;
  Core* core_;  // Stored for helper methods that don't receive Core&
  SettingsScreen currentScreen_;
  bool needsRender_;
  bool goHome_;
  bool goNetwork_;
  bool themeWasChanged_;
  SettingsScreen returnScreen_;  // Screen to return to after Network mode

  // Pending action for confirmation dialog
  // 0=none, 10=Clear Book Cache, 11=Clear Device Storage, 12=Factory Reset
  uint8_t pendingAction_;

  // Views (all small structs)
  ui::SettingsMenuView menuView_;
  ui::ReaderSettingsView readerView_;
  ui::DeviceSettingsView deviceView_;
  ui::CleanupMenuView cleanupView_;
  ui::SystemInfoView infoView_;
  ui::ConfirmDialogView confirmView_;
  ui::FirmwareUpdateView firmwareView_;
  uint32_t lastFirmwareRenderMs_ = 0;
  uint32_t firmwareCompleteMs_ = 0;
  bool firmwareValidationRendered_ = false;

  // Navigation helpers
  void openSelected();
  void goBack(Core& core);
  void handleConfirm(Core& core);
  void handleLeftRight(int delta);

  // Settings binding
  void loadReaderSettings();
  void saveReaderSettings();
  void loadDeviceSettings();
  void saveDeviceSettings();
  void populateSystemInfo();

  // Actions
  void clearCache(int type, Core& core);
};

}  // namespace papyrix
