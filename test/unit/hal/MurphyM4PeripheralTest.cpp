#include <Arduino.h>
#include <HardwareIdentity.h>
#include <Preferences.h>
#include <driver/usb_serial_jtag.h>

#include <algorithm>
#include <cassert>

#include "hal/Battery.h"
#include "hal/FrontLight.h"
#include "hal/Usb.h"
#include "test_utils.h"

namespace {
bool chargeLow = true;
unsigned adcReads = 0;
uint32_t adc(uint8_t pin) {
  assert(pin == 9);
  ++adcReads;
  return 2000;
}
int readPin(int pin) { return pin == 43 && chargeLow ? LOW : HIGH; }
}  // namespace

int main() {
  using namespace papyrix;
  TestUtils::TestRunner runner("M4 actual peripheral HAL");
  testSetManualMillis(0);
  testAnalogMillivoltsHook() = adc;
  testSetDigitalReadHook(readPin);
  hal::Battery battery;
  battery.init();
  auto status = battery.readStatus();
  runner.expectTrue(status.millivoltsKnown && status.millivolts == 4000 && status.chargingKnown && status.charging,
                    "ADC9 uses the existing divider while charge43-low supplies independent charging state");
  runner.expectTrue(!battery.serviceDesignCapacity() && battery.finishDesignCapacity(),
                    "ADC-only M4 neither starts gauge calibration nor waits for a gauge close-out");
  chargeLow = false;
  testSetManualMillis(5000);
  status = battery.readStatus();
  runner.expectTrue(status.millivolts == 4000 && status.chargingKnown && !status.charging,
                    "charge43-high changes charging without changing the measured voltage");
  hal::Usb usb;
  testResetGpioEvents();
  usb.init(battery);
  runner.expectTrue(std::none_of(testGpioEvents, testGpioEvents + testGpioEventCount,
                                 [](const TestGpioEvent& event) { return event.pin == 9; }),
                    "USB setup never configures the battery ADC as Sticky supply detection");
  for (bool host : {false, true}) {
    for (bool charging : {false, true}) {
      testNativeUsbConnected = host;
      chargeLow = charging;
      testSetManualMillis(static_cast<uint32_t>(millis()) + 5000);
      const auto power = battery.readStatus();
      const auto native = usb.readStatus();
      runner.expectTrue(
          power.chargingKnown && power.charging == charging && native.available && native.connected == host,
          "native host connection and charging remain independent");
    }
  }
  const auto readsAfterBattery = adcReads;
  usb.readStatus();
  runner.expectTrue(adcReads == readsAfterBattery, "USB status does not sample or reinterpret the battery divider");
  Preferences store;
  store.begin("frontlight");
  store.putUChar("x4pro_b", 37);
  store.putUChar("x4pro_w", 68);
  store.putUChar("unknown_b", 99);
  store.putUChar("unknown_w", 99);
  store.end();
  hal::FrontLight light;
  light.init(false);
  runner.expectTrue(light.isAvailable() && testLedcPins[47].attached && testLedcPins[48].attached &&
                        testLedcPins[47].hz == 25000 && testLedcPins[48].hz == 25000 && testLedcPins[47].bits == 10 &&
                        testLedcPins[48].bits == 10,
                    "both physical light channels use 25 kHz and 10-bit duty");
  for (uint8_t percent : {uint8_t{0}, uint8_t{1}, uint8_t{5}, uint8_t{50}, uint8_t{100}}) {
    runner.expectTrue(light.setBrightness(percent), "brightness persists on the M4");
    for (uint8_t warmth : {uint8_t{0}, uint8_t{100}}) {
      runner.expectTrue(light.setWarmth(warmth), "warmth endpoint persists on the M4");
      const auto cool = testLedcPins[47].duty, warm = testLedcPins[48].duty;
      runner.expectTrue(cool <= 1023 && warm <= 1023 && cool + warm <= 1023 && (warmth == 0 ? warm == 0 : cool == 0) &&
                            (percent == 0 ? cool + warm == 0 : cool + warm > 0),
                        "zero and low brightness remain bounded and select only the requested channel");
    }
  }
  light.setBrightness(50);
  light.setWarmth(25);
  store.begin("frontlight", true);
  runner.expectTrue(store.getUChar("m4_b", 0xFF) == 50 && store.getUChar("m4_w", 0xFF) == 25 &&
                        store.getUChar("x4pro_b", 0) == 37 && store.getUChar("x4pro_w", 0) == 68 &&
                        store.getUChar("unknown_b", 0) == 99 && store.getUChar("unknown_w", 0) == 99,
                    "M4 settings use their own keys without overwriting Pro or unknown-board state");
  store.end();
  Preferences::failPut = true;
  runner.expectFalse(light.setBrightness(75), "failed brightness storage is reported");
  runner.expectEq(uint8_t{75}, light.brightness(), "failed save preserves the existing live-adjustment behavior");
  Preferences::failPut = false;
  Preferences::failBegin = true;
  runner.expectFalse(light.setWarmth(80), "failed NVS open is reported");
  Preferences::failBegin = false;
  light.shutdown(false);
  runner.expectTrue(!testLedcPins[47].attached && !testLedcPins[48].attached && testLedcPins[47].duty == 0 &&
                        testLedcPins[48].duty == 0,
                    "sleep shuts off and releases both light channels");
  hal::FrontLight wake;
  wake.init(false);
  runner.expectTrue(
      wake.brightness() == 50 && wake.warmth() == 25 && testLedcPins[47].duty > 0 && testLedcPins[48].duty > 0,
      "wake restores only the last successfully stored M4 values");
  wake.shutdown(false);
  testLedcFailAttachPin = 48;
  hal::FrontLight unavailable;
  unavailable.init(false);
  runner.expectTrue(!unavailable.isAvailable() && !testLedcPins[47].attached && !testLedcPins[48].attached,
                    "partial PWM setup releases the successful channel");
  testLedcFailAttachPin = -1;
  unavailable.shutdown(false);
  testAnalogMillivoltsHook() = nullptr;
  testSetDigitalReadHook(nullptr);
  testUseRealtimeMillis();
  return runner.allPassed() ? 0 : 1;
}
