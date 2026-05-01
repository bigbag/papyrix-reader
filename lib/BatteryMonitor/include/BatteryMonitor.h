#pragma once
#include <cstdint>

class BatteryMonitor {
 public:
  // Optional divider multiplier parameter defaults to 2.0
  explicit BatteryMonitor(uint8_t adcPin, float dividerMultiplier = 2.0f);

  // Read voltage and return percentage (0-100)
  uint16_t readPercentage() const;

  // EMA-smoothed percentage (0-100). Suppresses ADC jitter so the UI doesn't
  // flicker. Single-threaded; use the singleton in src/Battery.h for shared
  // smoothing across UI surfaces.
  uint16_t readSmoothedPercentage() const;

  // Read the battery voltage in millivolts (accounts for divider)
  uint16_t readMillivolts() const;

  // Read raw millivolts from ADC (doesn't account for divider)
  uint16_t readRawMillivolts() const;

  // Read the battery voltage in volts (accounts for divider)
  double readVolts() const;

  // Percentage (0-100) from a millivolt value
  static uint16_t percentageFromMillivolts(uint16_t millivolts);

  // Calibrate a raw ADC reading and return millivolts
  static uint16_t millivoltsFromRawAdc(uint16_t adc_raw);

 private:
  uint8_t _adcPin;
  float _dividerMultiplier;
  // EMA state for readSmoothedPercentage(). Holds smoothed percentage * 10
  // to keep one decimal of precision. Mutable so the smoothing can run from
  // a const method; callers must invoke it from a single thread (papyrix
  // polls battery on the main loop only).
  mutable uint16_t _smoothedScaled = 0;
  mutable bool _smoothInitialized = false;
};
