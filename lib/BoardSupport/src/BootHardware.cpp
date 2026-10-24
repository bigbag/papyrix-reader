#include "BootHardware.h"

#include <Arduino.h>
#include <SDPowerControl.h>
#include <driver/gpio.h>

#include "TargetConfig.h"
#include "TouchBackend.h"

namespace papyrix::board {

void earlyInit(const BoardProfile& profile) {
  if (profile.power.latchPin != kPinUnused) {
    gpio_hold_dis(static_cast<gpio_num_t>(profile.power.latchPin));
    pinMode(profile.power.latchPin, OUTPUT);
    digitalWrite(profile.power.latchPin, profile.power.latchActiveHigh ? HIGH : LOW);
  }
  if (profile.power.latchPin2 != kPinUnused) {
    gpio_hold_dis(static_cast<gpio_num_t>(profile.power.latchPin2));
    pinMode(profile.power.latchPin2, OUTPUT);
    digitalWrite(profile.power.latchPin2, profile.power.latchActiveHigh ? HIGH : LOW);
  }
  if (profile.power.chargeEnablePin != kPinUnused) {
    gpio_hold_dis(static_cast<gpio_num_t>(profile.power.chargeEnablePin));
    pinMode(profile.power.chargeEnablePin, OUTPUT);
    digitalWrite(profile.power.chargeEnablePin, profile.power.chargeEnableActiveHigh ? HIGH : LOW);
    gpio_hold_en(static_cast<gpio_num_t>(profile.power.chargeEnablePin));
  }
  if (profile.power.microphonePowerPin != kPinUnused) {
    gpio_hold_dis(static_cast<gpio_num_t>(profile.power.microphonePowerPin));
    pinMode(profile.power.microphonePowerPin, OUTPUT);
    digitalWrite(profile.power.microphonePowerPin, LOW);
  }
  if (profile.family != McuFamily::Esp32S3) return;
  gpio_hold_dis(static_cast<gpio_num_t>(profile.storage.powerPin));
  pinMode(profile.storage.powerPin, OUTPUT);
  digitalWrite(profile.storage.powerPin, profile.storage.powerActiveHigh ? LOW : HIGH);
  gpio_hold_dis(static_cast<gpio_num_t>(profile.display.rst));
  for (const int8_t pin : {profile.input.back, profile.input.confirm, profile.input.left, profile.input.right,
                           profile.input.up, profile.input.down, profile.input.power}) {
    if (pin != kPinUnused) pinMode(pin, profile.input.activeHigh ? INPUT : INPUT_PULLUP);
  }
#if PAPYRIX_CAP_TOUCH
  enableTouch(profile.touch);
#endif
}

void preparePanelProbe(const BoardProfile& profile) {
  if (profile.storage.transport != StorageTransport::Spi || profile.storage.powerPin == kPinUnused) return;
  papyrix::sd::prepareSdForDisplayProbe(profile.storage.powerPin, profile.storage.spiCs);
}

}  // namespace papyrix::board
