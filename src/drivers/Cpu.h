#pragma once

#include <atomic>
#include <cstdint>

namespace papyrix {
namespace drivers {

class Cpu {
 public:
  class PerformanceLock {
   public:
    explicit PerformanceLock(Cpu& cpu);
    ~PerformanceLock();
    PerformanceLock(const PerformanceLock&) = delete;
    PerformanceLock& operator=(const PerformanceLock&) = delete;

   private:
    Cpu& cpu_;
  };

  void throttle();
  void unthrottle();
  bool isThrottled() const;
  uint8_t loopDelayMs() const;

 private:
  friend class PerformanceLock;
  void acquirePerformanceLock();
  void releasePerformanceLock();

  std::atomic<bool> throttled_{false};
  std::atomic<uint32_t> performanceLockCount_{0};
};

}  // namespace drivers
}  // namespace papyrix
