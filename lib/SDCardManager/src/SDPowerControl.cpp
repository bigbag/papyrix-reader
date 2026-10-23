#include "SDPowerControl.h"

#include <Arduino.h>
#include <driver/gpio.h>

namespace papyrix::sd {

void enableSdPower(int8_t powerEnablePin) {
  if (powerEnablePin < 0) return;

  gpio_hold_dis(static_cast<gpio_num_t>(powerEnablePin));
  pinMode(powerEnablePin, OUTPUT);
  digitalWrite(powerEnablePin, HIGH);
  delay(10);
}

void prepareSdForDisplayProbe(int8_t powerEnablePin, int8_t chipSelectPin) {
  if (powerEnablePin < 0) return;

  enableSdPower(powerEnablePin);
  pinMode(chipSelectPin, OUTPUT);
  digitalWrite(chipSelectPin, HIGH);
}

void disableSdPower(int8_t powerEnablePin, int8_t chipSelectPin, int8_t clockPin, int8_t misoPin, int8_t mosiPin) {
  if (powerEnablePin < 0) return;

  for (const int8_t pin : {chipSelectPin, clockPin, misoPin, mosiPin}) {
    if (pin >= 0) pinMode(pin, INPUT);
  }
  gpio_hold_dis(static_cast<gpio_num_t>(powerEnablePin));
  pinMode(powerEnablePin, OUTPUT);
  digitalWrite(powerEnablePin, LOW);
}

}  // namespace papyrix::sd
