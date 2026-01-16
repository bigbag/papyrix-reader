#include <Arduino.h>
#include <EInkDisplay.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <InputManager.h>
#include <SDCardManager.h>
#include <SPI.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/reader_italic_2b.h>
#include <esp_system.h>
// Medium font (16pt)
#include <builtinFonts/reader_medium_2b.h>
#include <builtinFonts/reader_medium_bold_2b.h>
#include <builtinFonts/reader_medium_italic_2b.h>
// Large font (18pt)
#include <builtinFonts/reader_large_2b.h>
#include <builtinFonts/reader_large_bold_2b.h>
#include <builtinFonts/reader_large_italic_2b.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/network/CalibreConnectionActivity.h"
#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/opds/OpdsBookBrowserActivity.h"
#include "activities/opds/OpdsServerListActivity.h"
#include "activities/reader/ReaderActivity.h"
#include "activities/settings/SettingsActivity.h"
#include "activities/util/FullScreenMessageActivity.h"
#include "config.h"
#include "opds/OpdsServerStore.h"

#define SPI_FQ 40000000
// Display SPI pins (custom pins for XteinkX4, not hardware SPI defaults)
#define EPD_SCLK 8   // SPI Clock
#define EPD_MOSI 10  // SPI MOSI (Master Out Slave In)
#define EPD_CS 21    // Chip Select
#define EPD_DC 4     // Data/Command
#define EPD_RST 5    // Reset
#define EPD_BUSY 6   // Busy

#define UART0_RXD 20  // Used for USB connection detection

#define SD_SPI_MISO 7

EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);
GfxRenderer renderer(einkDisplay);
Activity* currentActivity;

// Fonts - Small (14pt, default)
EpdFont readerFont(&reader_2b);
EpdFont readerBoldFont(&reader_bold_2b);
EpdFont readerItalicFont(&reader_italic_2b);
EpdFontFamily readerFontFamily(&readerFont, &readerBoldFont, &readerItalicFont, &readerBoldFont);

// Fonts - Medium (16pt)
EpdFont readerMediumFont(&reader_medium_2b);
EpdFont readerMediumBoldFont(&reader_medium_bold_2b);
EpdFont readerMediumItalicFont(&reader_medium_italic_2b);
EpdFontFamily readerMediumFontFamily(&readerMediumFont, &readerMediumBoldFont, &readerMediumItalicFont, &readerMediumBoldFont);

// Fonts - Large (18pt)
EpdFont readerLargeFont(&reader_large_2b);
EpdFont readerLargeBoldFont(&reader_large_bold_2b);
EpdFont readerLargeItalicFont(&reader_large_italic_2b);
EpdFontFamily readerLargeFontFamily(&readerLargeFont, &readerLargeBoldFont, &readerLargeItalicFont, &readerLargeBoldFont);

EpdFont smallFont(&small14);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui12Font(&ui_12);
EpdFont uiBold12Font(&ui_bold_12);
EpdFontFamily uiFontFamily(&ui12Font, &uiBold12Font);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

void exitActivity() {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }
}

