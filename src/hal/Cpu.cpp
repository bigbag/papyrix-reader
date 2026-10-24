#include "Cpu.h"

#include <Arduino.h>
#include <TargetConfig.h>

#include <cassert>

namespace papyrix::hal {

static constexpr uint8_t kIdleFreqMhz = 10;
static constexpr uint32_t kActiveFreqMhz = F_CPU / 1000000;
static constexpr uint8_t kIdleLoopDelayMs = 50;
static constexpr uint8_t kActiveLoopDelayMs = 10;

Cpu::PerformanceLock::PerformanceLock(Cpu& cpu) : cpu_(cpu) { cpu_.acquirePerformanceLock(); }

Cpu::PerformanceLock::~PerformanceLock() { cpu_.releasePerformanceLock(); }

void Cpu::acquirePerformanceLock() {
  if constexpr (PAPYRIX_TARGET_MURPHY_M4) return;
  performanceLockCount_.fetch_add(1, std::memory_order_acq_rel);
  unthrottle();
}

void Cpu::releasePerformanceLock() {
  if constexpr (PAPYRIX_TARGET_MURPHY_M4) return;
  const uint32_t previous = performanceLockCount_.fetch_sub(1, std::memory_order_acq_rel);
  assert(previous > 0);
}

void Cpu::throttle() {
  if constexpr (PAPYRIX_TARGET_MURPHY_M4) return;
  std::lock_guard<std::mutex> lock(mutex_);
  if (performanceLockCount_.load(std::memory_order_acquire) != 0) {
    unthrottleLocked();
    return;
  }

  bool expected = false;
  if (throttled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
#ifdef TEST_BUILD
    if (throttleCasHook) throttleCasHook();
#endif
    if (!setCpuFrequencyMhz(kIdleFreqMhz)) throttled_.store(false, std::memory_order_release);
  }

  if (performanceLockCount_.load(std::memory_order_acquire) != 0) unthrottleLocked();
}

void Cpu::unthrottle() {
  if constexpr (PAPYRIX_TARGET_MURPHY_M4) return;
  std::lock_guard<std::mutex> lock(mutex_);
  unthrottleLocked();
}

void Cpu::unthrottleLocked() {
  if (throttled_.load(std::memory_order_acquire) && setCpuFrequencyMhz(kActiveFreqMhz)) {
    throttled_.store(false, std::memory_order_release);
  }
}

bool Cpu::isThrottled() const { return !PAPYRIX_TARGET_MURPHY_M4 && throttled_.load(std::memory_order_acquire); }

uint8_t Cpu::loopDelayMs() const { return isThrottled() ? kIdleLoopDelayMs : kActiveLoopDelayMs; }

}  // namespace papyrix::hal
