#include "Cpu.h"

#include <Arduino.h>

#include <cassert>

namespace papyrix {
namespace drivers {

static constexpr uint8_t kIdleFreqMhz = 10;
static constexpr uint8_t kActiveFreqMhz = 160;
static constexpr uint8_t kIdleLoopDelayMs = 50;
static constexpr uint8_t kActiveLoopDelayMs = 10;

Cpu::PerformanceLock::PerformanceLock(Cpu& cpu) : cpu_(cpu) { cpu_.acquirePerformanceLock(); }

Cpu::PerformanceLock::~PerformanceLock() { cpu_.releasePerformanceLock(); }

void Cpu::acquirePerformanceLock() {
  performanceLockCount_.fetch_add(1, std::memory_order_acq_rel);
  unthrottle();
}

void Cpu::releasePerformanceLock() {
  const uint32_t previous = performanceLockCount_.fetch_sub(1, std::memory_order_acq_rel);
  assert(previous > 0);
}

void Cpu::throttle() {
  if (performanceLockCount_.load(std::memory_order_acquire) != 0) {
    unthrottle();
    return;
  }

  bool expected = false;
  if (throttled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    setCpuFrequencyMhz(kIdleFreqMhz);
  }

  if (performanceLockCount_.load(std::memory_order_acquire) != 0) unthrottle();
}

void Cpu::unthrottle() {
  if (throttled_.exchange(false, std::memory_order_acq_rel)) setCpuFrequencyMhz(kActiveFreqMhz);
}

bool Cpu::isThrottled() const { return throttled_.load(std::memory_order_acquire); }

uint8_t Cpu::loopDelayMs() const { return isThrottled() ? kIdleLoopDelayMs : kActiveLoopDelayMs; }

}  // namespace drivers
}  // namespace papyrix
