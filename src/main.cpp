#include <Arduino.h>
#include <BoardProfiles.h>
#include <BoardSelector.h>
#include <BootHardware.h>
#include <Display.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <HardwareIdentity.h>
#include <HardwareRecovery.h>
#include <InputManager.h>
#include <LittleFS.h>  // Must be before SdFat includes to avoid FILE_READ/FILE_WRITE redefinition
#include <PowerPolicy.h>
#include <SDCardManager.h>
#include <SDPowerControl.h>
#include <SPI.h>
#include <TargetConfig.h>
#include <builtinFonts/reader_2b.h>
#include <builtinFonts/reader_bold_2b.h>
#include <builtinFonts/reader_italic_2b.h>
// XSmall font (12pt)
#include <builtinFonts/reader_xsmall_bold_2b.h>
#include <builtinFonts/reader_xsmall_italic_2b.h>
#include <builtinFonts/reader_xsmall_regular_2b.h>
#include <esp_system.h>

#include "core/CrashDebug.h"
// Medium font (16pt)
#include <builtinFonts/reader_medium_2b.h>
#include <builtinFonts/reader_medium_bold_2b.h>
#include <builtinFonts/reader_medium_italic_2b.h>
// Large font (18pt)
#include <Logging.h>
#include <builtinFonts/reader_large_2b.h>
#include <builtinFonts/reader_large_bold_2b.h>
#include <builtinFonts/reader_large_italic_2b.h>
#include <builtinFonts/small14.h>
#include <builtinFonts/ui_12.h>
#include <builtinFonts/ui_bold_12.h>

#include "FontManager.h"
#include "MappedInputManager.h"
#include "ThemeManager.h"
#include "config.h"
#include "content/ContentTypes.h"
#include "hal/Power.h"
#include "hal/Recovery.h"
#include "hal/UsbPolicy.h"
#include "ui/Elements.h"

#define TAG "MAIN"

// New refactored core system
#include <I18n.h>

#include "I18nLoader.h"
#include "core/BootMode.h"
#include "core/Core.h"
#include "core/EmergencyFirmwareUpdatePolicy.h"
#include "core/FirmwareUpdater.h"
#include "core/StateMachine.h"
#include "diagnostics/X4ProCharacterization.h"
#include "states/AppLauncherState.h"
#include "states/CalibreSyncState.h"
#include "states/ErrorState.h"
#include "states/FileListState.h"
#include "states/HomeState.h"
#include "states/NetworkState.h"
#include "states/ReaderState.h"
#include "states/RecentState.h"
#include "states/SettingsState.h"
#include "states/SleepState.h"
#include "states/StartupState.h"
#include "ui/views/BootSleepViews.h"

constexpr uint32_t kSerialBaudRate = 115200;
constexpr uint32_t kSerialEnumerationDelayMs = 250;
constexpr uint32_t kSerialTxTimeoutMs = 1;

InputManager inputManager;
MappedInputManager mappedInputManager(inputManager);

namespace papyrix {
Core core;
}

GfxRenderer renderer(papyrix::core.display);

// State instances (pre-allocated, no heap per transition)
static papyrix::StartupState startupState;
static papyrix::HomeState homeState(renderer);
static papyrix::FileListState fileListState(renderer);
static papyrix::RecentState recentState(renderer);
static papyrix::ReaderState readerState(renderer);
static papyrix::SettingsState settingsState(renderer);
static papyrix::NetworkState networkState(renderer);
static papyrix::CalibreSyncState calibreSyncState(renderer);
static papyrix::AppLauncherState appLauncherState(renderer);
static papyrix::SleepState sleepState(renderer);
static papyrix::ErrorState errorState(renderer);
static papyrix::StateMachine stateMachine;

RTC_DATA_ATTR uint16_t rtcPowerButtonDurationMs = 400;

// Always-needed fonts (UI, status bar)
EpdFont smallFont(&small14);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui12Font(&ui_12);
EpdFont uiBold12Font(&ui_bold_12);
EpdFontFamily uiFontFamily(&ui12Font, &uiBold12Font);

