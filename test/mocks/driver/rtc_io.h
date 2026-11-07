#pragma once

#include <driver/gpio.h>
#include <esp_err.h>

#include <cstdint>

inline uint64_t testRtcPullupMask = 0;
inline uint64_t testRtcPulldownMask = 0;
inline esp_err_t testRtcPullError = ESP_OK;

inline esp_err_t rtc_gpio_pullup_en(gpio_num_t pin) {
  if (testRtcPullError == ESP_OK) testRtcPullupMask |= uint64_t{1} << pin;
  return testRtcPullError;
}
inline esp_err_t rtc_gpio_pullup_dis(gpio_num_t pin) {
  if (testRtcPullError == ESP_OK) testRtcPullupMask &= ~(uint64_t{1} << pin);
  return testRtcPullError;
}
inline esp_err_t rtc_gpio_pulldown_en(gpio_num_t pin) {
  if (testRtcPullError == ESP_OK) testRtcPulldownMask |= uint64_t{1} << pin;
  return testRtcPullError;
}
inline esp_err_t rtc_gpio_pulldown_dis(gpio_num_t pin) {
  if (testRtcPullError == ESP_OK) testRtcPulldownMask &= ~(uint64_t{1} << pin);
  return testRtcPullError;
}
