#include "SleepState.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <CoverHelpers.h>
#include <Display.h>
#include <Epub.h>
#include <Fb2.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Html.h>
#include <I18n.h>
#include <InputManager.h>
#include <LittleFS.h>
#include <Logging.h>
#include <Markdown.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cstring>
#include <string>
#include <vector>

#include "../ThemeManager.h"
#include "../config.h"
#include "../core/Core.h"
#include "../hal/Power.h"
#include "../images/PapyrixLogo.h"
#include "../ui/views/BootSleepViews.h"

extern InputManager inputManager;
extern uint16_t rtcPowerButtonDurationMs;

#define TAG "SLEEP"
static constexpr uint32_t POWER_RELEASE_TIMEOUT_MS = 5000;

namespace papyrix {

SleepState::SleepState(GfxRenderer& renderer) : renderer_(renderer) {}

void SleepState::enter(Core& core) {
  // Keep Page only applies in READER mode (book page is on screen). In UI mode
  // it falls back to the Light sleep screen so menus/home are never frozen.
  // Effective mode is local only — never mutate the saved setting.
  const bool keepPageRequested = (core.settings.sleepScreen == Settings::SleepKeepPage);
  const bool keepPage = keepPageRequested && (core.bootMode == BootMode::READER);
  const uint8_t effectiveSleepScreen =
      (keepPageRequested && !keepPage) ? Settings::SleepLight : core.settings.sleepScreen;

  if (keepPage) {
    // Standard "going to sleep" feedback (SLEEPING wipe), then restore the book
    // page so deep sleep freezes the page — not the intermediate SLEEPING screen.
    LOG_INF(TAG, "SleepState::enter - Keep Page (READER): SLEEPING then restore page");

    const size_t bufSize = renderer_.getBufferSize();
    uint8_t* pageSnap = nullptr;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= bufSize) {
      pageSnap = static_cast<uint8_t*>(malloc(bufSize));
    }

    if (pageSnap) {
      memcpy(pageSnap, renderer_.getFrameBuffer(), bufSize);

      renderSleepProgress();

      // Restore book page into the active framebuffer and lock it for deep sleep
      memcpy(renderer_.getFrameBuffer(), pageSnap, bufSize);
      free(pageSnap);
      pageSnap = nullptr;
      renderer_.displayBuffer(papyrix::hal::Display::HALF_REFRESH);
    } else {
      // OOM: skip intermediate SLEEPING and lock whatever is already on screen
      LOG_WRN(TAG, "Keep Page: no RAM for page snapshot (%u bytes), locking current buffer",
              static_cast<unsigned>(bufSize));
      renderer_.displayBuffer(papyrix::hal::Display::HALF_REFRESH);
    }
  } else {
    if (keepPageRequested) {
      LOG_INF(TAG, "SleepState::enter - Keep Page outside READER mode, using Light");
    } else {
      LOG_INF(TAG, "SleepState::enter - rendering sleep screen");
    }

    renderSleepProgress();

    switch (effectiveSleepScreen) {
      case Settings::SleepCustom:
        renderCustomSleepScreen(core);
        break;
      case Settings::SleepCover:
        renderCoverSleepScreen(core);
        break;
      default:
        renderDefaultSleepScreen(effectiveSleepScreen);
        break;
    }
  }

  rtcPowerButtonDurationMs = core.settings.getPowerButtonDuration();
  const bool externalPower = core.usb.isConnected();

  // A stuck power switch must not sleep the device: deep sleep arms an
  // active-low GPIO wake, so an asserted button would storm wake cycles.
  if (waitForPowerRelease(POWER_RELEASE_TIMEOUT_MS)) {
    LOG_ERR(TAG, "Power button stuck; sleep cancelled");
    snprintf(core.buf.text, sizeof(core.buf.text), "Power button stuck. Sleep cancelled.");
    core.input.resetIdleTimer();
    return;
  }

  if (!core.display.deepSleep()) {
    LOG_ERR(TAG, "Display power-off failed; sleep cancelled");
    snprintf(core.buf.text, sizeof(core.buf.text), "Display power-off failed. Sleep cancelled.");
    core.input.resetIdleTimer();
    return;
  }
  core.frontLight.shutdown();

  if (core.wifi.isInitialized()) {
    core.wifi.shutdown();
  }

  LittleFS.end();
  if (!core.battery.finishDesignCapacity()) {
    LOG_ERR(TAG, "Fuel-gauge close-out failed; sleeping anyway");
  }

  // Teardown is done, so a still-asserted wake source cannot enter deep sleep
  // safely, and un-doing teardown is not sound either. Restart instead: boot
  // then reaches the bounded check above, which keeps the device awake while
  // the button stays held. No wake storm, and the sleep screen never hangs.
  if (waitForPowerRelease(POWER_RELEASE_TIMEOUT_MS)) {
    LOG_ERR(TAG, "Power button held through teardown; restarting");
    esp_restart();
  }

  LOG_INF(TAG, "Entering deep sleep (external power: %s)", externalPower ? "yes" : "no");
  hal::enterDeepSleepWithHardwareShutdown(externalPower);
}

void SleepState::exit(Core& core) { LOG_ERR(TAG, "SleepState::exit (unexpected)"); }

StateTransition SleepState::update(Core& core) { return StateTransition::to(StateId::Error); }

void SleepState::renderSleepProgress() const {
  renderer_.clearScreen(0x00);
  renderer_.displayBuffer(papyrix::hal::Display::FAST_REFRESH);

  ui::BootView progressView;
  progressView.setStatus(tr(SLEEPING));
  ui::render(renderer_, THEME, progressView);
}

