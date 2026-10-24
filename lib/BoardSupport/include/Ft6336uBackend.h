#pragma once

#include <BackgroundTask.h>

#include <atomic>

#include "TouchBackend.h"

namespace papyrix::board {

bool decodeFt6336uFrame(const uint8_t* bytes, size_t length, RawTouchSample& out);

class Ft6336uBackend {
 public:
  bool begin(const BoardProfile& profile);
  RawTouchSample poll();
  bool pauseForDisplayReset();
  bool resumeAfterDisplayReset();
  bool shutdown();
  bool available() const { return available_.load(std::memory_order_acquire); }

 private:
  bool configure(RawTouchSample& probe);
  bool start(const RawTouchSample& probe);
  void acquire();
  bool synchronizeRevisions();
  void clearContacts(const RawTouchSample& probe);
  void record(RawTouchSample sample, bool received, bool decoded);
  void buildReplay(const RawTouchSample* release);

  BackgroundTask worker_;
  SemaphoreHandle_t mutex_ = nullptr;
  const BoardProfile* profile_ = nullptr;
  std::atomic<bool> available_{false};
  RawTouchSample down_{}, extrema_[4]{}, lastHeld_{}, cancellation_{}, idle_{}, latest_{};
  RawTouchSample replay_[8]{};
  uint8_t replayCount_ = 0, replayIndex_ = 0;
  uint32_t version_ = 0, replayedVersion_ = 0, suppressionRevision_ = 0, lastGoodAtMs_ = 0;
  uint32_t invalidationRevision_ = 0;
  uint8_t orientationRevision_ = 0;
  bool active_ = false, cancelled_ = false, blocked_ = true, frozen_ = false;
  bool knownIdle_ = false, idlePending_ = false;
};

Ft6336uBackend& ft6336uBackend();

}  // namespace papyrix::board
