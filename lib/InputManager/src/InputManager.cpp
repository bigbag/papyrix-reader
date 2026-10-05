#include "InputManager.h"

#include <AdcMutex.h>
#include <HardwareIdentity.h>

// Recorded ADC values from real devices
// BACK CONF LEFT RGHT   UP DOWN
// 3597 2760 1530    6 2300    6
// 3470 2666 1480    6 2222    5
// 3470 2655 1470    3 2205    3

// Averages
// BACK CONF LEFT RGHT   UP DOWN
// 3512 2694 1493    5 2242    5

// Setup ranges, if ADC value is between value `i` and `i + 1`, button `i` is being pressed
// These ranges are based on real world values above, and are much more tolerant of different
// devices than a fixed threshold check
// These values are calculated by taking the midpoint of the pairs of averaged values above
const int InputManager::ADC_RANGES_1[] = {ADC_NO_BUTTON, 3100, 2090, 750, INT32_MIN};
const int InputManager::ADC_RANGES_2[] = {ADC_NO_BUTTON, 1120, INT32_MIN};
const char* InputManager::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};

InputManager::InputManager()
    : currentState(0),
      lastState(0),
      pressedEvents(0),
      releasedEvents(0),
      lastDebounceTime(0),
      buttonPressStart(0),
      buttonPressFinish(0) {}

InputManager::~InputManager() { stopSampling(); }

bool InputManager::startSampling() {
  if (sampling_) return true;
  sampling_ = samplingTask_.start(
      "Buttons", 4096,
      [this]() {
        while (!samplingTask_.shouldStop()) {
          sampleButtons();
          // Pending changes mean the main loop is behind (e.g. mid-refresh): stay fast.
          // Idle buttons need no 200 Hz polling; this restores pre-task-loop granularity.
          const TickType_t interval =
              pdMS_TO_TICKS(isDebouncePending() ? ACTIVE_SAMPLE_INTERVAL_MS : IDLE_SAMPLE_INTERVAL_MS);
          vTaskDelay(interval == 0 ? 1 : interval);
        }
      },
      2);
  return sampling_;
}

void InputManager::stopSampling() {
  if (!sampling_) return;
  samplingTask_.stop(0);
  sampling_ = false;
  std::lock_guard<std::mutex> lock(samplingMutex_);
  readIndex_ = writeIndex_ = pendingCount_ = 0;
  lastState = sampledState_ = currentState;
  lastDebounceTime = millis();
}

bool InputManager::isDebouncePending() const {
  std::lock_guard<std::mutex> lock(samplingMutex_);
  return pendingCount_ != 0 || lastState != currentState;
}

void InputManager::begin() {
  const auto& input = papyrix::board::HardwareIdentity::instance().profile().input;
  if (input.style == papyrix::board::InputStyle::DigitalButtons) {
    const int8_t pins[] = {input.back, input.confirm, input.left, input.right, input.up, input.down, input.power};
    for (const int8_t pin : pins) {
      if (pin != papyrix::board::kPinUnused) pinMode(pin, input.activeHigh ? INPUT : INPUT_PULLUP);
    }
    return;
  }
  pinMode(BUTTON_ADC_PIN_1, INPUT);
  pinMode(BUTTON_ADC_PIN_2, INPUT);
  pinMode(POWER_BUTTON_PIN, INPUT_PULLUP);
  analogSetAttenuation(ADC_11db);
}

int InputManager::getButtonFromADC(const int adcValue, const int ranges[], const int numButtons) {
  for (int i = 0; i < numButtons; i++) {
    if (ranges[i + 1] < adcValue && adcValue <= ranges[i]) {
      return i;
    }
  }

  return -1;
}

uint8_t InputManager::getState() {
  uint8_t state = 0;
  const auto& input = papyrix::board::HardwareIdentity::instance().profile().input;
  if (input.style == papyrix::board::InputStyle::DigitalButtons) {
    const int8_t pins[] = {input.back, input.confirm, input.left, input.right, input.up, input.down, input.power};
    for (uint8_t button = 0; button < sizeof(pins) / sizeof(pins[0]); ++button) {
      if (pins[button] != papyrix::board::kPinUnused && digitalRead(pins[button]) == (input.activeHigh ? HIGH : LOW)) {
        state |= uint8_t{1} << button;
      }
    }
    return state;
  }

  std::lock_guard<std::mutex> adcLock(papyrix::board::adcMutex);
  // Discard first sample to flush SAR ADC sample-and-hold charge from
  // the previous channel (battery monitor on GPIO0 shares ADC1).
  (void)analogRead(BUTTON_ADC_PIN_1);

  // Read GPIO1 buttons
  const int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }

  // Read GPIO2 buttons (discard first sample — same S&H crosstalk from PIN_1)
  (void)analogRead(BUTTON_ADC_PIN_2);
  const int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }

  // Read power button (digital, active LOW)
  if (digitalRead(POWER_BUTTON_PIN) == LOW) {
    state |= (1 << BTN_POWER);
  }

  return state;
}

void InputManager::sampleButtons() {
  const uint8_t state = getState();
  const unsigned long now = millis();
  std::lock_guard<std::mutex> lock(samplingMutex_);
  if (state != lastState) {
    lastDebounceTime = now;
    lastState = state;
  }
  if (now - lastDebounceTime <= DEBOUNCE_DELAY || state == sampledState_) return;
  sampledState_ = state;
  if (pendingCount_ == pending_.size()) {
    readIndex_ = (readIndex_ + 1) % pending_.size();
    --pendingCount_;
  }
  pending_[writeIndex_] = {now, state};
  writeIndex_ = (writeIndex_ + 1) % pending_.size();
  ++pendingCount_;
}

void InputManager::update() {
  if (!sampling_) sampleButtons();
  std::lock_guard<std::mutex> lock(samplingMutex_);
  pressedEvents = releasedEvents = 0;
  if (pendingCount_ == 0) return;
  const StateChange change = pending_[readIndex_];
  readIndex_ = (readIndex_ + 1) % pending_.size();
  --pendingCount_;
  pressedEvents = change.state & ~currentState;
  releasedEvents = currentState & ~change.state;
  if (pressedEvents != 0 && currentState == 0) buttonPressStart = change.time;
  if (releasedEvents != 0 && change.state == 0) buttonPressFinish = change.time;
  currentState = change.state;
}

bool InputManager::isPressed(const uint8_t buttonIndex) const { return currentState & (1 << buttonIndex); }

bool InputManager::wasPressed(const uint8_t buttonIndex) const { return pressedEvents & (1 << buttonIndex); }

bool InputManager::wasAnyPressed() const { return pressedEvents > 0; }

bool InputManager::wasReleased(const uint8_t buttonIndex) const { return releasedEvents & (1 << buttonIndex); }

bool InputManager::wasAnyReleased() const { return releasedEvents > 0; }

unsigned long InputManager::getHeldTime() const {
  // Still hold a button
  if (currentState > 0) {
    return millis() - buttonPressStart;
  }

  return buttonPressFinish - buttonPressStart;
}

const char* InputManager::getButtonName(const uint8_t buttonIndex) {
  if (buttonIndex <= BTN_POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool InputManager::isPowerButtonPressed() const { return isPressed(BTN_POWER); }
