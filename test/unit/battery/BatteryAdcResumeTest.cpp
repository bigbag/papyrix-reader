#include <Arduino.h>
#include <HardwareIdentity.h>
#include <esp_system.h>

#include <cassert>
#include <cstdio>

#include "hal/Battery.h"
#include "hal/Usb.h"

namespace {
uint32_t batteryMv = 4150;
unsigned adcReads = 0;
uint32_t readAdc(uint8_t pin) {
  assert(pin == papyrix::board::HardwareIdentity::instance().profile().battery.adcPin);
  ++adcReads;
  return batteryMv / 2;
}
}  // namespace

int main() {
  using papyrix::hal::Battery;
  testAnalogMillivoltsHook() = readAdc;
  testSetManualMillis(0);
  testResetReason = ESP_RST_POWERON;
  {
    Battery battery;
    battery.init(false);
    assert(battery.readStatus().percentage == 100);
  }
  testResetReason = ESP_RST_DEEPSLEEP;
  batteryMv = 4000;
  uint16_t percentage = 100;
  for (unsigned minute = 0; minute < 60; ++minute) {
    testSetManualMillis(0);
    Battery battery;
    battery.init(false);
    papyrix::hal::Usb usb;
    usb.init(battery);
    const unsigned before = adcReads;
    const auto status = battery.readStatus();
    usb.readStatus();
    assert(adcReads == before + 1);
    assert(status.percentageKnown && status.millivolts == 4000);
    assert(status.percentage <= percentage && status.percentage >= 84);
    percentage = status.percentage;
  }
  assert(percentage == 84);

  Battery awake;
  awake.init(false);
  assert(awake.readStatus().percentage == 84);
  batteryMv = 3500;
  testSetManualMillis(4999);
  assert(awake.readStatus().percentage == 84);
  testSetManualMillis(5000);
  assert(awake.readStatus().percentage == 15);

  batteryMv = 4150;
  testResetReason = ESP_RST_POWERON;
  Battery cold;
  cold.init(false);
  assert(cold.readStatus().percentage == 100);
  testAnalogMillivoltsHook() = nullptr;
  testUseRealtimeMillis();
  std::puts("ADC battery: minute wakes track charge, polling stays bounded, cold boot resets retention");
}
