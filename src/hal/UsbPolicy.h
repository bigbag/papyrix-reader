#pragma once

#include <BoardProfile.h>

namespace papyrix::hal::usb_policy {

struct Status {
  bool available;
  bool connected;
};

struct Inputs {
  bool gaugeAvailable;
  bool gaugeCharging;
  bool pinHigh;
  bool nativeAvailable;
  bool nativeConnected;
};

class StateTracker {
 public:
  bool update(bool connected) {
    if (!initialized_) {
      initialized_ = true;
      connected_ = connected;
      return false;
    }
    if (connected_ == connected) return false;
    connected_ = connected;
    return true;
  }

 private:
  bool initialized_ = false;
  bool connected_ = false;
};

constexpr Status evaluate(const board::UsbConfig& config, const Inputs& inputs) {
  bool available = false;
  bool connected = false;
  // Gauge charging is the only wall-charger signal on boards without a detect
  // pin, so it feeds "connected". Caveat: charge termination at 100 % draws
  // near-zero current and reads as "not connected" until charging resumes.
  // Keep this separate from host detection (native USB) when a future consumer
  // needs cable state rather than external power.
  if (config.viaBatteryStatus && inputs.gaugeAvailable) {
    available = true;
    connected = inputs.gaugeCharging;
  }
  if (config.detectPin != board::kPinUnused) {
    available = true;
    connected = connected || inputs.pinHigh;
  }
  if (config.nativeSerialJtag && inputs.nativeAvailable) {
    available = true;
    connected = connected || inputs.nativeConnected;
  }
  return {available, connected};
}

}  // namespace papyrix::hal::usb_policy
