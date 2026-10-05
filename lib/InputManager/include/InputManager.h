#pragma once

#include <Arduino.h>
#include <BackgroundTask.h>
#include <driver/gpio.h>

#include <array>
#include <mutex>

class InputManager {
 public:
  InputManager();
  ~InputManager();
  void begin();
  uint8_t getState();
  bool startSampling();
  void stopSampling();

  /**
   * Updates the button states. Should be called regularly in the main loop.
   */
  void update();

  bool isDebouncePending() const;

  /**
   * Returns true if the button was being held at the time of the last #update() call.
   *
   * @param buttonIndex the button indexes
   * @return the button current press state
   */
  bool isPressed(uint8_t buttonIndex) const;

  /**
   * Returns true if the button went from unpressed to pressed between the last two #update() calls.
   *
   * This differs from #isPressed() in that pressing and holding a button will cause this function
   * to return true after the first #update() call, but false on subsequent calls, whereas #isPressed()
   * will continue to return true.
   *
   * @param buttonIndex
   * @return the button pressed state
   */
  bool wasPressed(uint8_t buttonIndex) const;

  /**
   * Returns true if any button started being pressed between the last two #update() calls
   *
   * @return true if any button started being pressed between the last two #update() calls
   */
  bool wasAnyPressed() const;

  /**
   * Returns true if the button went from pressed to unpressed between the last two #update() calls
   *
   * @param buttonIndex the button indexes
   * @return the button release state
   */
  bool wasReleased(uint8_t buttonIndex) const;

  /**
   * Returns true if any button was released between the last two #update() calls
   *
   * @return  true if any button was released between the last two #update() calls
   */
  bool wasAnyReleased() const;

  /**
   * Returns the time between any button starting to be depressed and all buttons between released
   *
   * @return duration in milliseconds
   */
  unsigned long getHeldTime() const;

  // Button indices
  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;

  // Pins
  static constexpr int BUTTON_ADC_PIN_1 = 1;
  static constexpr int BUTTON_ADC_PIN_2 = 2;
  static constexpr int POWER_BUTTON_PIN = 3;

  // Power button methods
  bool isPowerButtonPressed() const;

  // Button names
  static const char* getButtonName(uint8_t buttonIndex);

 private:
  int getButtonFromADC(int adcValue, const int ranges[], int numButtons);
  void sampleButtons();

  struct StateChange {
    unsigned long time;
    uint8_t state;
  };
  std::array<StateChange, 32> pending_{};
  size_t readIndex_ = 0;
  size_t writeIndex_ = 0;
  size_t pendingCount_ = 0;
  uint8_t sampledState_ = 0;
  bool sampling_ = false;
  mutable std::mutex samplingMutex_;

  uint8_t currentState;
  uint8_t lastState;
  uint8_t pressedEvents;
  uint8_t releasedEvents;
  unsigned long lastDebounceTime;
  unsigned long buttonPressStart;
  unsigned long buttonPressFinish;

  static constexpr int NUM_BUTTONS_1 = 4;
  static const int ADC_RANGES_1[];

  static constexpr int NUM_BUTTONS_2 = 2;
  static const int ADC_RANGES_2[];

  static constexpr int ADC_NO_BUTTON = 3900;
  static constexpr unsigned long DEBOUNCE_DELAY = 20;
  static constexpr unsigned long ACTIVE_SAMPLE_INTERVAL_MS = 5;
  static constexpr unsigned long IDLE_SAMPLE_INTERVAL_MS = 50;

  static const char* BUTTON_NAMES[];
  BackgroundTask samplingTask_;
};

// Disable internal pull-ups/pull-downs on all GPIOs to minimize leakage current during deep sleep.
// Skips POWER_BUTTON_PIN (wakeup source — needs pull-up to avoid floating/spurious wakeups).
inline void disableGpioPullsForSleep() {
  static constexpr gpio_num_t pins[] = {
      GPIO_NUM_0, GPIO_NUM_1, GPIO_NUM_2,  GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,
      GPIO_NUM_8, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_13, GPIO_NUM_20, GPIO_NUM_21,
  };
  for (auto pin : pins) {
    gpio_pullup_dis(pin);
    gpio_pulldown_dis(pin);
  }
}
