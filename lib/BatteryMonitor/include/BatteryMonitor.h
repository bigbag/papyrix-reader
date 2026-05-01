#pragma once
#include <cstdint>

class BatteryMonitor {
 public:
  // ADC mode (Xteink X4): voltage divider on adcPin.
  explicit BatteryMonitor(uint8_t adcPin, float dividerMultiplier = 2.0f);

  // BQ27220 mode (Xteink X3): fuel gauge on I²C 0x55. The bus is brought up
  // and torn down per read so the SDA/SCL pins remain available for the
  // existing USB-detection digitalRead between calls.
  struct Bq27220Config {
    int sdaPin;
    int sclPin;
    uint32_t freq = 400000;
  };
  explicit BatteryMonitor(const Bq27220Config& cfg);

  // Read voltage and return percentage (0-100). On BQ27220, this is the chip's
  // calibrated SOC; on ADC, it's a polynomial curve fit to LiPo discharge.
  uint16_t readPercentage() const;

  // EMA-smoothed percentage (0-100). Suppresses jitter so the UI doesn't
  // flicker. Single-threaded; use the singleton in src/Battery.h for shared
  // smoothing across UI surfaces.
  uint16_t readSmoothedPercentage() const;

  // Read the battery voltage in millivolts (accounts for divider on ADC mode).
  uint16_t readMillivolts() const;

  // Read raw millivolts from ADC (doesn't account for divider). ADC mode only;
  // returns the same value as readMillivolts() in BQ27220 mode.
  uint16_t readRawMillivolts() const;

  // Read the battery voltage in volts.
  double readVolts() const;

  // True if the battery is currently being charged. BQ27220 mode reads the
  // signed Current() register (positive = charging). Returns false on ADC
  // mode and on transient I²C failures (callers using this on X3 typically
  // also have pin-20 USB detection as a fallback).
  bool isCharging() const;

  // Percentage (0-100) from a millivolt value (LiPo curve).
  static uint16_t percentageFromMillivolts(uint16_t millivolts);

  // Calibrate a raw ADC reading and return millivolts.
  static uint16_t millivoltsFromRawAdc(uint16_t adc_raw);

 private:
  enum class Mode : uint8_t { Adc, Bq27220 };
  Mode _mode;

  // ADC mode state
  uint8_t _adcPin = 0;
  float _dividerMultiplier = 2.0f;

  // BQ27220 mode state
  Bq27220Config _i2c{};

  // EMA state for readSmoothedPercentage(). Holds smoothed percentage * 10
  // to keep one decimal of precision. Mutable so the smoothing can run from
  // a const method; callers must invoke it from a single thread (papyrix
  // polls battery on the main loop only).
  mutable uint16_t _smoothedScaled = 0;
  mutable bool _smoothInitialized = false;

  // Cached last good readings for BQ27220 mode. Returned on transient I²C
  // failure so the UI doesn't snap to 0%, and on poll-rate hits so we don't
  // hammer the bus every render.
  mutable uint16_t _lastGoodSoc = 0;
  mutable uint16_t _lastGoodMv = 0;
  mutable int16_t _lastGoodCurrentMa = 0;
  mutable unsigned long _lastSocPollMs = 0;
  mutable unsigned long _lastMvPollMs = 0;
  mutable unsigned long _lastCurrentPollMs = 0;
  mutable bool _haveBqReading = false;
  mutable bool _haveBqCurrent = false;

  // Min interval between BQ27220 hardware reads (ms). Caller still gets the
  // cached value on every call — this just rate-limits the I²C traffic.
  static constexpr unsigned long kBqPollIntervalMs = 1000;

  uint16_t readBq27220Soc_() const;
  uint16_t readBq27220Mv_() const;
  bool readBq27220Current_(int16_t* outMa) const;
};
