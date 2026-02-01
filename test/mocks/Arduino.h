#pragma once

// Arduino compatibility shim for host tests
#include <ctype.h>

#include "WString.h"
#include "platform_stubs.h"

#define HEX 16
#define PROGMEM

inline bool isPrintable(char c) { return std::isprint(static_cast<unsigned char>(c)); }
