#pragma once

#include <stdint.h>

#include "State.h"

class GfxRenderer;
class Bitmap;

namespace papyrix {

struct Core;

// SleepState handles entering deep sleep mode
class SleepState : public State {
 public:
  explicit SleepState(GfxRenderer& renderer);

  void enter(Core& core) override;
  void exit(Core& core) override;
  StateTransition update(Core& core) override;
  StateId id() const override { return StateId::Sleep; }

 private:
  GfxRenderer& renderer_;

  void renderSleepProgress() const;
  void renderDefaultSleepScreen(uint8_t sleepMode) const;
  void renderCustomSleepScreen(const Core& core) const;
  void renderCoverSleepScreen(Core& core) const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  bool waitForPowerRelease(uint32_t timeoutMs = 0) const;
};

}  // namespace papyrix
