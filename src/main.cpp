#include <Arduino.h>
#include <EInkDisplay.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <InputManager.h>
#include <LittleFS.h>  // Must be before SdFat includes to avoid FILE_READ/FILE_WRITE redefinition
#include <SDCardManager.h>
#include <SPI.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/reader_italic_2b.h>
// XSmall font (12pt)
#include <builtinFonts/reader_xsmall_bold_2b.h>
#include <builtinFonts/reader_xsmall_italic_2b.h>
#include <builtinFonts/reader_xsmall_regular_2b.h>
#include <driver/gpio.h>
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
#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"
#include "content/ContentTypes.h"
#include "ui/Elements.h"

// New refactored core system
#include "core/BootMode.h"
#include "core/Core.h"
#include "core/StateMachine.h"
#include "images/PapyrixLogo.h"
#include "states/CalibreSyncState.h"
#include "states/ErrorState.h"
#include "states/FileListState.h"
#include "states/HomeState.h"
#include "states/NetworkState.h"
#include "states/ReaderState.h"
#include "states/SettingsState.h"
#include "states/SleepState.h"
#include "states/StartupState.h"
#include "states/SyncState.h"
#include "ui/views/BootSleepViews.h"

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

#define SERIAL_INIT_DELAY_MS 10
#define SERIAL_READY_TIMEOUT_MS 3000

EInkDisplay einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);
GfxRenderer renderer(einkDisplay);

// Extern references for driver wrappers
EInkDisplay& display = einkDisplay;
MappedInputManager& mappedInput = mappedInputManager;

// Core system
namespace papyrix {
Core core;
}

// State instances (pre-allocated, no heap per transition)
static papyrix::StartupState startupState;
static papyrix::HomeState homeState(renderer);
static papyrix::FileListState fileListState(renderer);
static papyrix::ReaderState readerState(renderer);
static papyrix::SettingsState settingsState(renderer);
static papyrix::SyncState syncState(renderer);
static papyrix::NetworkState networkState(renderer);
static papyrix::CalibreSyncState calibreSyncState(renderer);
static papyrix::SleepState sleepState(renderer);
static papyrix::ErrorState errorState(renderer);
static papyrix::StateMachine stateMachine;

RTC_DATA_ATTR uint16_t rtcPowerButtonDurationMs = 400;

// Fonts - XSmall (12pt)
EpdFont readerXSmallFont(&reader_xsmall_regular_2b);
EpdFont readerXSmallBoldFont(&reader_xsmall_bold_2b);
EpdFont readerXSmallItalicFont(&reader_xsmall_italic_2b);
EpdFontFamily readerXSmallFontFamily(&readerXSmallFont, &readerXSmallBoldFont, &readerXSmallItalicFont,
                                     &readerXSmallBoldFont);

// Fonts - Small (14pt, default)
EpdFont readerFont(&reader_2b);
EpdFont readerBoldFont(&reader_bold_2b);
EpdFont readerItalicFont(&reader_italic_2b);
EpdFontFamily readerFontFamily(&readerFont, &readerBoldFont, &readerItalicFont, &readerBoldFont);

// Fonts - Medium (16pt)
EpdFont readerMediumFont(&reader_medium_2b);
EpdFont readerMediumBoldFont(&reader_medium_bold_2b);
EpdFont readerMediumItalicFont(&reader_medium_italic_2b);
EpdFontFamily readerMediumFontFamily(&readerMediumFont, &readerMediumBoldFont, &readerMediumItalicFont,
                                     &readerMediumBoldFont);

// Fonts - Large (18pt)
EpdFont readerLargeFont(&reader_large_2b);
EpdFont readerLargeBoldFont(&reader_large_bold_2b);
EpdFont readerLargeItalicFont(&reader_large_italic_2b);
EpdFontFamily readerLargeFontFamily(&readerLargeFont, &readerLargeBoldFont, &readerLargeItalicFont,
                                    &readerLargeBoldFont);

EpdFont smallFont(&small14);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui12Font(&ui_12);
EpdFont uiBold12Font(&ui_bold_12);
EpdFontFamily uiFontFamily(&ui12Font, &uiBold12Font);

bool isUsbConnected() { return digitalRead(UART0_RXD) == HIGH; }

struct WakeupInfo {
  esp_reset_reason_t resetReason;
  bool isPowerButton;
};

