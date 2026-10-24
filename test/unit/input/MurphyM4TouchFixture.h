#pragma once

#include <Arduino.h>
#include <TouchBackend.h>
#include <driver/i2c_master.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <condition_variable>
#include <mutex>

class MurphyM4TouchFixture {
 public:
  using Frame = std::array<uint8_t, 11>;
  explicit MurphyM4TouchFixture(bool failButtons = true) {
    assert(instance_ == nullptr);
    instance_ = this;
    testSetManualMillis(0);
    if (failButtons) testTaskCreateFailureName = "Buttons";
    testSetVTaskDelayHook(delayTask);
    setFrame({});
  }
  ~MurphyM4TouchFixture() {
    testEventWaitFailure = false;
    releaseForStop();
    assert(papyrix::board::stopTouchAcquisition());
    cleanupMockTasks();
    testSetVTaskDelayHook(nullptr);
    testTaskCreateFailureName.clear();
    test_native_i2c::readHook = nullptr;
    instance_ = nullptr;
    testUseRealtimeMillis();
  }
  static Frame frame(uint8_t contacts, uint16_t x = 100, uint16_t y = 200, uint8_t event = 2) {
    Frame bytes{};
    bytes[0] = contacts;
    if (contacts) {
      bytes[1] = static_cast<uint8_t>((event << 6) | (x >> 8));
      bytes[2] = static_cast<uint8_t>(x);
      bytes[3] = static_cast<uint8_t>(y >> 8);
      bytes[4] = static_cast<uint8_t>(y);
    }
    return bytes;
  }
  static void setFrame(const Frame& frame) {
    std::copy(frame.begin(), frame.end(), test_native_i2c::registers[0x2E].begin() + 2);
  }
  void waitPaused() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return paused_; });
  }
  void step(uint32_t now, const Frame& frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return paused_; });
    testSetManualMillis(now);
    setFrame(frame);
    const auto next = iterations_ + 1;
    ++permits_;
    cv_.notify_all();
    cv_.wait(lock, [&] { return iterations_ >= next && paused_; });
  }
  void releaseForStop() {
    std::lock_guard<std::mutex> lock(mutex_);
    freeRunning_ = true;
    cv_.notify_all();
  }
  void stopComplete() {
    std::lock_guard<std::mutex> lock(mutex_);
    freeRunning_ = false;
    paused_ = false;
    permits_ = 0;
  }

 private:
  static void delayTask(TickType_t) {
    if (!currentMockTask || currentMockTask->name != "m4-touch") {
      std::this_thread::yield();
      return;
    }
    auto& fixture = *instance_;
    std::unique_lock<std::mutex> lock(fixture.mutex_);
    fixture.paused_ = true;
    ++fixture.iterations_;
    fixture.cv_.notify_all();
    fixture.cv_.wait(lock, [&] { return fixture.freeRunning_ || fixture.permits_ > 0; });
    fixture.paused_ = false;
    if (fixture.permits_) --fixture.permits_;
  }
  inline static MurphyM4TouchFixture* instance_ = nullptr;
  std::mutex mutex_;
  std::condition_variable cv_;
  unsigned iterations_ = 0, permits_ = 0;
  bool paused_ = false, freeRunning_ = false;
};