void SleepState::renderDefaultSleepScreen(uint8_t sleepMode) const {
  ui::SleepView sleepView;
  sleepView.setLogo(PapyrixLogo, PapyrixLogoSize, PapyrixLogoSize);
  sleepView.setDarkMode(sleepMode != Settings::SleepLight);
  ui::render(renderer_, THEME, sleepView);
}

void SleepState::renderCustomSleepScreen(const Core& core) const {
  // Check if we have a /sleep directory
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    std::vector<std::string> files;
    char name[256];  // FAT32 LFN max is 255 chars; reduced from 500 to save stack
    // collect all valid BMP files
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) {
        file.close();
        continue;
      }
      file.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        file.close();
        continue;
      }

      if (!FsHelpers::isBmpFile(filename)) {
        LOG_DBG(TAG, "Skipping non-.bmp file name: %s", name);
        file.close();
        continue;
      }
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG(TAG, "Skipping invalid BMP file: %s", name);
        file.close();
        continue;
      }
      files.emplace_back(filename);
      file.close();
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Generate a random number between 0 and numFiles-1
      const auto randomFileIndex = random(numFiles);
      const auto filename = "/sleep/" + files[randomFileIndex];
      FsFile file;
      if (SdMan.openFileForRead("SLP", filename, file)) {
        LOG_INF(TAG, "Randomly loading: /sleep/%s", files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          dir.close();
          return;
        }
      }
    }
  }
  if (dir) dir.close();

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  FsFile file;
  if (SdMan.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_INF(TAG, "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen(core.settings.sleepScreen);
}

void SleepState::renderCoverSleepScreen(Core& core) const {
  if (core.settings.lastBookPath[0] == '\0') {
    return renderDefaultSleepScreen(core.settings.sleepScreen);
  }

  std::string coverBmpPath;
  const char* bookPath = core.settings.lastBookPath;
  const char* cacheDir = core.device.renderCacheDir();

  // Generate cover BMP based on file type (creates temporary wrapper to generate cover)
  if (FsHelpers::isXtcFile(bookPath)) {
    Xtc xtc(bookPath, cacheDir);
    if (xtc.load() && xtc.generateCoverBmp()) {
      coverBmpPath = xtc.getCoverBmpPath();
    }
  } else if (FsHelpers::isTxtFile(bookPath)) {
    Txt txt(bookPath, cacheDir);
    if (txt.load() && txt.generateCoverBmp(true)) {
      coverBmpPath = txt.getCoverBmpPath();
    }
  } else if (FsHelpers::isMarkdownFile(bookPath)) {
    Markdown md(bookPath, cacheDir);
    if (md.load() && md.generateCoverBmp(true)) {
      coverBmpPath = md.getCoverBmpPath();
    }
  } else if (FsHelpers::isEpubFile(bookPath)) {
    Epub epub(bookPath, cacheDir);
    if (epub.load() && epub.generateCoverBmp(true)) {
      coverBmpPath = epub.getCoverBmpPath();
    }
  } else if (FsHelpers::isFb2File(bookPath)) {
    Fb2 fb2(bookPath, cacheDir);
    if (fb2.load() && fb2.generateCoverBmp(true)) {
      coverBmpPath = fb2.getCoverBmpPath();
    }
  } else if (FsHelpers::isHtmlFile(bookPath)) {
    Html html(bookPath, cacheDir);
    if (html.load() && html.generateCoverBmp(true)) {
      coverBmpPath = html.getCoverBmpPath();
    }
  }

  if (coverBmpPath.empty()) {
    LOG_DBG(TAG, "No cover BMP available");
    return renderDefaultSleepScreen(core.settings.sleepScreen);
  }

  FsFile file;
  if (SdMan.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen(core.settings.sleepScreen);
}

void SleepState::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  const auto pageWidth = renderer_.getScreenWidth();
  const auto pageHeight = renderer_.getScreenHeight();

  auto rect = CoverHelpers::calculateCenteredRect(bitmap.getWidth(), bitmap.getHeight(), 0, 0, pageWidth, pageHeight);

  renderer_.clearScreen();
  renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
  renderer_.displayBuffer(papyrix::hal::Display::HALF_REFRESH);

  if (renderer_.supportsGrayscale() && bitmap.hasGreyscale()) {
    bitmap.rewindToData();
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer_.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer_.copyGrayscaleMsbBuffers();

    renderer_.displayGrayBuffer();
    renderer_.setRenderMode(GfxRenderer::BW);

    // Restore BW frame buffer and clean up RED RAM so e-ink controller
    // doesn't show grayscale residue as ghosting during deep sleep
    bitmap.rewindToData();
    renderer_.clearScreen();
    renderer_.drawBitmap(bitmap, rect.x, rect.y, rect.width, rect.height);
    renderer_.cleanupGrayscaleWithFrameBuffer();
  }
}

// Returns true only when the button is still held after timeoutMs elapsed.
// timeoutMs 0 waits without a bound.
bool SleepState::waitForPowerRelease(uint32_t timeoutMs) const {
  inputManager.update();
  const uint32_t startedMs = millis();
  while (inputManager.isPressed(InputManager::BTN_POWER)) {
    if (timeoutMs != 0 && millis() - startedMs >= timeoutMs) {
      LOG_ERR(TAG, "Power button still held after %lu ms", static_cast<unsigned long>(timeoutMs));
      return true;
    }
    delay(50);
    inputManager.update();
  }
  return false;
}

}  // namespace papyrix