// Reader font families — lazily constructed via static locals so only the
// active size allocates EpdFont objects (~520 bytes each × 3 per size).
// In READER mode this saves ~4.5KB by not instantiating unused sizes.
static EpdFontFamily& readerFontFamilyXSmall() {
  static EpdFont r(&reader_xsmall_regular_2b), b(&reader_xsmall_bold_2b), i(&reader_xsmall_italic_2b);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}
static EpdFontFamily& readerFontFamilySmall() {
  static EpdFont r(&reader_2b), b(&reader_bold_2b), i(&reader_italic_2b);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}
static EpdFontFamily& readerFontFamilyMedium() {
  static EpdFont r(&reader_medium_2b), b(&reader_medium_bold_2b), i(&reader_medium_italic_2b);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}
static EpdFontFamily& readerFontFamilyLarge() {
  static EpdFont r(&reader_large_2b), b(&reader_large_bold_2b), i(&reader_large_italic_2b);
  static EpdFontFamily f(&r, &b, &i, &b);
  return f;
}

struct WakeupInfo {
  esp_reset_reason_t resetReason;
  bool isPowerButton;
  bool usbColdBoot;
};

WakeupInfo getWakeupInfo() {
  const bool usbConnected = papyrix::core.usb.isConnected();
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();

  // Deep sleep uses the power-button wake mask on both targets.
  const bool isPowerButton =
      (!usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON) ||
      ((wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1) &&
       resetReason == ESP_RST_DEEPSLEEP);

  // USB plugged into a powered-off device cold-boots the chip without a button press.
  // Restricted to ESP_RST_POWERON so any other reset reason (UNKNOWN/USB/JTAG/SW/...) — including
  // dev flashes — still boots normally.
  const bool usbColdBoot = usbConnected && wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON;

  return {resetReason, isPowerButton, usbColdBoot};
}

// Verify long press on wake-up from deep sleep
void verifyWakeupLongPress(esp_reset_reason_t resetReason) {
  if (resetReason == ESP_RST_SW) {
    LOG_DBG(TAG, "Skipping wakeup verification (software restart)");
    return;
  }

  // Fast path for short press mode - skip verification entirely.
  // Uses settings directly (not RTC variable) so it works even after a full power cycle
  // where RTC memory is lost. Needed because inputManager.isPressed() may take up to
  // ~500ms to return the correct state after wake-up.
  if (papyrix::core.settings.shortPwrBtn == papyrix::Settings::PowerSleep) {
    LOG_DBG(TAG, "Skipping wakeup verification (short press mode)");
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for the configured duration.
  const auto start = millis();
  bool abort = false;
  const uint16_t requiredPressDuration = papyrix::core.settings.getPowerButtonDuration();

  // Count boot time as part of the required hold time.
  // X3 board and display detection make its boot slower than X4.
  const uint16_t calibratedPressDuration =
      (start < requiredPressDuration) ? static_cast<uint16_t>(requiredPressDuration - start) : 1;

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
    } while (inputManager.isPressed(InputManager::BTN_POWER) && inputManager.getHeldTime() < calibratedPressDuration);
    abort = inputManager.getHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    if (!papyrix::core.battery.finishDesignCapacity()) {
      LOG_ERR(TAG, "Fuel-gauge close-out failed; sleeping anyway");
    }
    papyrix::hal::enterDeepSleepWithHardwareShutdown(papyrix::core.usb.isConnected());
  }
}

static constexpr uint32_t POWER_RELEASE_TIMEOUT_MS = 5000;

// A power press still held from setup is consumed by hal::Input until a
// release is observed; see Input::suppressPowerUntilRelease().

bool waitForPowerRelease() {
  inputManager.update();
  const uint32_t startedMs = millis();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    if (millis() - startedMs >= POWER_RELEASE_TIMEOUT_MS) {
      LOG_ERR(TAG, "Power button still held after %lu ms; continuing",
              static_cast<unsigned long>(POWER_RELEASE_TIMEOUT_MS));
      return true;
    }
    delay(50);
    inputManager.update();
  }
  return false;
}

// Register only the reader font for the active size (saves ~4.5KB in READER mode)
void setupReaderFontForSize(papyrix::Settings::FontSize fontSize) {
  switch (fontSize) {
    case papyrix::Settings::FontXSmall:
      renderer.insertFont(READER_FONT_ID_XSMALL, readerFontFamilyXSmall());
      break;
    case papyrix::Settings::FontMedium:
      renderer.insertFont(READER_FONT_ID_MEDIUM, readerFontFamilyMedium());
      break;
    case papyrix::Settings::FontLarge:
      renderer.insertFont(READER_FONT_ID_LARGE, readerFontFamilyLarge());
      break;
    default:  // FontSmall
      renderer.insertFont(READER_FONT_ID, readerFontFamilySmall());
      break;
  }
}

