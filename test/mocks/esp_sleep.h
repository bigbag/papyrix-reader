#pragma once

#include <esp_err.h>

#include <cstdint>

#include <cassert>


using esp_sleep_wakeup_cause_t = int;
constexpr esp_sleep_wakeup_cause_t ESP_SLEEP_WAKEUP_TIMER = 4;
constexpr esp_sleep_wakeup_cause_t ESP_SLEEP_WAKEUP_EXT1 = 3;
constexpr esp_sleep_wakeup_cause_t ESP_SLEEP_WAKEUP_GPIO = 7;
constexpr esp_sleep_wakeup_cause_t ESP_SLEEP_WAKEUP_ALL = 1;
constexpr int ESP_GPIO_WAKEUP_GPIO_LOW = 0, ESP_GPIO_WAKEUP_GPIO_HIGH = 1;
constexpr int ESP_EXT1_WAKEUP_ANY_LOW = 0, ESP_EXT1_WAKEUP_ANY_HIGH = 1;

inline int testSleepTimerError = ESP_OK;
inline int testSleepCleanupError = ESP_OK;
inline uint64_t testSleepTimerUs = 0;
inline uint64_t testSleepGpioMask = 0;
inline int testSleepPowerError = ESP_OK;
inline unsigned testDeepSleepCalls = 0;
inline int testDeepSleepError = ESP_ERR_SLEEP_REJECT;
inline void (*testDeepSleepHook)() = nullptr;

inline int esp_deep_sleep_enable_gpio_wakeup(uint64_t mask, int) {
  if (testSleepPowerError == ESP_OK) testSleepGpioMask = mask;
  return testSleepPowerError;
}
inline int esp_sleep_enable_ext1_wakeup_io(uint64_t mask, int mode) {
  return esp_deep_sleep_enable_gpio_wakeup(mask, mode);
}
inline int esp_deep_sleep_try_to_start() {
  ++testDeepSleepCalls;
  if (testDeepSleepHook) testDeepSleepHook();
  return testDeepSleepError;
}

inline int esp_sleep_enable_timer_wakeup(uint64_t durationUs) {
  if (testSleepTimerError != ESP_OK) return testSleepTimerError;
  testSleepTimerUs = durationUs;
  return ESP_OK;
}

inline int esp_sleep_disable_wakeup_source(esp_sleep_wakeup_cause_t source) {
  assert(source == ESP_SLEEP_WAKEUP_TIMER || source == ESP_SLEEP_WAKEUP_ALL);
  if (testSleepCleanupError == ESP_OK) {
    testSleepTimerUs = 0;
    if (source == ESP_SLEEP_WAKEUP_ALL) testSleepGpioMask = 0;
  }
  return testSleepCleanupError;
}

inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_TIMER; }
