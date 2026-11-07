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
#if !PAPYRIX_TARGET_XTEINK_C3
#include <driver/rtc_io.h>
#endif
extern InputManager inputManager;

namespace papyrix::hal {

static esp_err_t enablePowerButtonWakeup(const board::BoardProfile& profile, uint64_t wakeMask) {
#if PAPYRIX_TARGET_XTEINK_C3
  return esp_deep_sleep_enable_gpio_wakeup(
      wakeMask, profile.input.activeHigh ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  // EXT1 uses separate RTC pulls; the SDK holds them through deep sleep.
  const auto pin = static_cast<gpio_num_t>(profile.input.power);
  auto error = profile.input.activeHigh ? rtc_gpio_pullup_dis(pin) : rtc_gpio_pulldown_dis(pin);
  if (error == ESP_OK) error = profile.input.activeHigh ? rtc_gpio_pulldown_en(pin) : rtc_gpio_pullup_en(pin);
  if (error == ESP_OK)
    error = esp_sleep_enable_ext1_wakeup_io(
        wakeMask, profile.input.activeHigh ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
  return error;
#endif
}

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
  const auto error = wakeMask != 0 ? enablePowerButtonWakeup(profile, wakeMask) : ESP_ERR_INVALID_ARG;
  if (error != ESP_OK) {
    LOG_ERR("POWER", "Power wake setup failed (%d); restarting", error);
    esp_restart();
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
  if (error == ESP_OK) error = enablePowerButtonWakeup(profile, wakeMask);
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