void enterNewActivity(Activity* activity) {
  if (!activity) {
    Serial.printf("[%lu] [ERR] enterNewActivity called with null activity\n", millis());
    return;
  }
  currentActivity = activity;
  currentActivity->onEnter();
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress() {
  // Skip verification on software restart (e.g., after WiFi memory cleanup)
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_SW) {
    Serial.printf("[%lu] [   ] Skipping wakeup verification (software restart)\n", millis());
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // It takes us some time to wake up from deep sleep, so we need to subtract that from the duration
  uint16_t calibration = 29;
  uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  inputManager.update();
  // Verify the user has actually pressed
  while (!inputManager.isPressed(InputManager::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    inputManager.update();
  }

  t2 = millis();
  if (inputManager.isPressed(InputManager::BTN_POWER)) {
    do {
      delay(10);
      inputManager.update();
    } while (inputManager.isPressed(InputManager::BTN_POWER) && inputManager.getHeldTime() < calibratedPressDuration);
    abort = inputManager.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
  }
}

void waitForPowerRelease() {
  inputManager.update();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    delay(50);
    inputManager.update();
  }
}

// Enter deep sleep mode
void enterDeepSleep() {
  exitActivity();
  enterNewActivity(new SleepActivity(renderer, mappedInputManager));

  einkDisplay.deepSleep();
  Serial.printf("[%lu] [   ] Power button press calibration value: %lu ms\n", millis(), t2 - t1);
  Serial.printf("[%lu] [   ] Entering deep sleep.\n", millis());
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  waitForPowerRelease();
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

void onGoHome();
void onGoToReader(const std::string& initialEpubPath) {
  exitActivity();
  enterNewActivity(new ReaderActivity(renderer, mappedInputManager, initialEpubPath, onGoHome));
}
void onGoToReaderHome() { onGoToReader(std::string()); }
void onContinueReading() { onGoToReader(APP_STATE.openEpubPath); }

void onGoToSettingsFileTransfer();
void onGoToFileTransfer() {
  exitActivity();
  enterNewActivity(new CrossPointWebServerActivity(renderer, mappedInputManager, onGoToSettingsFileTransfer));
}

void onGoToSettingsCalibreWireless();
void onGoToCalibreWireless() {
  exitActivity();
  enterNewActivity(new CalibreConnectionActivity(renderer, mappedInputManager, onGoToSettingsCalibreWireless));
}

void onGoToSettings();
void onGoToSettingsNetLibrary();
void onGoToOpdsServers();
void onOpdsServerSelected(const OpdsServerConfig& server) {
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(renderer, mappedInputManager, server,
                                               onGoToOpdsServers  // onGoBack
                                               ));
}

void onGoToOpdsServers() {
  exitActivity();
  enterNewActivity(
      new OpdsServerListActivity(renderer, mappedInputManager, onGoToSettingsNetLibrary, onOpdsServerSelected));
}

void onGoToSettings() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome, onGoToFileTransfer, onGoToOpdsServers,
                                        onGoToCalibreWireless));
}

void onGoToSettingsNetLibrary() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome, onGoToFileTransfer, onGoToOpdsServers,
                                        onGoToCalibreWireless, 11));
}

void onGoToSettingsCalibreWireless() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome, onGoToFileTransfer, onGoToOpdsServers,
                                        onGoToCalibreWireless, 12));
}

void onGoToSettingsFileTransfer() {
  exitActivity();
  enterNewActivity(new SettingsActivity(renderer, mappedInputManager, onGoHome, onGoToFileTransfer, onGoToOpdsServers,
                                        onGoToCalibreWireless, 13));
}

void onGoHome() {
  exitActivity();
  enterNewActivity(new HomeActivity(renderer, mappedInputManager, onContinueReading, onGoToReaderHome, onGoToSettings));
}

