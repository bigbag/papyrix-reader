#pragma once

#include <cstdint>

namespace papyrix::sd {

void enableSdPower(int8_t powerEnablePin);
void prepareSdForDisplayProbe(int8_t powerEnablePin, int8_t chipSelectPin);
void disableSdPower(int8_t powerEnablePin, int8_t chipSelectPin, int8_t clockPin, int8_t misoPin, int8_t mosiPin);

}  // namespace papyrix::sd
