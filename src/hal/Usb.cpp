#include "Usb.h"

#include <AdcMutex.h>
#include <Arduino.h>
#include <HardwareIdentity.h>
#include <driver/usb_serial_jtag.h>

#include "Battery.h"
#include "UsbPolicy.h"

namespace papyrix::hal {

void Usb::init(const Battery& battery) {
  battery_ = &battery;
  const auto& config = board::HardwareIdentity::instance().profile().usb;
  if (config.detectPin != board::kPinUnused) {
    pinMode(config.detectPin, INPUT);
    if (config.detectThresholdMv != 0) {
      std::lock_guard<std::mutex> lock(board::adcMutex);
      analogSetPinAttenuation(config.detectPin, ADC_11db);
    }
  }
}

Usb::Status Usb::readStatus() const {
  const auto& config = board::HardwareIdentity::instance().profile().usb;
  Battery::Status batteryStatus;
  if (config.viaBatteryStatus && battery_) batteryStatus = battery_->readStatus();
  bool pinHigh = false;
  if (config.detectPin != board::kPinUnused) {
    if (config.detectThresholdMv != 0) {
      std::lock_guard<std::mutex> lock(board::adcMutex);
      pinHigh = analogReadMilliVolts(config.detectPin) >= config.detectThresholdMv;
    } else {
      pinHigh = digitalRead(config.detectPin) == HIGH;
    }
  }
  const bool nativeAvailable = config.nativeSerialJtag;
  const bool nativeConnected = nativeAvailable && usb_serial_jtag_is_connected();
  const auto status = usb_policy::evaluate(
      config, {batteryStatus.chargingKnown, batteryStatus.charging, pinHigh, nativeAvailable, nativeConnected});
  return {status.available, status.connected};
}

}  // namespace papyrix::hal