WakeupInfo getWakeupInfo() {
  const bool usbConnected = isUsbConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  // Without USB: power button triggers a full power-on reset (not GPIO wakeup)
  // With USB: power button wakes from deep sleep via GPIO
  const bool isPowerButton =
      (!usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON) ||
      (usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_GPIO && resetReason == ESP_RST_DEEPSLEEP);

  return {resetReason, isPowerButton};
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress(esp_reset_reason_t resetReason) {
  if (resetReason == ESP_RST_SW) {
    Serial.printf("[%lu] [   ] Skipping wakeup verification (software restart)\n", millis());
    return;
  }

  // Fast path for short press mode - skip verification entirely.
  // Uses settings directly (not RTC variable) so it works even after a full power cycle
  // where RTC memory is lost. Needed because inputManager.isPressed() may take up to
  // ~500ms to return the correct state after wake-up.
  if (papyrix::core.settings.shortPwrBtn == papyrix::Settings::PowerSleep) {
    Serial.printf("[%lu] [   ] Skipping wakeup verification (short press mode)\n", millis());
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for the configured duration
  const auto start = millis();
  bool abort = false;
  const uint16_t requiredPressDuration = papyrix::core.settings.getPowerButtonDuration();

  inputManager.update();
  // Verify the user has actually pressed
  while (!inputManager.isPressed(InputManager::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    inputManager.update();
  }

  if (inputManager.isPressed(InputManager::BTN_POWER)) {
    do {
      delay(10);
      inputManager.update();
    } while (inputManager.isPressed(InputManager::BTN_POWER) && inputManager.getHeldTime() < requiredPressDuration);
    abort = inputManager.getHeldTime() < requiredPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    // Hold all GPIO pins at their current state during deep sleep to keep the X4's LDO enabled.
    // Without this, floating pins can cause increased power draw during sleep.
    gpio_deep_sleep_hold_en();
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

void setupDisplayAndFonts() {
  einkDisplay.begin();
  renderer.begin();
  Serial.printf("[%lu] [   ] Display initialized\n", millis());
  renderer.insertFont(READER_FONT_ID_XSMALL, readerXSmallFontFamily);
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

  switch (papyrix::core.settings.fontSize) {
    case papyrix::Settings::FontXSmall:
      fontFamilyName = theme.readerFontFamilyXSmall;
      targetFontId = &theme.readerFontIdXSmall;
      builtinFontId = READER_FONT_ID_XSMALL;
      break;
    case papyrix::Settings::FontMedium:
      fontFamilyName = theme.readerFontFamilyMedium;
      targetFontId = &theme.readerFontIdMedium;
      builtinFontId = READER_FONT_ID_MEDIUM;
      break;
    case papyrix::Settings::FontLarge:
      fontFamilyName = theme.readerFontFamilyLarge;
      targetFontId = &theme.readerFontIdLarge;
      builtinFontId = READER_FONT_ID_LARGE;
      break;
    default:  // FontSmall
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

void showErrorScreen(const char* message) {
  renderer.clearScreen(false);
  renderer.drawCenteredText(UI_FONT_ID, 100, message, true, BOLD);
  renderer.displayBuffer();
}

// Track current boot mode for loop behavior
static papyrix::BootMode currentBootMode = papyrix::BootMode::UI;

// Early initialization - common to both boot modes
// Returns false if critical initialization failed
bool earlyInit() {
  // Only start serial if USB connected
  pinMode(UART0_RXD, INPUT);
  gpio_deep_sleep_hold_dis();  // Release GPIO hold from deep sleep to allow fresh readings
  if (isUsbConnected()) {
    Serial.begin(115200);
    delay(SERIAL_INIT_DELAY_MS);  // Allow USB CDC to initialize
    unsigned long start = millis();
    while (!Serial && (millis() - start) < SERIAL_READY_TIMEOUT_MS) {
      delay(SERIAL_INIT_DELAY_MS);
    }
  }

  inputManager.begin();

  // Initialize SPI and SD card before wakeup verification so settings are available
  SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);
  if (!SdMan.begin()) {
    Serial.printf("[%lu] [   ] SD card initialization failed\n", millis());
    setupDisplayAndFonts();
    showErrorScreen("SD card error");
    return false;
  }

  // Load settings before wakeup verification - without this, a full power cycle
  // (no USB) resets RTC memory and the short power button setting is ignored
  papyrix::core.settings.loadFromFile();
  rtcPowerButtonDurationMs = papyrix::core.settings.getPowerButtonDuration();

  const auto wakeup = getWakeupInfo();
  if (wakeup.isPowerButton) {
    verifyWakeupLongPress(wakeup.resetReason);
  }

  Serial.printf("[%lu] [   ] Starting Papyrix version " PAPYRIX_VERSION "\n", millis());

  // Initialize battery ADC pin with proper attenuation for 0-3.3V range
  analogSetPinAttenuation(BAT_GPIO0, ADC_11db);

  // Initialize internal flash filesystem for font storage
  if (!LittleFS.begin(false)) {
    Serial.printf("[%lu] [FS] LittleFS mount failed, attempting format\n", millis());
    if (!LittleFS.format() || !LittleFS.begin(false)) {
      Serial.printf("[%lu] [FS] LittleFS recovery failed\n", millis());
      showErrorScreen("Internal storage error");
      return false;
    }
    Serial.printf("[%lu] [FS] LittleFS formatted and mounted\n", millis());
  } else {
    Serial.printf("[%lu] [FS] LittleFS mounted\n", millis());
  }

  return true;
}

// Initialize UI mode - full state registration, all resources
void initUIMode() {
  Serial.printf("[%lu] [BOOT] Initializing UI mode\n", millis());
  Serial.printf("[%lu] [BOOT] [UI mode] Free heap: %lu, Max block: %lu\n", millis(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());

  // Initialize theme and font managers (full)
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(papyrix::core.settings.themeName);
  THEME_MANAGER.createDefaultThemeFiles();
  Serial.printf("[%lu] [   ] Theme loaded: %s\n", millis(), THEME_MANAGER.currentThemeName());

  setupDisplayAndFonts();
  applyThemeFonts();

  // Show boot splash only on cold boot (not mode transition)
  const auto& preInitTransition = papyrix::getTransition();
  if (!preInitTransition.isValid()) {
    ui::BootView bootView;
    bootView.setLogo(PapyrixLogo, 128, 128);
    bootView.setVersion(PAPYRIX_VERSION);
    bootView.setStatus("BOOTING");
    ui::render(renderer, THEME, bootView);
  }

  // Register ALL states for UI mode
  stateMachine.registerState(&startupState);
  stateMachine.registerState(&homeState);
  stateMachine.registerState(&fileListState);
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&settingsState);
  stateMachine.registerState(&syncState);
  stateMachine.registerState(&networkState);
  stateMachine.registerState(&calibreSyncState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

  // Initialize core
  auto result = papyrix::core.init();
  if (!result.ok()) {
    Serial.printf("[%lu] [CORE] Init failed: %s\n", millis(), papyrix::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  Serial.printf("[%lu] [CORE] State machine starting (UI mode)\n", millis());
  mappedInputManager.setSettings(&papyrix::core.settings);
  ui::setFrontButtonLayout(papyrix::core.settings.frontButtonLayout);

  // Determine initial state - check for return from reader mode
  papyrix::StateId initialState = papyrix::StateId::Home;
  const auto& transition = papyrix::getTransition();

  if (transition.returnTo == papyrix::ReturnTo::FILE_MANAGER) {
    initialState = papyrix::StateId::FileList;
    Serial.printf("[%lu] [BOOT] Returning to FileList from Reader\n", millis());
  } else {
    Serial.printf("[%lu] [BOOT] Starting at Home\n", millis());
  }

  stateMachine.init(papyrix::core, initialState);

  // Force initial render
  Serial.printf("[%lu] [CORE] Forcing initial render\n", millis());
  stateMachine.update(papyrix::core);

  Serial.printf("[%lu] [BOOT] [UI mode] After init - Free heap: %lu, Max block: %lu\n", millis(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
}

// Initialize Reader mode - minimal state registration, single font size
void initReaderMode() {
  Serial.printf("[%lu] [BOOT] Initializing READER mode\n", millis());
  Serial.printf("[%lu] [BOOT] [READER mode] Free heap: %lu, Max block: %lu\n", millis(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());

  // Detect content type early to decide if we need custom fonts
  // XTC/XTCH files contain pre-rendered bitmaps and don't need fonts for page rendering
  const auto& transition = papyrix::getTransition();
  papyrix::ContentType contentType = papyrix::detectContentType(transition.bookPath);
  bool needsCustomFonts = (contentType != papyrix::ContentType::Xtc);

  // Initialize theme and font managers (minimal - no cache)
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(papyrix::core.settings.themeName);
  // Skip createDefaultThemeFiles() - not needed in reader mode
  Serial.printf("[%lu] [   ] Theme loaded: %s (reader mode)\n", millis(), THEME_MANAGER.currentThemeName());

  setupDisplayAndFonts();  // Builtin fonts - always needed for UI

  if (needsCustomFonts) {
    applyThemeFonts();  // Custom fonts - skip for XTC/XTCH to save ~500KB+ RAM
  } else {
    Serial.printf("[%lu] [BOOT] Skipping custom fonts for XTC content\n", millis());
  }

  // Register ONLY states needed for Reader mode
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

  // Initialize core
  auto result = papyrix::core.init();
  if (!result.ok()) {
    Serial.printf("[%lu] [CORE] Init failed: %s\n", millis(), papyrix::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  Serial.printf("[%lu] [CORE] State machine starting (READER mode)\n", millis());
  mappedInputManager.setSettings(&papyrix::core.settings);
  ui::setFrontButtonLayout(papyrix::core.settings.frontButtonLayout);

  if (transition.bookPath[0] != '\0') {
    // Copy path to shared buffer for ReaderState to consume
    strncpy(papyrix::core.buf.path, transition.bookPath, sizeof(papyrix::core.buf.path) - 1);
    papyrix::core.buf.path[sizeof(papyrix::core.buf.path) - 1] = '\0';
    Serial.printf("[%lu] [BOOT] Opening book: %s\n", millis(), papyrix::core.buf.path);
  } else {
    // No book path - fall back to UI mode to avoid boot loop
    Serial.printf("[%lu] [BOOT] ERROR: No book path in transition, falling back to UI\n", millis());
    initUIMode();
    return;
  }

  stateMachine.init(papyrix::core, papyrix::StateId::Reader);

  // Force initial render
  Serial.printf("[%lu] [CORE] Forcing initial render\n", millis());
  stateMachine.update(papyrix::core);

  Serial.printf("[%lu] [BOOT] [READER mode] After init - Free heap: %lu, Max block: %lu\n", millis(), ESP.getFreeHeap(),
                ESP.getMaxAllocHeap());
}

void setup() {
  // Early initialization (common to both modes)
  if (!earlyInit()) {
    return;  // Critical failure
  }

  // Detect boot mode from RTC memory or settings
  currentBootMode = papyrix::detectBootMode();

  if (currentBootMode == papyrix::BootMode::READER) {
    initReaderMode();
  } else {
    initUIMode();
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

  // Poll input and push events to queue
  papyrix::core.input.poll();

  // Auto-sleep after inactivity
  const auto autoSleepTimeout = papyrix::core.settings.getAutoSleepTimeoutMs();
  if (autoSleepTimeout > 0 && papyrix::core.input.idleTimeMs() >= autoSleepTimeout) {
    Serial.printf("[%lu] [SLP] Auto-sleep after %lu ms idle\n", millis(), autoSleepTimeout);
    stateMachine.init(papyrix::core, papyrix::StateId::Sleep);
    return;
  }

  // Power button sleep check: track held time that excludes long rendering gaps
  // where button state changes could have been missed by inputManager
  {
    static unsigned long powerHeldSinceMs = 0;
    static unsigned long prevPowerCheckMs = 0;
    const unsigned long loopGap = loopStartTime - prevPowerCheckMs;
    prevPowerCheckMs = loopStartTime;

    if (inputManager.isPressed(InputManager::BTN_POWER)) {
      if (powerHeldSinceMs == 0 || loopGap > 100) {
        powerHeldSinceMs = loopStartTime;
      }
      if (loopStartTime - powerHeldSinceMs > papyrix::core.settings.getPowerButtonDuration()) {
        stateMachine.init(papyrix::core, papyrix::StateId::Sleep);
        return;
      }
    } else {
      powerHeldSinceMs = 0;
    }
  }

  // CPU frequency scaling: drop to 10 MHz after idle to save battery,
  // restore full speed on any activity. Must run BEFORE stateMachine.update()
  // so rendering always happens at full CPU/SPI speed after wake.
  // Idea: CrossPoint HalPowerManager by @ngxson (https://github.com/ngxson)
  static constexpr unsigned long kIdlePowerSavingMs = 3000;
  static bool cpuThrottled = false;
  const bool isIdle = papyrix::core.input.idleTimeMs() >= kIdlePowerSavingMs;

  if (isIdle && !cpuThrottled) {
    setCpuFrequencyMhz(10);
    cpuThrottled = true;
  } else if (!isIdle && cpuThrottled) {
    setCpuFrequencyMhz(160);
    cpuThrottled = false;
  }

  // Update state machine (handles transitions and rendering)
  const unsigned long activityStartTime = millis();
  stateMachine.update(papyrix::core);
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
  // Increase delay after idle to save power (~4x less CPU load)
  // Idea: https://github.com/crosspoint-reader/crosspoint-reader/commit/0991782 by @ngxson (https://github.com/ngxson)
  if (isIdle) {
    delay(50);
  } else {
    delay(10);
  }
}
