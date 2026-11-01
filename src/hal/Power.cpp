#include "Power.h"

#include <BootHardware.h>
#include <HardwareIdentity.h>
#include <InputManager.h>
#include <Logging.h>
#include <PowerPolicy.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <TargetConfig.h>
#include <TouchBackend.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
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
  const auto& profile = board::HardwareIdentity::instance().profile();
  disableGpioPullsForSleep(profile.input.power);
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

esp_err_t enterClockDeepSleep(uint64_t delayUs) {
  if (delayUs == 0 || delayUs > 60000000) return ESP_ERR_INVALID_ARG;
  const auto& profile = board::HardwareIdentity::instance().profile();
  const uint64_t wakeMask = board::powerButtonWakeMask(profile);
  if (!wakeMask) return ESP_ERR_INVALID_ARG;
  const int pressed = profile.input.activeHigh ? HIGH : LOW;
  if (digitalRead(profile.input.power) == pressed) return ESP_ERR_SLEEP_REJECT;

  auto error = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (error == ESP_OK) error = esp_sleep_enable_timer_wakeup(delayUs);
  if (error == ESP_OK) {
#if PAPYRIX_TARGET_XTEINK_C3
    error = esp_deep_sleep_enable_gpio_wakeup(
        wakeMask, profile.input.activeHigh ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
    error = esp_sleep_enable_ext1_wakeup_io(
        wakeMask, profile.input.activeHigh ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  }
  bool pinsPrepared = false;
  if (error == ESP_OK) {
    inputManager.stopSampling();
#if PAPYRIX_CAP_TOUCH
    if (!board::stopTouchAcquisition()) error = ESP_ERR_INVALID_STATE;
#endif
    if (error == ESP_OK && digitalRead(profile.input.power) == pressed) error = ESP_ERR_SLEEP_REJECT;
    if (error == ESP_OK) {
      if (SdMan.ready()) SdMan.end();
      SPI.end();
      disableGpioPullsForSleep(profile.input.power);
      board::prepareClockSleepPins(profile);
      gpio_deep_sleep_hold_en();
      pinsPrepared = true;
      error = digitalRead(profile.input.power) == pressed ? ESP_ERR_SLEEP_REJECT : esp_deep_sleep_try_to_start();
    }
  }
  const auto cleanupError = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  if (cleanupError != ESP_OK) error = cleanupError;
  if (pinsPrepared) {
    board::earlyInit(profile, board::BootPurpose::Interactive);
    board::releaseDeepSleepHolds();
  }
  return error == ESP_OK ? ESP_ERR_SLEEP_REJECT : error;
}

}  // namespace papyrix::hal
