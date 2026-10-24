#include "Power.h"

#include <HardwareIdentity.h>
#include <InputManager.h>
#include <PowerPolicy.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <TargetConfig.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

#if PAPYRIX_TARGET_MURPHY_M4
#include <Logging.h>
#include <TouchBackend.h>
#endif
extern InputManager inputManager;

namespace papyrix::hal {

[[noreturn]] void enterDeepSleepWithHardwareShutdown(bool externalPower) {
  inputManager.stopSampling();
#if PAPYRIX_TARGET_MURPHY_M4
  if (!board::stopTouchAcquisition()) {
    LOG_ERR("POWER", "Touch task does not stop; restart without hardware shutdown");
    esp_restart();
  }
#endif
  if (SdMan.ready()) SdMan.end();
  SPI.end();
  disableGpioPullsForSleep();
  const auto& profile = board::HardwareIdentity::instance().profile();
  const uint64_t wakeMask = board::powerButtonWakeMask(profile);
  if (wakeMask != 0) {
#if PAPYRIX_TARGET_XTEINK_C3
    esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    esp_sleep_enable_ext1_wakeup_io(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  }
  board::prepareDeepSleepPins(profile, externalPower);
  gpio_deep_sleep_hold_en();
  esp_deep_sleep_try_to_start();
  // Returns only if sleep entry is rejected. Reboot instead of spinning.
  esp_restart();
}

}  // namespace papyrix::hal