void initializeDisplayWithRecovery() {
  auto& display = papyrix::core.display;
  const auto& profile = papyrix::board::HardwareIdentity::instance().profile();
  papyrix::hal::DisplayRecoveryPolicy policy;
  auto result = display.begin();
  while (result != papyrix::hal::Display::InitResult::Ok) {
    const auto action = policy.recordFailure();
    LOG_ERR(TAG, "Display initialization failed");
    if (action == papyrix::hal::DisplayRecoveryAction::ResetAndRetry) {
      result = display.recover();
      continue;
    }

    SdMan.end();
    papyrix::board::shutdownRecoveryRails(profile);
    papyrix::board::waitForRecoveryRetry(profile);
    papyrix::board::restoreRecoveryStorage(profile);
    if (!SdMan.begin()) LOG_ERR(TAG, "SD card did not recover");
    policy.recordSuccess();
    if (!papyrix::board::HardwareIdentity::instance().panelResolved())
      papyrix::board::selectPanel(papyrix::board::HardwareIdentity::instance());
    result = display.recover();
  }
  policy.recordSuccess();
}

void setupDisplayAndFonts(bool allReaderSizes = true) {
  initializeDisplayWithRecovery();
  renderer.begin();
  LOG_INF(TAG, "Display initialized");
  if (allReaderSizes) {
    renderer.insertFont(READER_FONT_ID_XSMALL, readerFontFamilyXSmall());
    renderer.insertFont(READER_FONT_ID, readerFontFamilySmall());
    renderer.insertFont(READER_FONT_ID_MEDIUM, readerFontFamilyMedium());
    renderer.insertFont(READER_FONT_ID_LARGE, readerFontFamilyLarge());
  } else {
    setupReaderFontForSize(static_cast<papyrix::Settings::FontSize>(papyrix::core.settings.fontSize));
  }
  renderer.insertFont(UI_FONT_ID, uiFontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.excludeExternalFont(UI_FONT_ID);
  renderer.excludeExternalFont(SMALL_FONT_ID);
  LOG_INF(TAG, "Fonts setup");
}

void applyThemeFonts() {
  Theme& theme = THEME_MANAGER.mutableCurrent();

  // Reset UI font to builtin first in case custom font loading fails
  theme.uiFontId = UI_FONT_ID;

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
      LOG_INF(TAG, "Reader font: %s (ID: %d)", fontFamilyName, customFontId);
    } else {
      LOG_ERR(TAG, "Theme requested custom font '%s' but it failed to load - using builtin", fontFamilyName);
    }
  }
}

void showErrorScreen(const char* message) {
  renderer.clearScreen(false);
  renderer.drawCenteredText(UI_FONT_ID, 100, message, true, BOLD);
  renderer.displayBuffer();
}

bool showEmergencyUpdateNotice() {
  auto& display = papyrix::core.display;
  const auto initResult = display.begin();
  const bool displayReady = initResult == papyrix::hal::Display::InitResult::Ok;
  if (papyrix::emergency_update::noticeActionFor(displayReady) ==
      papyrix::emergency_update::NoticeAction::ContinueHeadless) {
    LOG_WRN(TAG, "Emergency update notice unavailable: display result=%u", static_cast<unsigned>(initResult));
    return false;
  }

  renderer.begin();
  renderer.insertFont(UI_FONT_ID, uiFontFamily);
  renderer.excludeExternalFont(UI_FONT_ID);
  renderer.clearScreen(papyrix::emergency_update::kNoticeBackground);
  const int centerY = renderer.getScreenHeight() / 2;
  renderer.drawCenteredText(UI_FONT_ID, centerY - 24, papyrix::emergency_update::kNoticeTitle,
                            papyrix::emergency_update::kNoticeTextBlack, BOLD);
  renderer.drawCenteredText(UI_FONT_ID, centerY + 24, papyrix::emergency_update::kNoticeWarning,
                            papyrix::emergency_update::kNoticeTextBlack);
  renderer.displayBuffer(papyrix::hal::Display::FULL_REFRESH, papyrix::emergency_update::kTurnOffDuringRefresh);
  if (papyrix::emergency_update::kSleepAfterRender && !display.deepSleep()) return false;
  LOG_INF(TAG, "Emergency update notice displayed");
  return true;
}

