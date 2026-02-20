#include "test_utils.h"

#include <cstdint>

// We need to provide the dependencies for EventQueue.h
// Create minimal stubs for Result.h and Types.h dependencies

namespace papyrix {

enum class Button : uint8_t {
  Up,
  Down,
  Left,
  Right,
  Center,
  Back,
  Power,
};

enum class Error : uint8_t {
  None = 0,
  SdCardNotFound,
  FileNotFound,
  FileCorrupted,
};

}  // namespace papyrix

// Now include EventQueue (we define Result.h and Types.h deps inline)
#define RESULT_H_INCLUDED  // Skip the real header
#define TYPES_H_INCLUDED

// Include the EventQueue implementation
namespace papyrix {

enum class EventType : uint8_t {
  None = 0,
  ButtonPress,
  ButtonLongPress,
  ButtonRepeat,
  ButtonRelease,
  BatteryLow,
  UsbConnected,
  UsbDisconnected,
  SleepTimeout,
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

using namespace papyrix;

int main() {
  TestUtils::TestRunner runner("EventQueue");

  // ============================================
  // Basic push/pop tests
  // ============================================

  // Test 1: Empty queue check
  {
    EventQueue queue;
    runner.expectTrue(queue.empty(), "Initial queue is empty");
    runner.expectEq(static_cast<size_t>(0), queue.size(), "Initial queue size is 0");
  }

  // Test 2: Single push/pop
  {
    EventQueue queue;
    Event e = Event::buttonPress(Button::Up);
    bool pushResult = queue.push(e);
    runner.expectTrue(pushResult, "push() returns true on success");
    runner.expectFalse(queue.empty(), "Queue not empty after push");
    runner.expectEq(static_cast<size_t>(1), queue.size(), "Queue size is 1 after push");

    Event out;
    bool popResult = queue.pop(out);
    runner.expectTrue(popResult, "pop() returns true on success");
    runner.expectTrue(out.type == EventType::ButtonPress, "pop() returns correct event type");
    runner.expectTrue(out.button == Button::Up, "pop() returns correct button");
    runner.expectTrue(queue.empty(), "Queue empty after pop");
  }

  // Test 3: Multiple push/pop
  {
    EventQueue queue;
    queue.push(Event::buttonPress(Button::Up));
    queue.push(Event::buttonPress(Button::Down));
    queue.push(Event::buttonPress(Button::Left));

    runner.expectEq(static_cast<size_t>(3), queue.size(), "Size is 3 after 3 pushes");

    Event out;
    queue.pop(out);
    runner.expectTrue(out.button == Button::Up, "First pop is Up");
    queue.pop(out);
    runner.expectTrue(out.button == Button::Down, "Second pop is Down");
    queue.pop(out);
    runner.expectTrue(out.button == Button::Left, "Third pop is Left");
    runner.expectTrue(queue.empty(), "Queue empty after all pops");
  }

  // Test 4: Pop from empty queue
  {
    EventQueue queue;
    Event out;
    bool popResult = queue.pop(out);
    runner.expectFalse(popResult, "pop() returns false on empty queue");
  }

  // ============================================
  // Capacity and overflow tests
  // ============================================

  // Test 5: Fill to capacity
  {
    EventQueue queue;
    // Capacity is 16, but circular buffer uses 1 slot for empty/full detection
    // So max items = CAPACITY - 1 = 15
    for (int i = 0; i < 15; i++) {
      bool result = queue.push(Event::buttonPress(Button::Up));
      runner.expectTrue(result, "push() succeeds for item " + std::to_string(i));
    }
    runner.expectEq(static_cast<size_t>(15), queue.size(), "Size is 15 at max capacity");
  }

  // Test 6: Push when full returns false
  {
    EventQueue queue;
    for (int i = 0; i < 15; i++) {
      queue.push(Event::buttonPress(Button::Up));
    }
    bool overflowPush = queue.push(Event::buttonPress(Button::Down));
    runner.expectFalse(overflowPush, "push() returns false when full");
    runner.expectEq(static_cast<size_t>(15), queue.size(), "Size unchanged after failed push");
  }

  // ============================================
  // Wraparound tests
  // ============================================

  // Test 7: Wraparound behavior
  {
    EventQueue queue;
    // Push and pop to move head/tail pointers
    for (int i = 0; i < 10; i++) {
      queue.push(Event::buttonPress(Button::Up));
    }
    Event out;
    for (int i = 0; i < 10; i++) {
      queue.pop(out);
    }
    runner.expectTrue(queue.empty(), "Empty after push/pop cycle");

    // Now push again - should wraparound
    for (int i = 0; i < 15; i++) {
      bool result = queue.push(Event::buttonPress(static_cast<Button>(i % 7)));
      runner.expectTrue(result, "Wraparound push " + std::to_string(i) + " succeeds");
    }
    runner.expectEq(static_cast<size_t>(15), queue.size(), "Size correct after wraparound fill");

    // Pop all and verify FIFO order
    for (int i = 0; i < 15; i++) {
      queue.pop(out);
      runner.expectTrue(out.button == static_cast<Button>(i % 7),
                        "Wraparound FIFO order correct for item " + std::to_string(i));
    }
  }

  // Test 8: Size calculation with head > tail
  {
    EventQueue queue;
    // Push 5 items
    for (int i = 0; i < 5; i++) {
      queue.push(Event::buttonPress(Button::Up));
    }
    runner.expectEq(static_cast<size_t>(5), queue.size(), "Size with head > tail");
  }

  // Test 9: Size calculation with head < tail (after wraparound)
  {
    EventQueue queue;
    // Push to near end
    for (int i = 0; i < 14; i++) {
      queue.push(Event::buttonPress(Button::Up));
    }
    // Pop some to move tail forward
    Event out;
    for (int i = 0; i < 10; i++) {
      queue.pop(out);
    }
    // Now tail > head conceptually. Push more to wrap head around
    for (int i = 0; i < 8; i++) {
      queue.push(Event::buttonPress(Button::Down));
    }
    // Size should be 4 (remaining) + 8 (new) = 12
    runner.expectEq(static_cast<size_t>(12), queue.size(), "Size with wraparound (head < tail in array)");
  }

  // ============================================
  // Clear tests
  // ============================================

  // Test 10: Clear empty queue
  {
    EventQueue queue;
    queue.clear();
    runner.expectTrue(queue.empty(), "clear() on empty queue keeps it empty");
    runner.expectEq(static_cast<size_t>(0), queue.size(), "Size is 0 after clear on empty");
  }

  // Test 11: Clear non-empty queue
  {
    EventQueue queue;
    for (int i = 0; i < 10; i++) {
      queue.push(Event::buttonPress(Button::Up));
    }
    queue.clear();
    runner.expectTrue(queue.empty(), "clear() makes queue empty");
    runner.expectEq(static_cast<size_t>(0), queue.size(), "Size is 0 after clear");
  }

  // Test 12: Push after clear
  {
    EventQueue queue;
    for (int i = 0; i < 10; i++) {
      queue.push(Event::buttonPress(Button::Up));
    }
    queue.clear();

    bool result = queue.push(Event::buttonPress(Button::Down));
    runner.expectTrue(result, "push() works after clear");
    runner.expectEq(static_cast<size_t>(1), queue.size(), "Size is 1 after push following clear");

    Event out;
    queue.pop(out);
    runner.expectTrue(out.button == Button::Down, "Correct event after clear and push");
  }

  // ============================================
  // Event type tests
  // ============================================

  // Test 13: Different event types
  {
    EventQueue queue;
    queue.push(Event::buttonPress(Button::Center));
    queue.push(Event::buttonLongPress(Button::Power));
    queue.push(Event::buttonRelease(Button::Back));
    queue.push(Event::system(EventType::BatteryLow));
    queue.push(Event::contentError(Error::FileNotFound));

    Event out;

    queue.pop(out);
    runner.expectTrue(out.type == EventType::ButtonPress, "Event type: ButtonPress");

    queue.pop(out);
    runner.expectTrue(out.type == EventType::ButtonLongPress, "Event type: ButtonLongPress");

    queue.pop(out);
    runner.expectTrue(out.type == EventType::ButtonRelease, "Event type: ButtonRelease");

    queue.pop(out);
    runner.expectTrue(out.type == EventType::BatteryLow, "Event type: BatteryLow");

    queue.pop(out);
    runner.expectTrue(out.type == EventType::ContentError, "Event type: ContentError");
    runner.expectTrue(out.error == Error::FileNotFound, "Error value preserved");
  }

  // Test 14: Event::none()
  {
    Event e = Event::none();
    runner.expectTrue(e.type == EventType::None, "Event::none() has None type");
  }

  // Test 15: Event::buttonRepeat()
  {
    EventQueue queue;
    queue.push(Event::buttonRepeat(Button::Down));

    Event out;
    queue.pop(out);
    runner.expectTrue(out.type == EventType::ButtonRepeat, "Event type: ButtonRepeat");
    runner.expectTrue(out.button == Button::Down, "ButtonRepeat preserves button");
  }

  return runner.allPassed() ? 0 : 1;
}
