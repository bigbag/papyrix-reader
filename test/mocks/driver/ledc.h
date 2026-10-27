#pragma once

#include "../platform_stubs.h"

enum ledc_mode_t { LEDC_LOW_SPEED_MODE };
enum ledc_channel_t { LEDC_CHANNEL_0, LEDC_CHANNEL_1 };

inline int ledc_fade_stop(ledc_mode_t, ledc_channel_t channel) {
  for (auto& pin : testLedcPins) {
    if (pin.attached && pin.channel == channel) {
      testLedcSyncPin(pin);
      pin.fading = false;
    }
  }
  return 0;
}
