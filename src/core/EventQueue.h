#pragma once

#include <cstdint>

#include "Result.h"
#include "Types.h"

namespace papyrix {

enum class EventType : uint8_t {
  None = 0,

  // Input events
  ButtonPress,
  ButtonLongPress,
  ButtonRepeat,
  ButtonRelease,

  // System events
  BatteryLow,
  UsbConnected,
  UsbDisconnected,
  SleepTimeout,

  // Content events
  ContentLoaded,
  ContentError,
  PageReady,
};

struct Event {
  EventType type;
  union {
    Button button;
    Error error;
    uint8_t data;
  };

  static Event none() { return {EventType::None, {}}; }

  static Event buttonPress(Button btn) {
    Event e;
    e.type = EventType::ButtonPress;
    e.button = btn;
    return e;
  }

  static Event buttonLongPress(Button btn) {
    Event e;
    e.type = EventType::ButtonLongPress;
    e.button = btn;
    return e;
  }

  static Event buttonRepeat(Button btn) {
    Event e;
    e.type = EventType::ButtonRepeat;
    e.button = btn;
    return e;
  }

  static Event buttonRelease(Button btn) {
    Event e;
    e.type = EventType::ButtonRelease;
    e.button = btn;
    return e;
  }

  static Event system(EventType t) { return {t, {}}; }

  static Event contentError(Error err) {
    Event e;
    e.type = EventType::ContentError;
    e.error = err;
    return e;
  }
};

class EventQueue {
 public:
  static constexpr size_t CAPACITY = 16;

  bool push(Event e) {
    uint8_t nextHead = (head_ + 1) % CAPACITY;
    if (nextHead == tail_) {
      return false;  // Full
    }
    buffer_[head_] = e;
    head_ = nextHead;
    return true;
  }

  bool pop(Event& out) {
    if (tail_ == head_) {
      return false;  // Empty
    }
    out = buffer_[tail_];
    tail_ = (tail_ + 1) % CAPACITY;
    return true;
  }

  bool empty() const { return tail_ == head_; }

  size_t size() const {
    if (head_ >= tail_) {
      return head_ - tail_;
    }
    return CAPACITY - tail_ + head_;
  }

  void clear() { tail_ = head_ = 0; }

 private:
  Event buffer_[CAPACITY] = {};
  uint8_t head_ = 0;
  uint8_t tail_ = 0;
};

}  // namespace papyrix
