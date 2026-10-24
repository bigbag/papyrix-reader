#pragma once

#include <DisplayController.h>

#include <cstdint>

#include "BoardProfiles.h"
#include "BoardSelector.h"

namespace papyrix::board {

enum class PanelSelectionSource : uint8_t { Fixed, X4Default, Override, Cache, Probe, Fallback, Factory, Unresolved };
inline constexpr uint8_t kProfileSchemaVersion = 3;

class HardwareIdentity {
 public:
  static HardwareIdentity& instance();

  HardwareIdentity();

  bool applyBoardSelection(const BoardSelection& selection, const BoardProbeReport& report);
  void setPanel(eink::DisplayController panel, PanelSelectionSource source, uint8_t variant = 0);
  void clearPanelSelection();
  bool panelResolved() const { return panelResolved_; }

  BoardId board() const { return profile_->id; }
  const BoardProfile& profile() const { return *profile_; }
  const char* cacheDir() const { return profile_->cacheDir; }
  bool boardSelected() const { return boardSelected_; }
  BoardSelectionSource boardSource() const { return boardSource_; }
  const BoardProbeReport& lastProbe() const { return lastProbe_; }
  eink::DisplayController panel() const { return panel_; }
  PanelSelectionSource panelSource() const { return panelSource_; }
  uint8_t panelVariant() const { return panelVariant_; }
  uint8_t profileSchemaVersion() const { return kProfileSchemaVersion; }

 private:
  const BoardProfile* profile_;
  bool boardSelected_ = !PAPYRIX_TARGET_XTEINK_C3;
  BoardSelectionSource boardSource_;
  BoardProbeReport lastProbe_{};
  eink::DisplayController panel_ = kBootPanelController;
  PanelSelectionSource panelSource_ = (PAPYRIX_TARGET_X4CLASSIC || PAPYRIX_TARGET_STICKY)
                                          ? PanelSelectionSource::Unresolved
                                          : PanelSelectionSource::Fixed;
  uint8_t panelVariant_ = 0;
  bool panelResolved_ = !(PAPYRIX_TARGET_X4CLASSIC || PAPYRIX_TARGET_STICKY);
};

const char* panelSelectionSourceName(PanelSelectionSource source);

}  // namespace papyrix::board
