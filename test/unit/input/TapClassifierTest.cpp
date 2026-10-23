#include <Arduino.h>
#include <BoardProfiles.h>
#include <TouchBackend.h>
#include <Wire.h>

#include "core/EventQueue.h"
#include "test_utils.h"

using papyrix::Event;
using papyrix::EventQueue;
using papyrix::EventType;
using papyrix::board::RawTouchSample;
using papyrix::board::TapClassifier;
using papyrix::board::TapFrame;
using papyrix::board::TouchBackend;
using papyrix::board::TouchConfig;
using papyrix::board::TouchPosition;
using papyrix::board::TouchSample;

namespace {

TouchSample contact(int16_t x, int16_t y, uint8_t count = 1) { return {true, true, count, {x, y}}; }

TouchSample idle() { return {true, true, 0, {}}; }
TouchSample failure() { return {false, false, 0, {}}; }

bool noEvents(const TapFrame& frame) { return !frame.down && !frame.up && !frame.tap; }

}  // namespace

int main() {
  TestUtils::TestRunner runner("Tap classifier and touch events");
  Wire.reset();
  Wire.setPresent(0x5D, true);
  Wire.setWideAddress(0x5D);
  TouchBackend backend;
  runner.expectTrue(backend.begin(*papyrix::board::findProfile(papyrix::board::BoardId::X4Pro)),
                    "X4 Pro GT911 starts on the shared I2C bus");
  Wire.setInvalidState(true);
  runner.expectTrue(!backend.poll().controllerOk, "GT911 reports a failed bus read");
  Wire.setInvalidState(false);
  runner.expectTrue(backend.poll().controllerOk, "GT911 resumes polling after a transient failure");
  Wire.setRegister(0x5D, 0x814E, 0x81);
  Wire.setRegister(0x5D, 0x8150, 0xE0);
  Wire.setRegister(0x5D, 0x8151, 0x01);
  Wire.setRegister(0x5D, 0x8152, 0x20);
  Wire.setRegister(0x5D, 0x8153, 0x03);
  auto polled = backend.poll();
  runner.expectTrue(polled.fresh && polled.controllerOk && polled.contactCount == 1,
                    "poll reads a real GT911 register frame");
  runner.expectEq(uint16_t{480}, polled.rawX, "poll reads full 16-bit X coordinate");
  runner.expectEq(uint16_t{800}, polled.rawY, "poll reads full 16-bit Y coordinate");
  runner.expectEq(uint8_t{0}, Wire.getRegister(0x5D, 0x814E), "poll acknowledges the ready frame");
  runner.expectFalse(backend.poll().fresh, "acknowledged frame is not delivered twice");
  runner.expectEq(uint8_t{1}, backend.poll().contactCount, "no new frame preserves the acknowledged held contact");
  Wire.setRegister(0x5D, 0x814E, 0x81);
  Wire.setReadLimit(1);
  runner.expectFalse(backend.poll().controllerOk, "short point read rejects the frame");
  runner.expectEq(uint8_t{0x81}, Wire.getRegister(0x5D, 0x814E), "short read leaves the frame ready for retry");
  Wire.setReadLimit(32);
  Wire.setWriteFailure(true);
  runner.expectFalse(backend.poll().controllerOk, "failed acknowledgement rejects the frame");
  Wire.setWriteFailure(false);
  runner.expectTrue(backend.poll().fresh, "frame is available after acknowledgement recovers");
  Wire.setPresent(0x5D, false);
  runner.expectFalse(backend.poll().controllerOk, "register pointer NACK rejects the poll");
  Wire.setPresent(0x5D, true);

  TouchConfig gt911{};
  gt911.coordinatesAtByte0 = true;
  const uint8_t gtPoint[] = {0xE0, 0x01, 0x20, 0x03, 0, 0, 0, 0};
  RawTouchSample raw{};
  runner.expectTrue(papyrix::board::decodeGt911Frame(gt911, 0x81, gtPoint, sizeof(gtPoint), raw),
                    "GT911 ready frame decodes");
  runner.expectTrue(raw.fresh && raw.controllerOk && raw.contactCount == 1, "GT911 frame reports one contact");
  runner.expectEq(uint16_t{480}, raw.rawX, "GT911 byte-zero layout decodes little-endian X");
  runner.expectEq(uint16_t{800}, raw.rawY, "GT911 byte-zero layout decodes little-endian Y");
  runner.expectTrue(papyrix::board::decodeGt911Frame(gt911, 0x01, nullptr, 0, raw) && !raw.fresh && raw.controllerOk,
                    "GT911 frame without ready bit preserves contact state");
  runner.expectTrue(
      papyrix::board::decodeGt911Frame(gt911, 0x82, nullptr, 0, raw) && raw.fresh && raw.contactCount == 2,
      "GT911 multi-contact count decodes without dynamic storage");
  runner.expectTrue(
      papyrix::board::decodeGt911Frame(gt911, 0x90, nullptr, 0, raw) && raw.homeDown && raw.contactCount == 0,
      "GT911 Home key is separate from screen contacts");
  runner.expectTrue(papyrix::board::decodeGt911Frame(gt911, 0x80, nullptr, 0, raw) && !raw.homeDown,
                    "GT911 idle frame releases Home");

  const uint32_t suppressionBefore = papyrix::board::touchSuppressionRevision();
  papyrix::board::suppressTouchUntilIdle();
  runner.expectTrue(papyrix::board::touchSuppressionRevision() != suppressionBefore,
                    "display refresh advances the touch-suppression revision");

  TapClassifier classifier;
  TapFrame frame = classifier.update(contact(100, 120), 100, true, false, 0);
  runner.expectTrue(frame.down && !frame.up && !frame.tap, "first contact emits TouchDown");
  runner.expectEq(int16_t{100}, frame.position.x, "TouchDown keeps logical X");
  runner.expectTrue(noEvents(classifier.update(contact(100, 120), 130, true, false, 0)),
                    "stationary held contact does not repeat TouchDown");
  frame = classifier.update(idle(), 160, true, false, 0);
  runner.expectTrue(!frame.down && frame.up && frame.tap, "stationary release emits TouchUp and Tap");
  runner.expectEq(int16_t{100}, frame.position.x, "Tap routes to touch-down X");
  runner.expectEq(int16_t{120}, frame.position.y, "Tap routes to touch-down Y");

  classifier = {};
  classifier.update(contact(100, 120), 200, true, false, 0);
  runner.expectTrue(noEvents(classifier.update(contact(100, 120), 2200, true, false, 0)),
                    "long stationary hold does not repeat events");
  frame = classifier.update(idle(), 2300, true, false, 0);
  runner.expectTrue(frame.up && frame.tap, "stationary long hold produces one tap on release");
  classifier = {};
  classifier.update(contact(10, 10), 100, true, false, 0);
  classifier.update(contact(35, 10), 120, true, false, 0);
  frame = classifier.update(idle(), 140, true, false, 0);
  runner.expectTrue(frame.up && !frame.tap, "movement beyond the limit cancels Tap");
  classifier = {};
  classifier.update(contact(10, 10), 100, true, false, 0);
  classifier.update(contact(34, 10), 120, true, false, 0);
  frame = classifier.update(idle(), 140, true, false, 0);
  runner.expectTrue(frame.up && frame.tap, "24-pixel movement remains a tap");

  classifier = {};
  classifier.update(contact(20, 20), 100, true, false, 0);
  frame = classifier.update(contact(20, 20, 2), 120, true, false, 0);
  runner.expectTrue(frame.up && !frame.tap, "second contact cancels the single-contact sequence");
  runner.expectTrue(noEvents(classifier.update(contact(20, 20), 130, true, false, 0)),
                    "multi-contact sequence stays suppressed while held");
  classifier.update(idle(), 140, true, false, 0);
  runner.expectTrue(classifier.update(contact(20, 20), 200, true, false, 0).down,
                    "single contact works after multi-contact returns idle");

  classifier = {};
  classifier.update(contact(30, 30), 100, true, false, 0);
  classifier.update(idle(), 110, true, false, 0);
  runner.expectTrue(noEvents(classifier.update(contact(30, 30), 120, true, false, 0)),
                    "debounce suppresses immediate re-contact");
  classifier.update(idle(), 125, true, false, 0);
  runner.expectTrue(classifier.update(contact(30, 30), 110 + TapClassifier::DEBOUNCE_MS, true, false, 0).down,
                    "contact is accepted after debounce interval");

  classifier = {};
  runner.expectTrue(noEvents(classifier.update(contact(40, 40), 100, false, false, 0)),
                    "disabled touch suppresses contact");
  classifier.update(idle(), 110, true, false, 0);
  runner.expectTrue(classifier.update(contact(40, 40), 200, true, false, 0).down,
                    "contact works after touch is enabled and idle");

  classifier = {};
  runner.expectTrue(noEvents(classifier.update(contact(50, 50), 100, true, true, 0)),
                    "display refresh suppresses contact");
  classifier.update(idle(), 110, true, false, 0);
  runner.expectTrue(classifier.update(contact(50, 50), 200, true, false, 0).down,
                    "contact works after refresh suppression returns idle");

  classifier = {};
  classifier.resetAfterWake();
  runner.expectTrue(noEvents(classifier.update(contact(60, 60), 100, true, false, 0)),
                    "wake ignores a stale held contact");
  classifier.update(idle(), 120, true, false, 0);
  runner.expectTrue(classifier.update(contact(60, 60), 200, true, false, 0).down,
                    "new contact works after post-wake idle frame");

  classifier.resetAfterWake();
  Wire.setRegister(0x5D, 0x814E, 0x80);
  backend.poll();
  auto unchanged = backend.poll();
  classifier.update({unchanged.fresh, unchanged.controllerOk, unchanged.contactCount, {}}, 300, true, false, 0);
  runner.expectTrue(classifier.update(contact(60, 60), 400, true, false, 0).down,
                    "first new contact works after an unchanged idle poll");
  classifier.resetAfterWake();
  Wire.setRegister(0x5D, 0x814E, 0x81);
  backend.poll();
  unchanged = backend.poll();
  classifier.update({unchanged.fresh, unchanged.controllerOk, unchanged.contactCount, {}}, 500, true, false, 0);
  runner.expectTrue(noEvents(classifier.update(contact(60, 60), 600, true, false, 0)),
                    "unchanged held contact cannot rearm wake suppression");
  classifier.update(idle(), 700, true, false, 0);
  runner.expectTrue(classifier.update(contact(60, 60), 800, true, false, 0).down,
                    "release rearms the next contact after an unchanged held poll");

  classifier = {};
  classifier.update(contact(70, 70), 100, true, false, 0);
  frame = classifier.update(contact(70, 70), 120, true, false, 1);
  runner.expectTrue(frame.up && !frame.tap, "orientation change releases and cancels held contact");
  runner.expectTrue(noEvents(classifier.update(contact(70, 70), 130, true, false, 1)),
                    "orientation-changed contact stays suppressed until idle");

  classifier = {};
  classifier.update(contact(80, 80), 100, true, false, 0);
  runner.expectTrue(noEvents(classifier.update(failure(), 150, true, false, 0)),
                    "transient controller failure preserves active contact");
  frame = classifier.update(failure(), 100 + TapClassifier::STALE_CONTACT_MS, true, false, 0);
  runner.expectTrue(frame.up && !frame.tap, "stale controller failure releases without Tap");

  runner.expectTrue(sizeof(Event) <= 8, "coordinate events keep Event at eight bytes or less");
  runner.expectEq(size_t{16}, EventQueue::CAPACITY, "touch events keep fixed queue capacity");
  EventQueue queue;
  const papyrix::TouchPoint point{123, 321};
  runner.expectTrue(queue.push(Event::tap(point)), "queue accepts Tap");
  Event event{};
  runner.expectTrue(queue.pop(event) && event.type == EventType::Tap && event.touch.x == 123 && event.touch.y == 321,
                    "Tap coordinates survive the fixed queue");

  Wire.setPresent(0x5D, false);
  Wire.setPresent(0x14, false);
  testResetGpioEvents();
  runner.expectFalse(backend.begin(*papyrix::board::findProfile(papyrix::board::BoardId::X4Pro)),
                     "failed GT911 probe leaves the backend unavailable");
  int powerLevel = LOW;
  int resetLevel = HIGH;
  for (size_t i = 0; i < testGpioEventCount; ++i) {
    if (testGpioEvents[i].type != TestGpioEventType::DigitalWrite) continue;
    if (testGpioEvents[i].pin == 2) powerLevel = testGpioEvents[i].value;
    if (testGpioEvents[i].pin == 4) resetLevel = testGpioEvents[i].value;
  }
  runner.expectTrue(powerLevel == HIGH && resetLevel == LOW, "failed GT911 probe disables power and holds reset low");

  return runner.allPassed() ? 0 : 1;
}