// Track current boot mode for loop behavior
static papyrix::BootMode currentBootMode = papyrix::BootMode::UI;

// Initialize the hardware for both boot modes.
bool earlyInit() {
#if PAPYRIX_TARGET_XTEINK_C3
  inputManager.begin();
#endif

  auto& identity = papyrix::board::HardwareIdentity::instance();
  papyrix::board::selectBoard(identity);
  papyrix::core.battery.init();
  papyrix::core.usb.init(papyrix::core.battery);
  const auto& profile = identity.profile();

  const auto wakeup = getWakeupInfo();
  if (wakeup.usbColdBoot) {
    papyrix::hal::enterDeepSleepWithHardwareShutdown(true);
  }

  papyrix::board::preparePanelProbe(papyrix::board::HardwareIdentity::instance().profile());
  papyrix::board::selectPanel(identity);

  if (profile.storage.transport == papyrix::board::StorageTransport::Spi) {
    SPI.begin(profile.display.sclk, profile.storage.spiMiso, profile.display.mosi, profile.display.cs);
  }
  if (!SdMan.begin()) {
    LOG_ERR(TAG, "SD card initialization failed");
    setupDisplayAndFonts();
    showErrorScreen("SD card error");
    return false;
  }

  // Emergency firmware flash from SD card (force_update.bin) — blocking, runs before any UI init
  if (SdMan.exists(PAPYRIX_EMERGENCY_FW_FILE)) {
    auto& fw = papyrix::FirmwareUpdater::instance();
    fw.findFirmwareFile(PAPYRIX_EMERGENCY_FW_FILE);
    if (fw.beginUpdate()) {
      showEmergencyUpdateNotice();
      while (fw.pump()) {
        delay(1);
      }
    }
    SdMan.remove(PAPYRIX_EMERGENCY_FW_FILE);
    if (fw.progress().phase == papyrix::FirmwareUpdatePhase::Complete) {
      delay(2000);
      ESP.restart();
    }
  }

  // Load settings before wakeup verification - without this, a full power cycle
  // (no USB) resets RTC memory and the short power button setting is ignored
  papyrix::core.settings.loadFromFile();
  i18n::loadLocaleFromSD();
  rtcPowerButtonDurationMs = papyrix::core.settings.getPowerButtonDuration();

  if (wakeup.isPowerButton) {
    verifyWakeupLongPress(wakeup.resetReason);
  }

  LOG_INF(TAG, "Starting PapyriX version " PAPYRIX_VERSION);
  papyrix::crashdebug::logBootInfo(wakeup.resetReason);

  if (papyrix::board::HardwareIdentity::instance().board() == papyrix::board::BoardId::X4) {
    // Arduino 3.x requires the first read to attach the pin to the ADC bus
    // before per-pin attenuation can be configured.
    (void)analogRead(profile.battery.adcPin);
    analogSetPinAttenuation(profile.battery.adcPin, ADC_11db);
  }

  // Initialize internal flash filesystem for font storage
  if (!LittleFS.begin(false)) {
    LOG_ERR(TAG, "LittleFS mount failed, attempting format");
    if (!LittleFS.format() || !LittleFS.begin(false)) {
      LOG_ERR(TAG, "LittleFS recovery failed");
      showErrorScreen("Internal storage error");
      return false;
    }
    LOG_INF(TAG, "LittleFS formatted and mounted");
  } else {
    LOG_INF(TAG, "LittleFS mounted");
  }

  return true;
}

