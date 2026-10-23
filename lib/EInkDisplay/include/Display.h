#pragma once
#include <Arduino.h>
#include <DisplayController.h>
#include <SPI.h>
#include <TargetConfig.h>

#if __has_include(<esp_attr.h>)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

namespace papyrix::hal {

class Display {
 public:
  Display();
  ~Display() = default;
  enum class InitResult : uint8_t { Ok, OutOfMemory = 2, UnsupportedPanel };
  // Refresh modes (guarded to avoid redefinition in test builds)
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  void setBackgroundHint(bool darkBackground);

  InitResult begin();
  InitResult recover();
  bool supportsGrayscale() const;

  // Legacy compile-time dimensions kept for compatibility with code that
  // still references them. Runtime callers should use the getDisplay*()
  // accessors below since geometry switches at runtime when X3 is detected.
  static constexpr uint16_t DISPLAY_WIDTH = papyrix::board::kTargetMaxDisplayWidth;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
  static constexpr uint16_t X3_DISPLAY_WIDTH = 792;
  static constexpr uint16_t X3_DISPLAY_HEIGHT = 528;
  static constexpr uint16_t X3_DISPLAY_WIDTH_BYTES = X3_DISPLAY_WIDTH / 8;
  static constexpr uint32_t X3_BUFFER_SIZE = X3_DISPLAY_WIDTH_BYTES * X3_DISPLAY_HEIGHT;
  static constexpr uint32_t MAX_BUFFER_SIZE = papyrix::board::kTargetFrameBufferBytes;

  // Runtime dimensions
  uint16_t getDisplayWidth() const { return displayWidth; }
  uint16_t getDisplayHeight() const { return displayHeight; }
  uint16_t getDisplayWidthBytes() const { return displayWidthBytes; }
  uint32_t getBufferSize() const { return bufferSize; }

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  // Like drawImage but only writes black pixels (white pixels remain as-is in the framebuffer).
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void swapBuffers();
#endif
  void setFramebuffer(const uint8_t* bwBuffer) const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
#endif

  // turnOffScreen: Power down display after refresh. Used for sunlight fading fix
  // on SSD1677 displays without resin protection (XTEINK X4).
  void displayBuffer(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  void displayBufferDriveAll(bool turnOffScreen = false);
  // EXPERIMENTAL: Windowed update - display only a rectangular region
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool turnOffScreen = false);
  void displayGrayBuffer(bool turnOffScreen = false);

  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);

  // Hint the X3 policy to run a one-shot full resync on next update.
  void requestResync(uint8_t settlePasses = 0);

  // debug function
  void grayscaleRevert();

  // LUT control
  void setCustomLUT(bool enabled, const unsigned char* lutData = nullptr);

  // Power management
  bool deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const { return frameBuffer; }

  // Save the current framebuffer to a PBM file (desktop/test builds only)
  void saveFrameBufferAsPBM(const char* filename);

 private:
  void setDisplayDimensions(uint16_t width, uint16_t height);

  // Pin configuration
  int8_t _sclk, _mosi, _cs, _dc, _rst, _busy;

  // Runtime display geometry
  uint16_t displayWidth = DISPLAY_WIDTH;
  uint16_t displayHeight = DISPLAY_HEIGHT;
  uint16_t displayWidthBytes = DISPLAY_WIDTH_BYTES;
  uint32_t bufferSize = BUFFER_SIZE;
  bool initialized_ = false;

  // X3 state machine
  bool _x3Mode = false;
  papyrix::eink::DisplayController displayController_ = papyrix::eink::DisplayController::SSD1677;
  bool _x3RedRamSynced = false;
  enum class X3LutSet : uint8_t { NONE, FULL, HALF, TURBO, IMG, GRAY };
  X3LutSet _x3LoadedLuts = X3LutSet::NONE;
  struct X3GrayState {
    bool lastBaseWasPartial = false;
    bool lsbValid = false;
  };
  X3GrayState _x3GrayState;
  uint8_t _x3InitialFullSyncsRemaining = 0;
  bool _x3ForceFullSyncNext = false;
  uint8_t _x3ForcedConditionPassesNext = 0;

#if PAPYRIX_TARGET_XTEINK_C3
  uint8_t frameBuffer0[MAX_BUFFER_SIZE];
#else
  uint8_t* frameBuffer0 = nullptr;
#endif
  uint8_t* frameBuffer;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
#if PAPYRIX_TARGET_XTEINK_C3
  uint8_t frameBuffer1[MAX_BUFFER_SIZE];
#else
  uint8_t* frameBuffer1 = nullptr;
#endif
  uint8_t* frameBufferActive;
#endif

  // SPI settings
  SPISettings spiSettings;

  // State
  bool isScreenOn;
  bool ssd1677BaselineValid_ = false;
  bool ssd1677RefreshSucceeded_ = false;
  bool customLutActive;
  bool inGrayscaleMode;
  bool drawGrayscale;

  // Low-level display control
  void resetDisplay();
  void IRAM_ATTR sendCommand(uint8_t command);
  void IRAM_ATTR sendData(uint8_t data);
  void IRAM_ATTR sendData(const uint8_t* data, uint16_t length);
  void sendDataBatchBegin();
  void sendDataBatchEnd();
  void waitForRefresh(const char* comment = nullptr);
  bool waitWhileBusy(const char* comment = nullptr);
  void initDisplayController();

  // Low-level display operations
  void IRAM_ATTR setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void IRAM_ATTR writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
  void IRAM_ATTR writeRamBufferInverted(uint8_t ramBuffer, const uint8_t* data, uint32_t size);
};

}  // namespace papyrix::hal