void setupDisplayAndFonts() {
  einkDisplay.begin();
  Serial.printf("[%lu] [   ] Display initialized\n", millis());
  renderer.insertFont(READER_FONT_ID, readerFontFamily);
  renderer.insertFont(READER_FONT_ID_MEDIUM, readerMediumFontFamily);
  renderer.insertFont(READER_FONT_ID_LARGE, readerLargeFontFamily);
  renderer.insertFont(UI_FONT_ID, uiFontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  Serial.printf("[%lu] [   ] Fonts setup\n", millis());
}

void applyThemeFonts() {
  Theme& theme = THEME_MANAGER.mutableCurrent();

  // Reset UI font to builtin first in case custom font loading fails
  theme.uiFontId = UI_FONT_ID;

  // Apply custom UI font if specified (small, always safe to load)
  if (theme.uiFontFamily[0] != '\0') {
    int customUiFontId = FONT_MANAGER.getFontId(theme.uiFontFamily, UI_FONT_ID);
    if (customUiFontId != UI_FONT_ID) {
      theme.uiFontId = customUiFontId;
      Serial.printf("[%lu] [FONT] UI font: %s (ID: %d)\n", millis(), theme.uiFontFamily, customUiFontId);
    }
  }

  // Only load the reader font that matches current font size setting
  // This saves ~500KB+ of RAM by not loading all three sizes
  const char* fontFamilyName = nullptr;
  int* targetFontId = nullptr;
  int builtinFontId = 0;

  switch (SETTINGS.fontSize) {
    case CrossPointSettings::FONT_MEDIUM:
      fontFamilyName = theme.readerFontFamilyMedium;
      targetFontId = &theme.readerFontIdMedium;
      builtinFontId = READER_FONT_ID_MEDIUM;
      break;
    case CrossPointSettings::FONT_LARGE:
      fontFamilyName = theme.readerFontFamilyLarge;
      targetFontId = &theme.readerFontIdLarge;
      builtinFontId = READER_FONT_ID_LARGE;
      break;
    default:  // FONT_SMALL
      fontFamilyName = theme.readerFontFamilySmall;
      targetFontId = &theme.readerFontId;
      builtinFontId = READER_FONT_ID;
      break;
  }

  // Reset to builtin first in case custom font loading fails
  *targetFontId = builtinFontId;

  if (fontFamilyName && fontFamilyName[0] != '\0') {
    int customFontId = FONT_MANAGER.getFontId(fontFamilyName, builtinFontId);
    if (customFontId != builtinFontId) {
      *targetFontId = customFontId;
      Serial.printf("[%lu] [FONT] Reader font: %s (ID: %d)\n", millis(), fontFamilyName, customFontId);
    }
  }
}

void setup() {
  t1 = millis();

  // Only start serial if USB connected
  pinMode(UART0_RXD, INPUT);
  if (digitalRead(UART0_RXD) == HIGH) {
    Serial.begin(115200);
  }

  inputManager.begin();
  // Initialize battery ADC pin with proper attenuation for 0-3.3V range
  analogSetPinAttenuation(BAT_GPIO0, ADC_11db);

  // Initialize SPI with custom pins
  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!SdMan.begin()) {
    Serial.printf("[%lu] [   ] SD card initialization failed\n", millis());
    setupDisplayAndFonts();
    exitActivity();
    enterNewActivity(new FullScreenMessageActivity(renderer, mappedInputManager, "SD card error", BOLD));
    return;
  }

  SETTINGS.loadFromFile();

  // Initialize theme and font managers
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(SETTINGS.themeName);
  THEME_MANAGER.createDefaultThemeFiles();  // Create template files if missing
  Serial.printf("[%lu] [   ] Theme loaded: %s\n", millis(), THEME_MANAGER.currentThemeName());

  // verify power button press duration after we've read settings.
  verifyWakeupLongPress();

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  Serial.printf("[%lu] [   ] Starting CrossPoint version " CROSSPOINT_VERSION "\n", millis());

  setupDisplayAndFonts();
  applyThemeFonts();

  exitActivity();
  enterNewActivity(new BootActivity(renderer, mappedInputManager));

  APP_STATE.loadFromFile();
  if (APP_STATE.openEpubPath.empty()) {
    onGoHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.saveToFile();
    onGoToReader(path);
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  inputManager.update();

  if (Serial && millis() - lastMemPrint >= 10000) {
    Serial.printf("[%lu] [MEM] Free: %d bytes, Total: %d bytes, Min Free: %d bytes\n", millis(), ESP.getFreeHeap(),
                  ESP.getHeapSize(), ESP.getMinFreeHeap());
    lastMemPrint = millis();
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (inputManager.wasAnyPressed() || inputManager.wasAnyReleased() ||
      (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();  // Reset inactivity timer
  }

  const auto autoSleepTimeout = SETTINGS.getAutoSleepTimeoutMs();
  if (autoSleepTimeout > 0 && millis() - lastActivityTime >= autoSleepTimeout) {
    Serial.printf("[%lu] [SLP] Auto-sleep triggered after %lu ms of inactivity\n", millis(), autoSleepTimeout);
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (inputManager.isPressed(InputManager::BTN_POWER) &&
      inputManager.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  const unsigned long activityStartTime = millis();
  if (currentActivity) {
    currentActivity->loop();
  }
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      Serial.printf("[%lu] [LOOP] New max loop duration: %lu ms (activity: %lu ms)\n", millis(), maxLoopDuration,
                    activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();  // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    delay(10);  // Normal delay when no activity requires fast response
  }
}
