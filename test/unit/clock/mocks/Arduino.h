#pragma once
#include "../../../mocks/Arduino.h"

constexpr int RISING = 1;
constexpr int FALLING = 2;
void attachInterrupt(uint8_t pin, void (*handler)(), int mode);
void detachInterrupt(uint8_t pin);
