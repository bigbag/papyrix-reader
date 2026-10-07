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
    // Cell capacity loaded into the gauge once; 0 leaves the gauge alone.
    uint16_t designCapacityMah = 0;
  };
  explicit BatteryMonitor(const Bq27220Config& cfg);

  struct Cw2017Config {
    int sdaPin;
    int sclPin;
    uint32_t freq = 400000;
    uint8_t address = 0x63;
    const uint8_t* profile = nullptr;
  };
  explicit BatteryMonitor(const Cw2017Config& cfg);

  struct Status {
    bool supported = false;
    bool percentageKnown = false;
    bool millivoltsKnown = false;
    bool chargingKnown = false;
    uint16_t percentage = 0;
    uint16_t millivolts = 0;
    bool charging = false;
  };

  Status readStatus() const;

  // Read voltage and return percentage (0-100). On BQ27220, this is the chip's
  // calibrated SOC; on ADC, it's a polynomial curve fit to LiPo discharge.
  uint16_t readPercentage() const;

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

  // Advance the BQ27220 design-capacity load. The gauge keeps Design Capacity
  // in RAM and resets it to TI's 3000 mAh default when it loses power. One
  // short I2C step per call; the unlock keys must land 1.5-4 s apart, so call
  // this from the main loop. Returns true while a load is still pending.
  bool serviceDesignCapacity();
  // Close out a pending load before deep sleep: leave CONFIG UPDATE, seal the
  // gauge, and verify the sealed state. Returns the verified result.
  bool finishDesignCapacity();

  // Percentage (0-100) from a millivolt value (LiPo curve).
  static uint16_t percentageFromMillivolts(uint16_t millivolts);

  // Calibrate a raw ADC reading and return millivolts.
  static uint16_t millivoltsFromRawAdc(uint16_t adc_raw);

 private:
  enum class Mode : uint8_t { Adc, Bq27220, Cw2017 };
  Mode _mode;

  // ADC mode state
  uint8_t _adcPin = 0;
  float _dividerMultiplier = 2.0f;

  // BQ27220 mode state
  Bq27220Config _i2c{};
  Cw2017Config _cw2017{};
  mutable bool _cw2017Initialized = false;
  mutable unsigned long _cw2017LastInitAttemptMs = 0;
  mutable bool _cw2017SocPolled = false;
  mutable bool _cw2017MvPolled = false;
  mutable bool _haveCw2017Soc = false;
  mutable bool _haveCw2017Mv = false;

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
  struct Bq27220Load {
    uint8_t step = 0;  // 0 check, 1-5 unlock keys, 6 learned FCC, 7 design capacity, 8 exit wait, 9 done
    bool cfg = false;
    bool wrote = false;
    bool exitSent = false;    // exit accepted, CFGUPDATE clear not yet observed
    uint8_t restarts = 0;     // key-pair windows lost to blocking loop work
    uint8_t exitRetries = 0;  // failed exit writes; grows the retry gap
    unsigned long at = 0;
    unsigned long since = 0;
  };
  Bq27220Load _bqLoad{};
  bool bq27220WriteParam_(uint16_t address, uint16_t mah);
  bool bq27220LoadStep_(unsigned long now);
  bool ensureCw2017Profile_() const;
  bool readCw2017Soc_(uint16_t* out) const;
  bool readCw2017Mv_(uint16_t* out) const;
};