// Initialize UI mode - full state registration, all resources
void initUIMode() {
  LOG_INF(TAG, "Initializing UI mode");
  LOG_DBG(TAG, "[UI mode] Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Initialize theme and font managers (full)
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(papyrix::core.settings.themeName);
  THEME_MANAGER.createDefaultThemeFiles();
  LOG_INF(TAG, "Theme loaded: %s", THEME_MANAGER.currentThemeName());

  setupDisplayAndFonts();
  applyThemeFonts();

  // Show boot splash only on cold boot (not mode transition)
  const auto& preInitTransition = papyrix::getTransition();
  if (!preInitTransition.isValid()) {
    ui::BootView bootView;
    bootView.setStatus(tr(BOOTING));
    bootView.setDarkMode(THEME.invertedMode);
    ui::render(renderer, THEME, bootView);
  }

  // Register ALL states for UI mode
  stateMachine.registerState(&startupState);
  stateMachine.registerState(&homeState);
  stateMachine.registerState(&fileListState);
  stateMachine.registerState(&recentState);
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&settingsState);
  stateMachine.registerState(&networkState);
  stateMachine.registerState(&calibreSyncState);
  stateMachine.registerState(&appLauncherState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

  // Initialize core
  auto result = papyrix::core.init();
  if (!result.ok()) {
    LOG_ERR(TAG, "Init failed: %s", papyrix::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  LOG_INF(TAG, "State machine starting (UI mode)");
  mappedInputManager.setSettings(&papyrix::core.settings);
  ui::setFrontButtonLayout(papyrix::core.settings.frontButtonLayout);

  // Determine initial state - check for return from reader mode
  papyrix::StateId initialState = papyrix::StateId::Home;
  const auto& transition = papyrix::getTransition();

  if (transition.returnTo == papyrix::ReturnTo::FILE_MANAGER) {
    initialState = papyrix::StateId::FileList;
    LOG_INF(TAG, "Returning to FileList from Reader");
  } else if (transition.returnTo == papyrix::ReturnTo::RECENT) {
    initialState = papyrix::StateId::Recent;
    LOG_INF(TAG, "Returning to Recent from Reader");
  } else {
    LOG_INF(TAG, "Starting at Home");
  }

  stateMachine.init(papyrix::core, initialState);

  // Force initial render
  LOG_DBG(TAG, "Forcing initial render");
  stateMachine.update(papyrix::core);

  LOG_DBG(TAG, "[UI mode] After init - Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// Initialize Reader mode - minimal state registration, single font size
void initReaderMode() {
  LOG_INF(TAG, "Initializing READER mode");
  LOG_DBG(TAG, "[READER mode] Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Detect content type early to decide if we need custom fonts
  // XTC/XTCH files contain pre-rendered bitmaps and don't need fonts for page rendering
  const auto& transition = papyrix::getTransition();
  papyrix::ContentType contentType = papyrix::detectContentType(transition.bookPath);
  bool needsCustomFonts = (contentType != papyrix::ContentType::Xtc);

  // Initialize theme and font managers (minimal - no cache)
  FONT_MANAGER.init(renderer);
  THEME_MANAGER.loadTheme(papyrix::core.settings.themeName);
  // Skip createDefaultThemeFiles() - not needed in reader mode
  LOG_INF(TAG, "Theme loaded: %s (reader mode)", THEME_MANAGER.currentThemeName());

  setupDisplayAndFonts(false);

  if (needsCustomFonts) {
    applyThemeFonts();  // Custom fonts - skip for XTC/XTCH to save ~500KB+ RAM
  } else {
    LOG_DBG(TAG, "Skipping custom fonts for XTC content");
  }

  // Register ONLY states needed for Reader mode
  stateMachine.registerState(&readerState);
  stateMachine.registerState(&sleepState);
  stateMachine.registerState(&errorState);

  // Initialize core
  auto result = papyrix::core.init();
  if (!result.ok()) {
    LOG_ERR(TAG, "Init failed: %s", papyrix::errorToString(result.err));
    showErrorScreen("Core init failed");
    return;
  }

  LOG_INF(TAG, "State machine starting (READER mode)");
  mappedInputManager.setSettings(&papyrix::core.settings);
  ui::setFrontButtonLayout(papyrix::core.settings.frontButtonLayout);

  if (transition.bookPath[0] != '\0') {
    // Copy path to shared buffer for ReaderState to consume
    strncpy(papyrix::core.buf.path, transition.bookPath, sizeof(papyrix::core.buf.path) - 1);
    papyrix::core.buf.path[sizeof(papyrix::core.buf.path) - 1] = '\0';
    LOG_INF(TAG, "Opening book: %s", papyrix::core.buf.path);
  } else {
    // No book path - fall back to UI mode to avoid boot loop
    LOG_ERR(TAG, "No book path in transition, falling back to UI");
    initUIMode();
    return;
  }

  stateMachine.init(papyrix::core, papyrix::StateId::Reader);

  // Force initial render
  LOG_DBG(TAG, "Forcing initial render");
  stateMachine.update(papyrix::core);

  LOG_DBG(TAG, "[READER mode] After init - Free heap: %lu, Max block: %lu", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

void setup() {
  papyrix::board::releaseDeepSleepHolds();
  papyrix::board::earlyInit(papyrix::board::bootProfile());
  // Let USB Serial/JTAG and the host enumerate before recovery checks.
  delay(kSerialEnumerationDelayMs);
  Serial.begin(kSerialBaudRate);
#ifdef ENABLE_SERIAL_LOG
  logSerial.setTxTimeoutMs(kSerialTxTimeoutMs);
#endif

#if PAPYRIX_X4PRO_CHARACTERIZE
  papyrix::diagnostics::beginX4ProCharacterization();
  return;
#endif

  // Early initialization (common to both modes)
  if (!earlyInit()) {
    return;  // Critical failure
  }

  // Detect boot mode from RTC memory or settings
  currentBootMode = papyrix::detectBootMode();
  papyrix::core.bootMode = currentBootMode;

  if (currentBootMode == papyrix::BootMode::READER) {
    initReaderMode();
  } else {
    initUIMode();
  }

  // Ensure we're not still holding the power button before leaving setup
  if (waitForPowerRelease()) {
    papyrix::core.input.suppressPowerUntilRelease();
  }
}

void loop() {
#if PAPYRIX_X4PRO_CHARACTERIZE
  papyrix::diagnostics::updateX4ProCharacterization();
  return;
#endif

  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  static papyrix::hal::usb_policy::StateTracker x4UsbState;
  if (papyrix::board::HardwareIdentity::instance().board() == papyrix::board::BoardId::X4) {
    const bool usbConnected = papyrix::core.usb.isConnected();
    if (x4UsbState.update(usbConnected)) {
      LOG_INF(TAG, "X4 USB state changed: %s", usbConnected ? "connected" : "disconnected");
      if (stateMachine.isInState(papyrix::StateId::Home)) {
        homeState.onUsbStateChanged(papyrix::core);
      }
    }
  }

  // One short fuel-gauge step per tick while the design-capacity load runs.
  papyrix::core.battery.serviceDesignCapacity();

  inputManager.update();

  if (!papyrix::core.cpu.isThrottled() && millis() - lastMemPrint >= 10000) {
    LOG_DBG(TAG, "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Poll input and push events to queue
  papyrix::core.input.poll();

  // Auto-sleep after inactivity
  const auto autoSleepTimeout = papyrix::core.settings.getAutoSleepTimeoutMs();
  const bool wifiActive = papyrix::core.wifi.isConnected() || papyrix::core.wifi.isAPMode();
  if (wifiActive) {
    papyrix::core.input.resetIdleTimer();
  }
  if (autoSleepTimeout > 0 && papyrix::core.input.idleTimeMs() >= autoSleepTimeout) {
    papyrix::core.cpu.unthrottle();
    LOG_INF(TAG, "Auto-sleep after %lu ms idle", autoSleepTimeout);
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

    if (papyrix::core.input.powerSuppressed()) {
      // hal::Input clears the flag when it observes the release.
    } else if (inputManager.isPressed(InputManager::BTN_POWER)) {
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
  if (currentBootMode == papyrix::BootMode::READER) {
    if (papyrix::core.input.idleTimeMs() >= kIdlePowerSavingMs) {
      papyrix::core.cpu.throttle();
    } else {
      papyrix::core.cpu.unthrottle();
    }
  }

  // Update state machine (handles transitions and rendering)
  const unsigned long activityStartTime = millis();
  stateMachine.update(papyrix::core);
  const unsigned long activityDuration = millis() - activityStartTime;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG(TAG, "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // Increase delay after idle to save power (~4x less CPU load)
  // Idea: https://github.com/crosspoint-reader/crosspoint-reader/commit/0991782 by @ngxson (https://github.com/ngxson)
  delay(papyrix::core.cpu.loopDelayMs());
}
