#pragma once

#include <cstdint>

namespace papyrix::board {

// Set a pin to this value when the MCU does not control it.
inline constexpr int8_t kPinUnused = -1;

enum class BoardId : uint8_t { X3, X4, X4Pro, X4Classic };
enum class McuFamily : uint8_t { Esp32C3, Esp32S3 };
enum class StorageTransport : uint8_t { Spi, Sdmmc1Bit };
enum class TouchController : uint8_t { None, Gt911 };
enum class BatteryBackend : uint8_t { None, Adc, Bq27220, Cw2017 };
enum class InputStyle : uint8_t { AdcLadder, DigitalButtons };

enum class RtcType : uint8_t { None, Ds3231, Bm8563 };

struct DisplayConfig {
  uint16_t width;
  uint16_t height;
  int8_t sclk;
  int8_t mosi;
  int8_t cs;
  int8_t dc;
  int8_t rst;
  int8_t busy;
  int8_t power;
  uint32_t spiHz;
  bool rotate180;
};

struct InputConfig {
  InputStyle style;
  int8_t adcPin1;
  int8_t adcPin2;
  int8_t back;
  int8_t confirm;
  int8_t left;
  int8_t right;
  int8_t up;
  int8_t down;
  int8_t power;
  bool activeHigh;
};

struct TouchConfig {
  TouchController controller;
  int8_t sda;
  int8_t scl;
  int8_t irq;
  int8_t rst;
  uint8_t i2cAddress;
  int8_t powerPin;
  bool powerActiveHigh;
  bool swapXY;
  bool flipX;
  bool flipY;
  bool hasHomeKey;
  uint16_t rawMinX = 0;
  uint16_t rawMaxX = 0;
  uint16_t rawMinY = 0;
  uint16_t rawMaxY = 0;
  uint32_t i2cHz = 100000;
  uint8_t i2cAddressAlt = 0;
  bool coordinatesAtByte0 = false;
};

struct StorageConfig {
  StorageTransport transport;
  int8_t spiCs;
  int8_t spiMiso;
  int8_t sdmmcClk;
  int8_t sdmmcCmd;
  int8_t sdmmcDat0;
  int8_t powerPin;
  bool powerActiveHigh;
};

struct BatteryConfig {
  BatteryBackend backend;
  int8_t adcPin;
  int8_t sda;
  int8_t scl;
  uint8_t i2cAddress;
  int8_t chargeStatusPin;
  bool chargeStatusActiveHigh;
  uint32_t i2cHz = 100000;
  const uint8_t* gaugeProfile = nullptr;
  // BQ27220: cell capacity loaded into the gauge; 0 leaves the gauge alone.
  uint16_t designCapacityMah = 0;
};

struct RtcConfig {
  RtcType type;
  int8_t sda;
  int8_t scl;
  uint32_t i2cHz;
  uint8_t i2cAddress;
};

struct FrontLightConfig {
  int8_t gpio;
  int8_t warmGpio;
  uint32_t pwmHz;
  uint8_t resolutionBits;
  bool activeHigh;
};

struct UsbConfig {
  int8_t detectPin;
  bool viaBatteryStatus;
  bool nativeSerialJtag;
};

struct PowerConfig {
  int8_t latchPin;
  bool latchActiveHigh;
};

struct BoardProfile {
  BoardId id;
  McuFamily family;
  const char* name;
  const char* cacheDir;
  const char* renderCacheKey;
  DisplayConfig display;
  InputConfig input;
  TouchConfig touch;
  StorageConfig storage;
  BatteryConfig battery;
  RtcConfig rtc;
  FrontLightConfig frontLight;
  UsbConfig usb;
  PowerConfig power;
};

// These functions report the configured capabilities.
constexpr bool hasTouch(const BoardProfile& p) { return p.touch.controller != TouchController::None; }
constexpr bool hasRtc(const BoardProfile& p) { return p.rtc.type != RtcType::None; }
constexpr bool hasFrontLight(const BoardProfile& p) { return p.frontLight.gpio != kPinUnused; }
constexpr bool hasUsbDetect(const BoardProfile& p) {
  return p.usb.detectPin != kPinUnused || p.usb.viaBatteryStatus || p.usb.nativeSerialJtag;
}
constexpr bool hasBattery(const BoardProfile& p) { return p.battery.backend != BatteryBackend::None; }

}  // namespace papyrix::board
