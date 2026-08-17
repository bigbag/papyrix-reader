#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#ifdef TEST_BUILD
#include <functional>
#endif

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
  void unthrottleLocked();

  std::mutex mutex_;
  std::atomic<bool> throttled_{false};
  std::atomic<uint32_t> performanceLockCount_{0};

#ifdef TEST_BUILD
 public:
  // Test seam: invoked between the throttle flag transition and the frequency
  // write so regression tests can interleave a performance lock acquisition
  // exactly where the main loop used to race the background cache task.
  std::function<void()> throttleCasHook;
#endif
};

}  // namespace drivers
}  // namespace papyrix
