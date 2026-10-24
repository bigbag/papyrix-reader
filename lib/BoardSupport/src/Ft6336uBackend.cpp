#include <TargetConfig.h>

#if PAPYRIX_TARGET_MURPHY_M4
#include <Arduino.h>
#include <ScopedMutex.h>
#include <TouchTransform.h>
#include <esp_heap_caps.h>

#include "Ft6336uBackend.h"
#include "MurphyM4I2c.h"

namespace papyrix::board {
namespace {
constexpr uint8_t kRegisters[][2] = {{0x00, 0x00}, {0x80, 0x16}, {0x88, 0x04}};
constexpr uint32_t kStackBytes = 4096;
using murphy_m4_i2c::Device;
}  // namespace

bool decodeFt6336uFrame(const uint8_t* bytes, size_t length, RawTouchSample& out) {
  out = {};
  if (!bytes || length != 11 || bytes[0] > 2) return false;
  const uint8_t contacts = bytes[0];
  if (contacts) {
    if (bytes[1] >> 6 == 3) return false;
    if (contacts != 1 || bytes[1] >> 6 != 1) {
      const uint16_t x = static_cast<uint16_t>((bytes[1] & 0x0F) << 8) | bytes[2];
      const uint16_t y = static_cast<uint16_t>((bytes[3] & 0x0F) << 8) | bytes[4];
      if (x > 479 || y > 799) return false;
      out.rawX = x;
      out.rawY = y;
    }
  }
  out.fresh = true;
  out.controllerOk = true;
  out.contactCount = contacts == 1 && bytes[1] >> 6 == 1 ? 0 : contacts;
  return true;
}

Ft6336uBackend& ft6336uBackend() {
  static Ft6336uBackend owner;
  return owner;
}

bool Ft6336uBackend::configure(RawTouchSample& probe) {
  if (!murphy_m4_i2c::begin()) return false;
  for (const auto& setting : kRegisters)
    if (!murphy_m4_i2c::write(Device::Touch, setting, sizeof(setting))) return false;
  for (const auto& setting : kRegisters) {
    uint8_t value = 0;
    if (!murphy_m4_i2c::read(Device::Touch, setting[0], &value, 1) || value != setting[1]) return false;
  }
  uint8_t frame[11];
  if (!murphy_m4_i2c::read(Device::Touch, 0x02, frame, sizeof(frame)) ||
      !decodeFt6336uFrame(frame, sizeof(frame), probe))
    return false;
  probe.sampledAtMs = millis();
  probe.timestamped = true;
  return true;
}

void Ft6336uBackend::clearContacts(const RawTouchSample& probe) {
  active_ = cancelled_ = frozen_ = false;
  replayCount_ = replayIndex_ = 0;
  version_ = replayedVersion_ = 0;
  latest_ = probe;
  knownIdle_ = probe.controllerOk && probe.contactCount == 0;
  blocked_ = !knownIdle_;
  idlePending_ = knownIdle_;
  idle_ = probe;
  lastGoodAtMs_ = probe.sampledAtMs;
  suppressionRevision_ = touchSuppressionRevision();
  invalidationRevision_ = touchInvalidationRevision();
  orientationRevision_ = displayOrientationRevision();
}

bool Ft6336uBackend::synchronizeRevisions() {
  const auto suppression = touchSuppressionRevision();
  const auto orientation = displayOrientationRevision();
  const auto invalidation = touchInvalidationRevision();
  if (suppression == suppressionRevision_ && orientation == orientationRevision_ &&
      invalidation == invalidationRevision_)
    return false;
  if (orientation == orientationRevision_ && invalidation == invalidationRevision_ && frozen_) {
    suppressionRevision_ = suppression;
    active_ = cancelled_ = false;
    blocked_ = !knownIdle_;
    if (blocked_) latest_.contactCount = 2;
    replayIndex_ = 0;
    idlePending_ = true;
    return true;
  }
  const auto probe = knownIdle_ ? latest_ : RawTouchSample{};
  clearContacts(probe);
  return true;
}

bool Ft6336uBackend::start(const RawTouchSample& probe) {
  {
    const ScopedMutex lock(mutex_);
    if (!lock) return false;
    clearContacts(probe);
  }
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < kStackBytes) return false;
  if (!worker_.start("m4-touch", kStackBytes, [this] { acquire(); }, 2)) return false;
  available_.store(true, std::memory_order_release);
  return true;
}

bool Ft6336uBackend::begin(const BoardProfile& profile) {
  if (!shutdown() || profile.touch.controller != TouchController::Ft6336u) return false;
  if (!mutex_) mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;
  profile_ = &profile;
  RawTouchSample probe;
  return configure(probe) && start(probe);
}

bool Ft6336uBackend::pauseForDisplayReset() {
  available_.store(false, std::memory_order_release);
  if (!worker_.stop(1000)) return false;
  if (!profile_) return true;
  discardPendingTouch();
  const ScopedMutex lock(mutex_);
  if (!lock) return false;
  clearContacts({});
  return true;
}

