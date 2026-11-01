#pragma once

#include "platform_stubs.h"

using gpio_num_t = int;
using esp_err_t = int;

esp_err_t gpio_hold_dis(gpio_num_t pin);
esp_err_t gpio_hold_en(gpio_num_t pin);
esp_err_t gpio_sleep_sel_dis(gpio_num_t pin);
void gpio_deep_sleep_hold_en();
void gpio_deep_sleep_hold_dis();
inline constexpr gpio_num_t GPIO_NUM_0 = 0, GPIO_NUM_1 = 1, GPIO_NUM_2 = 2, GPIO_NUM_4 = 4,
                            GPIO_NUM_5 = 5, GPIO_NUM_6 = 6, GPIO_NUM_7 = 7, GPIO_NUM_8 = 8,
                            GPIO_NUM_9 = 9, GPIO_NUM_10 = 10, GPIO_NUM_13 = 13, GPIO_NUM_20 = 20,
                            GPIO_NUM_21 = 21;
inline void gpio_pullup_dis(gpio_num_t pin) { testRecordGpioEvent(TestGpioEventType::PullUpDisable, pin, 0); }
inline void gpio_pulldown_dis(gpio_num_t pin) { testRecordGpioEvent(TestGpioEventType::PullDownDisable, pin, 0); }
