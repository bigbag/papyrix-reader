#pragma once

#include <esp_err.h>

#include <cstdint>

namespace papyrix::hal {

[[noreturn]] void enterDeepSleepWithHardwareShutdown(bool externalPower);
esp_err_t enterClockDeepSleep(uint64_t delayUs);

}  // namespace papyrix::hal