bool Ft6336uBackend::resumeAfterDisplayReset() {
  if (!profile_) return true;
  RawTouchSample probe;
  return configure(probe) && start(probe);
}

bool Ft6336uBackend::shutdown() {
  if (!pauseForDisplayReset()) return false;
  profile_ = nullptr;
  return true;
}

void Ft6336uBackend::buildReplay(const RawTouchSample* release) {
  replayIndex_ = 0;
  replayCount_ = 0;
  replay_[replayCount_++] = down_;
  for (const auto& point : extrema_) replay_[replayCount_++] = point;
  replay_[replayCount_++] = lastHeld_;
  if (cancelled_) replay_[replayCount_++] = cancellation_;
  if (release) replay_[replayCount_++] = *release;
  for (uint8_t i = 1; i < replayCount_; ++i) {
    const auto point = replay_[i];
    uint8_t j = i;
    while (j && point.sampledAtMs - down_.sampledAtMs < replay_[j - 1].sampledAtMs - down_.sampledAtMs) {
      replay_[j] = replay_[j - 1];
      --j;
    }
    replay_[j] = point;
  }
  replayedVersion_ = version_;
}

void Ft6336uBackend::record(RawTouchSample sample, bool received, bool decoded) {
  if (active_ && !cancelled_ &&
      (sample.sampledAtMs - lastGoodAtMs_ >= TapClassifier::STALE_CONTACT_MS ||
       (received && (!decoded || sample.contactCount > 1)))) {
    cancelled_ = true;
    blocked_ = true;
    cancellation_ = sample;
    cancellation_.fresh = true;
    cancellation_.contactCount = 2;
    ++version_;
  }
  if (!received) {
    latest_.fresh = false;
    latest_.controllerOk = false;
    latest_.sampledAtMs = sample.sampledAtMs;
    if (!active_ || sample.sampledAtMs - lastGoodAtMs_ < TapClassifier::STALE_CONTACT_MS) return;
  } else if (decoded) {
    lastGoodAtMs_ = sample.sampledAtMs;
    latest_ = sample;
    if (sample.contactCount == 0) {
      if (active_ && !frozen_) {
        buildReplay(&sample);
        frozen_ = true;
      } else if (!active_ && !frozen_) {
        idle_ = sample;
        idlePending_ = true;
      }
      active_ = false;
      blocked_ = false;
      knownIdle_ = true;
      return;
    }
    knownIdle_ = false;
    if (sample.contactCount == 1 && !blocked_) {
      if (!active_) {
        down_ = sample;
        for (auto& point : extrema_) point = sample;
        active_ = true;
        cancelled_ = false;
      } else {
        if (sample.rawX < extrema_[0].rawX) extrema_[0] = sample;
        if (sample.rawX > extrema_[1].rawX) extrema_[1] = sample;
        if (sample.rawY < extrema_[2].rawY) extrema_[2] = sample;
        if (sample.rawY > extrema_[3].rawY) extrema_[3] = sample;
      }
      lastHeld_ = sample;
      ++version_;
      return;
    }
    if (sample.contactCount == 1) {
      latest_.contactCount = 2;
      return;
    }
  }
  sample.fresh = true;
  sample.contactCount = 2;
  latest_ = sample;
  knownIdle_ = false;
  blocked_ = true;
}

void Ft6336uBackend::acquire() {
  auto previous = xTaskGetTickCount();
  while (!worker_.shouldStop()) {
    uint32_t suppression;
    uint8_t orientation;
    {
      const ScopedMutex lock(mutex_);
      if (!lock) return;
      synchronizeRevisions();
      suppression = suppressionRevision_;
      orientation = orientationRevision_;
    }
    uint8_t bytes[11];
    RawTouchSample sample;
    const bool received = murphy_m4_i2c::read(Device::Touch, 0x02, bytes, sizeof(bytes));
    if (worker_.shouldStop()) return;
    const bool decoded = received && decodeFt6336uFrame(bytes, sizeof(bytes), sample);
    sample.sampledAtMs = millis();
    sample.timestamped = true;
    {
      const ScopedMutex lock(mutex_);
      if (!lock) return;
      if (!synchronizeRevisions() && suppression == suppressionRevision_ && orientation == orientationRevision_)
        record(sample, received, decoded);
    }
    vTaskDelayUntil(&previous, pdMS_TO_TICKS(10));
  }
}

RawTouchSample Ft6336uBackend::poll() {
  if (!available()) return {};
  const ScopedMutex lock(mutex_);
  if (!lock) return {};
  synchronizeRevisions();
  if (idlePending_) {
    idlePending_ = false;
    idle_.fresh = true;
    return idle_;
  }
  if (replayIndex_ == replayCount_ && !frozen_ && active_ && version_ != replayedVersion_) buildReplay(nullptr);
  if (replayIndex_ < replayCount_) {
    const auto sample = replay_[replayIndex_++];
    if (replayIndex_ == replayCount_) frozen_ = false;
    return sample;
  }
  const auto sample = latest_;
  latest_.fresh = false;
  return sample;
}

}  // namespace papyrix::board
#endif
