#pragma once

#include <cstdint>

namespace papyrix {
namespace drivers {

class Device {
 public:
  enum class Type : uint8_t { Unknown = 0, X4 = 1, X3 = 2 };

  struct ProbeReport {
    bool bq27220 = false;
    bool ds3231 = false;
    bool qmi8658 = false;
    uint8_t score = 0;
  };

  static Device& instance();

  // Resolve device type. Reads NVS override → cached value → runs I2C probe.
  // Caches conclusive probe results in NVS. Idempotent (re-callable).
  // Must be called before any hardware init that depends on panel geometry
  // (i.e. before EInkDisplay::begin / Display::init).
  void probe();

  Type type() const { return type_; }
  bool isX3() const { return type_ == Type::X3; }
  bool isX4() const { return type_ == Type::X4; }

  // Diagnostic: chips seen on the most recent probe pass.
  ProbeReport lastProbe() const { return lastProbe_; }

  // Returns the cache subdirectory for this device variant.
  // X4 → "/.papyrix/cache" (legacy path, preserves existing caches).
  // X3 → "/.papyrix/cache/x3" (separate so an SD card moved between devices
  //      doesn't load X4-shaped layouts on an X3 panel).
  const char* cacheDir() const;

 private:
  Device() = default;
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;

  Type type_ = Type::Unknown;
  ProbeReport lastProbe_{};

  ProbeReport runProbePass_();
  Type runFullProbe_();        // two-pass; X3 if both ≥2, X4 if both 0, else Unknown
  Type readOverride_() const;  // NVS manual override
  Type readCache_() const;     // NVS detection cache
  void writeCache_(Type t);    // NVS persist (write-only-if-changed)
};

}  // namespace drivers
}  // namespace papyrix
