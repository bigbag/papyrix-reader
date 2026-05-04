#include "EInkDisplay.h"

#include <Logging.h>

#define TAG "DISPLAY"

#include <cstring>
#include <fstream>
#include <vector>

// SSD1677 command definitions
// Initialization and reset
#define CMD_SOFT_RESET 0x12             // Soft reset
#define CMD_BOOSTER_SOFT_START 0x0C     // Booster soft-start control
#define CMD_DRIVER_OUTPUT_CONTROL 0x01  // Driver output control
#define CMD_BORDER_WAVEFORM 0x3C        // Border waveform control
#define CMD_TEMP_SENSOR_CONTROL 0x18    // Temperature sensor control

// RAM and buffer management
#define CMD_DATA_ENTRY_MODE 0x11     // Data entry mode
#define CMD_SET_RAM_X_RANGE 0x44     // Set RAM X address range
#define CMD_SET_RAM_Y_RANGE 0x45     // Set RAM Y address range
#define CMD_SET_RAM_X_COUNTER 0x4E   // Set RAM X address counter
#define CMD_SET_RAM_Y_COUNTER 0x4F   // Set RAM Y address counter
#define CMD_WRITE_RAM_BW 0x24        // Write to BW RAM (current frame)
#define CMD_WRITE_RAM_RED 0x26       // Write to RED RAM (used for fast refresh)
#define CMD_AUTO_WRITE_BW_RAM 0x46   // Auto write BW RAM
#define CMD_AUTO_WRITE_RED_RAM 0x47  // Auto write RED RAM

// Display update and refresh
#define CMD_DISPLAY_UPDATE_CTRL1 0x21  // Display update control 1
#define CMD_DISPLAY_UPDATE_CTRL2 0x22  // Display update control 2
#define CMD_MASTER_ACTIVATION 0x20     // Master activation
#define CTRL1_NORMAL 0x00              // Normal mode - compare RED vs BW for partial
#define CTRL1_BYPASS_RED 0x40          // Bypass RED RAM (treat as 0) - for full refresh

// LUT and voltage settings
#define CMD_WRITE_LUT 0x32       // Write LUT
#define CMD_GATE_VOLTAGE 0x03    // Gate voltage
#define CMD_SOURCE_VOLTAGE 0x04  // Source voltage
#define CMD_WRITE_VCOM 0x2C      // Write VCOM
#define CMD_WRITE_TEMP 0x1A      // Write temperature

// Power management
#define CMD_DEEP_SLEEP 0x10  // Deep sleep

// Custom LUT for fast refresh
const unsigned char lut_grayscale[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0x54, 0x54, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0xAA, 0xA0, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xA2, 0x22, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x00, 0x00, 0x00, 0x00, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

const unsigned char lut_grayscale_revert[] PROGMEM = {
    // 00 black/white
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 10 gray
    0x54, 0x54, 0x54, 0x54, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 01 light gray
    0xA8, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // 11 dark gray
    0xFC, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // L4 (VCOM)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // TP/RP groups (global timing)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G0: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x01,  // G1: A=1 B=1 C=1 D=1 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G2: A=0 B=0 C=0 D=0 RP=0 (4 frames)
    0x01, 0x01, 0x01, 0x01, 0x00,  // G3: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G4: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G5: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G6: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G7: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G8: A=0 B=0 C=0 D=0 RP=0
    0x00, 0x00, 0x00, 0x00, 0x00,  // G9: A=0 B=0 C=0 D=0 RP=0

    // Frame rate
    0x8F, 0x8F, 0x8F, 0x8F, 0x8F,

    // Voltages (VGH, VSH1, VSH2, VSL, VCOM)
    0x17, 0x41, 0xA8, 0x32, 0x30,

    // Reserved
    0x00, 0x00};

// X3 reverse-exact full refresh LUTs (42 bytes each)
const uint8_t lut_x3_vcom_full[] PROGMEM = {0x00, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00,
                                            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_full[] PROGMEM = {0x20, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00,
                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_full[] PROGMEM = {0xAA, 0x06, 0x02, 0x06, 0x06, 0x01, 0x80, 0x05, 0x01, 0x00, 0x00,
                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_full[] PROGMEM = {0x55, 0x06, 0x02, 0x06, 0x06, 0x01, 0x40, 0x05, 0x01, 0x00, 0x00,
                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_full[] PROGMEM = {0x10, 0x06, 0x02, 0x06, 0x06, 0x01, 0x00, 0x05, 0x01, 0x00, 0x00,
                                          0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 dedicated grayscale LUTs — tuned drive strengths for 4-level gray.
// All entries share the same single-phase timing so the controller scans
// every row with consistent gate timing. Source voltages differ per transition:
//   VCOM: GND (stable common electrode reference)
//   BB:   GND (active hold — prevents floating source crosstalk)
//   WW:   brief VDL pulse (dark gray)
//   BW:   moderate VDL pulse (light gray)
//   WB:   GND (active hold — unused transition)
const uint8_t lut_x3_vcom_gray[] PROGMEM = {0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_gray[] PROGMEM = {
    // Dark gray: VS=0x20 → GND,VDL(2),GND,GND — brief pulse (sub-phase B)
    0x20, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_gray[] PROGMEM = {
    // Light gray: VS=0x80 → VDL(3),GND,GND,GND — subtle pulse (sub-phase A, TP0=3)
    0x80, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_gray[] PROGMEM = {
    // Active GND hold: VS=0x00 → all GND, matching timing
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_gray[] PROGMEM = {
    // Active GND hold: VS=0x00 → all GND, matching timing
    0x00, 0x03, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 stock image-write LUTs
const uint8_t lut_x3_vcom_img[] PROGMEM = {0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x00, 0x0C, 0x02, 0x07, 0x02,
                                           0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_img[] PROGMEM = {0xA8, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x44, 0x0C, 0x02, 0x07, 0x02,
                                         0x01, 0x04, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_img[] PROGMEM = {0x80, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x62, 0x0C, 0x02, 0x07, 0x02,
                                         0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_img[] PROGMEM = {0x88, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x60, 0x0C, 0x02, 0x07, 0x02,
                                         0x01, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_img[] PROGMEM = {0x00, 0x08, 0x0B, 0x02, 0x03, 0x01, 0x4A, 0x0C, 0x02, 0x07, 0x02,
                                         0x01, 0x88, 0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// X3 AA LUTs: fast partial-style set tuned to preserve X3 polarity behavior.
const uint8_t lut_x3_vcom_fast[] PROGMEM = {0x00, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_ww_fast[] PROGMEM = {0x60, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bw_fast[] PROGMEM = {0x20, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_wb_fast[] PROGMEM = {0x10, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t lut_x3_bb_fast[] PROGMEM = {0x90, 0x18, 0x18, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void EInkDisplay::setDisplayDimensions(uint16_t width, uint16_t height) {
  displayWidth = width;
  displayHeight = height;
  displayWidthBytes = width / 8;
  bufferSize = displayWidthBytes * height;
  _x3Mode = false;
}

void EInkDisplay::setDisplayX3() {
  setDisplayDimensions(X3_DISPLAY_WIDTH, X3_DISPLAY_HEIGHT);
  _x3Mode = true;
}

void EInkDisplay::requestResync(uint8_t settlePasses) {
  _x3ForceFullSyncNext = _x3Mode;
  _x3ForcedConditionPassesNext = _x3Mode ? settlePasses : 0;
}

EInkDisplay::EInkDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy)
    : _sclk(sclk),
      _mosi(mosi),
      _cs(cs),
      _dc(dc),
      _rst(rst),
      _busy(busy),
      frameBuffer(nullptr),
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
      frameBufferActive(nullptr),
#endif
      isScreenOn(false),
      customLutActive(false),
      inGrayscaleMode(false),
      drawGrayscale(false) {
  LOG_INF(TAG, "Constructor called");
  LOG_INF(TAG, "SCLK=%d, MOSI=%d, CS=%d, DC=%d, RST=%d, BUSY=%d", sclk, mosi, cs, dc, rst, busy);
}

void EInkDisplay::begin() {
  LOG_INF(TAG, "begin() called");

  // Reset isScreenOn flag to ensure display is properly initialized.
  // Especially important after deep-sleep wake-up where the display
  // controller needs to be treated as a fresh initialization.
  isScreenOn = false;

  frameBuffer = frameBuffer0;
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  frameBufferActive = frameBuffer1;
#endif

  // Initialize to white
  memset(frameBuffer0, 0xFF, bufferSize);
  _x3RedRamSynced = false;
  _x3InitialFullSyncsRemaining = _x3Mode ? 2 : 0;
  _x3ForceFullSyncNext = false;
  _x3ForcedConditionPassesNext = 0;
  _x3GrayState = {};
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  LOG_INF(TAG, "Static frame buffer (%lu bytes)", bufferSize);
#else
  memset(frameBuffer1, 0xFF, bufferSize);
  LOG_INF(TAG, "Static frame buffers (2 x %lu bytes)", bufferSize);
#endif

  LOG_INF(TAG, "Initializing e-ink display driver...");

  // Initialize SPI with custom pins. X3 controller doesn't tolerate the X4's 40 MHz.
  SPI.begin(_sclk, -1, _mosi, _cs);
  const uint32_t spiHz = _x3Mode ? 10000000 : 40000000;
  spiSettings = SPISettings(spiHz, MSBFIRST, SPI_MODE0);
  LOG_INF(TAG, "SPI initialized at %lu Hz, Mode 0", spiHz);

  // Setup GPIO pins
  pinMode(_cs, OUTPUT);
  pinMode(_dc, OUTPUT);
  pinMode(_rst, OUTPUT);
  pinMode(_busy, INPUT);

  digitalWrite(_cs, HIGH);
  digitalWrite(_dc, HIGH);

  LOG_INF(TAG, "GPIO pins configured");

  // Reset display
  resetDisplay();

  // Initialize display controller
  initDisplayController();

  LOG_INF(TAG, "E-ink display driver initialized");
}

// ============================================================================
// Low-level display control methods
// ============================================================================

void EInkDisplay::resetDisplay() {
  LOG_DBG(TAG, "Resetting display...");
  digitalWrite(_rst, HIGH);
  delay(20);
  digitalWrite(_rst, LOW);
  delay(2);
  digitalWrite(_rst, HIGH);
  delay(20);
  LOG_DBG(TAG, "Display reset complete");
  if (_x3Mode) {
    delay(50);
  }
}

void EInkDisplay::waitForRefresh(const char* comment) {
  unsigned long start = millis();
  if (!_x3Mode) {
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 30000) break;
    }
  } else {
    bool sawLow = false;
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 1000) break;
    }
    if (digitalRead(_busy) == LOW) {
      sawLow = true;
      while (digitalRead(_busy) == LOW) {
        delay(1);
        if (millis() - start > 30000) break;
      }
    }
    if (!sawLow) return;
  }
  if (comment) {
    LOG_DBG(TAG, "Refresh done: %s (%lu ms)", comment, millis() - start);
  }
}

void EInkDisplay::sendCommand(uint8_t command) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, LOW);  // Command mode
  digitalWrite(_cs, LOW);  // Select chip
  SPI.transfer(command);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(uint8_t data) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);  // Data mode
  digitalWrite(_cs, LOW);   // Select chip
  SPI.transfer(data);
  digitalWrite(_cs, HIGH);  // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendData(const uint8_t* data, uint16_t length) {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);       // Data mode
  digitalWrite(_cs, LOW);        // Select chip
  SPI.writeBytes(data, length);  // Transfer all bytes
  digitalWrite(_cs, HIGH);       // Deselect chip
  SPI.endTransaction();
}

void EInkDisplay::sendDataBatchBegin() {
  SPI.beginTransaction(spiSettings);
  digitalWrite(_dc, HIGH);
  digitalWrite(_cs, LOW);
}

void EInkDisplay::sendDataBatchEnd() {
  digitalWrite(_cs, HIGH);
  SPI.endTransaction();
}

void EInkDisplay::waitWhileBusy(const char* comment) {
  unsigned long start = millis();
  if (!_x3Mode) {
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 10000) {
        LOG_ERR(TAG, "Timeout waiting for busy%s", comment ? comment : "");
        break;
      }
    }
  } else {
    bool sawLow = false;
    while (digitalRead(_busy) == HIGH) {
      delay(1);
      if (millis() - start > 1000) break;
    }
    if (digitalRead(_busy) == LOW) {
      sawLow = true;
      while (digitalRead(_busy) == LOW) {
        delay(1);
        if (millis() - start > 30000) break;
      }
    }
    if (!sawLow) return;
  }
  if (comment) {
    LOG_DBG(TAG, "Wait complete: %s (%lu ms)", comment, millis() - start);
  }
}

void EInkDisplay::initDisplayController() {
  if (_x3Mode) {
    sendCommand(0x00);
    sendData(0x3F);
    sendData(0x08);
    sendCommand(0x61);
    sendData(0x03);
    sendData(0x18);
    sendData(0x02);
    sendData(0x58);
    sendCommand(0x65);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendData(0x00);
    sendCommand(0x03);
    sendData(0x1D);
    sendCommand(0x01);
    sendData(0x07);
    sendData(0x17);
    sendData(0x3F);
    sendData(0x3F);
    sendData(0x17);
    sendCommand(0x82);
    sendData(0x1D);
    sendCommand(0x06);
    sendData(0x25);
    sendData(0x25);
    sendData(0x3C);
    sendData(0x37);
    sendCommand(0x30);
    sendData(0x09);
    sendCommand(0xE1);
    sendData(0x02);
    sendCommand(0x20);
    sendData(lut_x3_vcom_full, 42);
    sendCommand(0x21);
    sendData(lut_x3_ww_full, 42);
    sendCommand(0x22);
    sendData(lut_x3_bw_full, 42);
    sendCommand(0x23);
    sendData(lut_x3_wb_full, 42);
    sendCommand(0x24);
    sendData(lut_x3_bb_full, 42);
    isScreenOn = false;
    LOG_INF(TAG, "X3 controller initialized");
    return;
  }

  LOG_INF(TAG, "Initializing SSD1677 controller...");

  const uint8_t TEMP_SENSOR_INTERNAL = 0x80;

  // Soft reset
  sendCommand(CMD_SOFT_RESET);
  waitWhileBusy(" CMD_SOFT_RESET");

  // Temperature sensor control (internal)
  sendCommand(CMD_TEMP_SENSOR_CONTROL);
  sendData(TEMP_SENSOR_INTERNAL);

  // Booster soft-start control (GDEQ0426T82 specific values)
  sendCommand(CMD_BOOSTER_SOFT_START);
  sendData(0xAE);
  sendData(0xC7);
  sendData(0xC3);
  sendData(0xC0);
  sendData(0x40);

  // Driver output control: set display height and scan direction
  sendCommand(CMD_DRIVER_OUTPUT_CONTROL);
  sendData((displayHeight - 1) % 256);  // gates A0..A7 (low byte)
  sendData((displayHeight - 1) / 256);  // gates A8..A9 (high byte)
  sendData(0x02);                       // SM=1 (interlaced), TB=0

  // Border waveform control
  sendCommand(CMD_BORDER_WAVEFORM);
  sendData(0x01);

  // Set up full screen RAM area
  setRamArea(0, 0, displayWidth, displayHeight);

  LOG_DBG(TAG, "Clearing RAM buffers...");
  sendCommand(CMD_AUTO_WRITE_BW_RAM);  // Auto write BW RAM
  sendData(0xF7);
  waitWhileBusy(" CMD_AUTO_WRITE_BW_RAM");

  sendCommand(CMD_AUTO_WRITE_RED_RAM);  // Auto write RED RAM
  sendData(0xF7);                       // Fill with white pattern
  waitWhileBusy(" CMD_AUTO_WRITE_RED_RAM");

  LOG_INF(TAG, "SSD1677 controller initialized");
}

void EInkDisplay::setRamArea(const uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  constexpr uint8_t DATA_ENTRY_X_INC_Y_DEC = 0x01;

  // Reverse Y coordinate (gates are reversed on this display)
  y = displayHeight - y - h;

  // Set data entry mode (X increment, Y decrement for reversed gates)
  sendCommand(CMD_DATA_ENTRY_MODE);
  sendData(DATA_ENTRY_X_INC_Y_DEC);

  // Set RAM X address range (start, end) - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_RANGE);
  sendData(x % 256);            // start low byte
  sendData(x / 256);            // start high byte
  sendData((x + w - 1) % 256);  // end low byte
  sendData((x + w - 1) / 256);  // end high byte

  // Set RAM Y address range (start, end) - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_RANGE);
  sendData((y + h - 1) % 256);  // start low byte
  sendData((y + h - 1) / 256);  // start high byte
  sendData(y % 256);            // end low byte
  sendData(y / 256);            // end high byte

  // Set RAM X address counter - X is in PIXELS
  sendCommand(CMD_SET_RAM_X_COUNTER);
  sendData(x % 256);  // low byte
  sendData(x / 256);  // high byte

  // Set RAM Y address counter - Y is in PIXELS
  sendCommand(CMD_SET_RAM_Y_COUNTER);
  sendData((y + h - 1) % 256);  // low byte
  sendData((y + h - 1) / 256);  // high byte
}

void EInkDisplay::clearScreen(const uint8_t color) const { memset(frameBuffer, color, bufferSize); }

void EInkDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                            const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer) {
    LOG_ERR(TAG, "Frame buffer not allocated!");
    return;
  }

  // Calculate bytes per line for the image
  const uint16_t imageWidthBytes = w / 8;

  // Copy image data to frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= displayHeight) break;

    const uint16_t destOffset = destY * displayWidthBytes + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= displayWidthBytes) break;

      if (fromProgmem) {
        frameBuffer[destOffset + col] = pgm_read_byte(&imageData[srcOffset + col]);
      } else {
        frameBuffer[destOffset + col] = imageData[srcOffset + col];
      }
    }
  }

  LOG_DBG(TAG, "Image drawn to frame buffer");
}

void EInkDisplay::drawImageTransparent(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                                       const uint16_t h, const bool fromProgmem) const {
  if (!frameBuffer) {
    LOG_ERR(TAG, "Frame buffer not allocated!");
    return;
  }

  // Calculate bytes per line for the image
  const uint16_t imageWidthBytes = w / 8;

  // AND-blend image data into the frame buffer (image is e-ink polarity:
  // bit=1 white, bit=0 black). AND keeps black source pixels and leaves
  // white source pixels alone.
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t destY = y + row;
    if (destY >= displayHeight) break;

    const uint16_t destOffset = destY * displayWidthBytes + (x / 8);
    const uint16_t srcOffset = row * imageWidthBytes;

    for (uint16_t col = 0; col < imageWidthBytes; col++) {
      if ((x / 8 + col) >= displayWidthBytes) break;

      const uint8_t srcByte = fromProgmem ? pgm_read_byte(&imageData[srcOffset + col]) : imageData[srcOffset + col];
      frameBuffer[destOffset + col] &= srcByte;
    }
  }

  LOG_DBG(TAG, "Transparent image drawn to frame buffer");
}

void EInkDisplay::writeRamBuffer(uint8_t ramBuffer, const uint8_t* data, uint32_t size) {
  const char* bufferName = (ramBuffer == CMD_WRITE_RAM_BW) ? "BW" : "RED";
  const unsigned long startTime = millis();
  LOG_DBG(TAG, "Writing frame buffer to %s RAM (%lu bytes)...", bufferName, size);

  sendCommand(ramBuffer);
  sendData(data, size);

  const unsigned long duration = millis() - startTime;
  LOG_DBG(TAG, "%s RAM write complete (%lu ms)", bufferName, duration);
}

void EInkDisplay::setFramebuffer(const uint8_t* bwBuffer) const { memcpy(frameBuffer, bwBuffer, bufferSize); }

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
void EInkDisplay::swapBuffers() {
  uint8_t* temp = frameBuffer;
  frameBuffer = frameBufferActive;
  frameBufferActive = temp;
}
#endif

void EInkDisplay::grayscaleRevert() {
  if (!inGrayscaleMode) {
    return;
  }

  inGrayscaleMode = false;

  // Load the revert LUT
  setCustomLUT(true, lut_grayscale_revert);
  refreshDisplay(FAST_REFRESH);
  setCustomLUT(false);
}

void EInkDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (!lsbBuffer) {
    _x3GrayState.lsbValid = false;
    return;
  }

  if (_x3Mode) {
    // X3 single-pass AA: write LSB plane to old-data RAM (0x10).
    // Send rows directly from frameBuffer (Y-mirrored via pointer arithmetic) —
    // no per-row copy buffer needed since we're not bit-inverting here.
    sendCommand(0x10);
    sendDataBatchBegin();
    for (uint16_t y = 0; y < displayHeight; y++) {
      const uint16_t srcY = static_cast<uint16_t>(displayHeight - 1 - y);
      SPI.writeBytes(lsbBuffer + static_cast<uint32_t>(srcY) * displayWidthBytes, displayWidthBytes);
    }
    sendDataBatchEnd();
    _x3GrayState.lsbValid = true;
    return;
  }
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, bufferSize);
}

void EInkDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (!msbBuffer) {
    return;
  }

  if (_x3Mode) {
    if (!_x3GrayState.lsbValid) {
      return;
    }

    sendCommand(0x13);
    sendDataBatchBegin();
    for (uint16_t y = 0; y < displayHeight; y++) {
      const uint16_t srcY = static_cast<uint16_t>(displayHeight - 1 - y);
      SPI.writeBytes(msbBuffer + static_cast<uint32_t>(srcY) * displayWidthBytes, displayWidthBytes);
    }
    sendDataBatchEnd();
    return;
  }
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, bufferSize);
}

void EInkDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  if (_x3Mode) {
    copyGrayscaleLsbBuffers(lsbBuffer);
    copyGrayscaleMsbBuffers(msbBuffer);
    return;
  }
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, lsbBuffer, bufferSize);
  writeRamBuffer(CMD_WRITE_RAM_RED, msbBuffer, bufferSize);
}

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
/**
 * In single buffer mode, this should be called with the previously written BW buffer
 * to reconstruct the RED buffer for proper differential fast refreshes following a
 * grayscale display.
 */
void EInkDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  if (_x3Mode) {
    if (!bwBuffer) {
      return;
    }

    // Rebase both X3 planes from restored BW buffer so next differential update
    // compares from a coherent known state. Both writes are non-inverting, so we
    // send rows directly from bwBuffer without an intermediate copy buffer.
    auto sendMirroredPlaneDirect = [&](const uint8_t* plane) {
      sendDataBatchBegin();
      for (uint16_t y = 0; y < displayHeight; y++) {
        const uint16_t srcY = static_cast<uint16_t>(displayHeight - 1 - y);
        SPI.writeBytes(plane + static_cast<uint32_t>(srcY) * displayWidthBytes, displayWidthBytes);
      }
      sendDataBatchEnd();
    };

    sendCommand(0x13);
    sendMirroredPlaneDirect(bwBuffer);
    sendCommand(0x10);
    sendMirroredPlaneDirect(bwBuffer);

    _x3RedRamSynced = true;
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;
    return;
  }

  // X4 single-buffer cleanup: also write BW so the next fast-diff has a current
  // current-frame baseline; otherwise the controller compares against a stale
  // BW plane left over from the grayscale write.
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_BW, bwBuffer, bufferSize);
  writeRamBuffer(CMD_WRITE_RAM_RED, bwBuffer, bufferSize);
  inGrayscaleMode = false;
}
#endif

void EInkDisplay::displayBuffer(RefreshMode mode, const bool turnOffScreen) {
  if (!_x3Mode && !isScreenOn && mode == FAST_REFRESH) {
    // Force half refresh if screen is off — FAST_REFRESH requires valid
    // previous frame data in RED RAM which may be stale after power-off.
    // FULL/HALF rebuild RED RAM themselves so they don't need coercion.
    mode = HALF_REFRESH;
  }

  // Revert grayscale mode if active (idempotent — guarded internally).
  grayscaleRevert();

  if (_x3Mode) {
    // X3 update policy: RED RAM (0x10) on the controller stores the previous
    // frame for differential updates, eliminating the 52 KB _x3PrevFrame
    // software buffer.  CMD04 re-powers the charge pump when needed.
    // On X3, treat HALF refresh as fast differential mode.
    // Reader uses HALF as a cadence hint, but forcing full here makes turns too slow.
    const bool fastMode = (mode != FULL_REFRESH);
    uint8_t row[128];
    auto sendCommandDataX3 = [&](uint8_t cmd, const uint8_t* data, uint16_t len) {
      SPI.beginTransaction(spiSettings);
      digitalWrite(_cs, LOW);
      digitalWrite(_dc, LOW);
      SPI.transfer(cmd);
      if (len > 0 && data != nullptr) {
        digitalWrite(_dc, HIGH);
        SPI.writeBytes(data, len);
      }
      digitalWrite(_cs, HIGH);
      SPI.endTransaction();
    };
    auto sendCommandDataByteX3 = [&](uint8_t cmd, uint8_t d0, uint8_t d1) {
      const uint8_t d[2] = {d0, d1};
      sendCommandDataX3(cmd, d, 2);
    };
    // Send a Y-mirrored plane to the controller. When invertBits is false we
    // bypass the per-row copy buffer and DMA straight from frameBuffer — the
    // copy was only ever needed for the bit-inverted full-sync path.
    auto sendMirroredPlane = [&](const uint8_t* plane, bool invertBits) {
      sendDataBatchBegin();
      if (invertBits) {
        for (uint16_t y = 0; y < displayHeight; y++) {
          const uint16_t srcY = static_cast<uint16_t>(displayHeight - 1 - y);
          const uint8_t* src = plane + static_cast<uint32_t>(srcY) * displayWidthBytes;
          for (uint16_t x = 0; x < displayWidthBytes; x++) {
            row[x] = static_cast<uint8_t>(~src[x]);
          }
          SPI.writeBytes(row, displayWidthBytes);
        }
      } else {
        for (uint16_t y = 0; y < displayHeight; y++) {
          const uint16_t srcY = static_cast<uint16_t>(displayHeight - 1 - y);
          SPI.writeBytes(plane + static_cast<uint32_t>(srcY) * displayWidthBytes, displayWidthBytes);
        }
      }
      sendDataBatchEnd();
    };

    const bool forcedFullSync = _x3ForceFullSyncNext;
    const bool doFullSync = !fastMode || !_x3RedRamSynced || _x3InitialFullSyncsRemaining > 0 || forcedFullSync;

    LOG_DBG(TAG, "X3_OEM_%s", doFullSync ? "FULL" : "FAST");
    _x3GrayState.lastBaseWasPartial = !doFullSync;

    if (doFullSync) {
      // Full sync: img LUTs, inverted data to both RAMs
      sendCommandDataX3(0x20, lut_x3_vcom_img, 42);
      sendCommandDataX3(0x21, lut_x3_ww_img, 42);
      sendCommandDataX3(0x22, lut_x3_bw_img, 42);
      sendCommandDataX3(0x23, lut_x3_wb_img, 42);
      sendCommandDataX3(0x24, lut_x3_bb_img, 42);

      sendCommand(0x13);
      sendMirroredPlane(frameBuffer, true);
      sendCommand(0x10);
      sendMirroredPlane(frameBuffer, true);

      sendCommandDataByteX3(0x50, 0xA9, 0x07);
    } else {
      // Fast differential: full LUTs, RED RAM (0x10) retains previous frame
      sendCommandDataX3(0x20, lut_x3_vcom_full, 42);
      sendCommandDataX3(0x21, lut_x3_ww_full, 42);
      sendCommandDataX3(0x22, lut_x3_bw_full, 42);
      sendCommandDataX3(0x23, lut_x3_wb_full, 42);
      sendCommandDataX3(0x24, lut_x3_bb_full, 42);

      // Write only new data to 0x13; controller diffs against 0x10
      sendCommand(0x13);
      sendMirroredPlane(frameBuffer, false);

      sendCommandDataByteX3(0x50, 0x29, 0x07);
    }

    if (!isScreenOn || doFullSync) {
      sendCommand(0x04);
      waitForRefresh(" X3_CMD04");
      isScreenOn = true;
    }

    LOG_DBG(TAG, "X3_OEM_TRIGGER=0x12");
    sendCommand(0x12);
    waitForRefresh(" X3_CMD12");

    // Power off analog rails immediately after refresh if requested,
    // before RAM bookkeeping (which only needs SPI, not the charge pump).
    // This mirrors X4 behavior where power-off is part of the refresh cycle.
    if (turnOffScreen) {
      sendCommand(0x02);
      waitForRefresh(" X3_CMD02_POWEROFF");
      isScreenOn = false;
    }

    if (!fastMode) delay(200);

    // One-time light settle after the first major full-sync improves early
    // page-turn quality on X3 without paying the old 6-pass cost.
    uint8_t postConditionPasses = 0;
    if (doFullSync) {
      if (forcedFullSync)
        postConditionPasses = _x3ForcedConditionPassesNext;
      else if (_x3InitialFullSyncsRemaining == 1)
        postConditionPasses = 1;
    }

    if (postConditionPasses > 0) {
      const uint16_t xStart = 0;
      const uint16_t xEnd = static_cast<uint16_t>(displayWidth - 1);
      const uint16_t yStart = 0;
      const uint16_t yEnd = static_cast<uint16_t>(displayHeight - 1);
      const uint8_t w[9] = {
          static_cast<uint8_t>(xStart >> 8), static_cast<uint8_t>(xStart & 0xFF), static_cast<uint8_t>(xEnd >> 8),
          static_cast<uint8_t>(xEnd & 0xFF), static_cast<uint8_t>(yStart >> 8),   static_cast<uint8_t>(yStart & 0xFF),
          static_cast<uint8_t>(yEnd >> 8),   static_cast<uint8_t>(yEnd & 0xFF),   0x01};

      sendCommandDataX3(0x20, lut_x3_vcom_full, 42);
      sendCommandDataX3(0x21, lut_x3_ww_full, 42);
      sendCommandDataX3(0x22, lut_x3_bw_full, 42);
      sendCommandDataX3(0x23, lut_x3_wb_full, 42);
      sendCommandDataX3(0x24, lut_x3_bb_full, 42);
      sendCommandDataByteX3(0x50, 0x29, 0x07);

      for (uint8_t i = 0; i < postConditionPasses; i++) {
        LOG_DBG(TAG, "X3_OEM_COND %u/%u", static_cast<unsigned>(i + 1), static_cast<unsigned>(postConditionPasses));
        sendCommand(0x91);
        sendCommandDataX3(0x90, w, 9);
        sendCommand(0x13);
        sendMirroredPlane(frameBuffer, false);
        sendCommand(0x92);
        if (!isScreenOn) {
          sendCommand(0x04);
          waitForRefresh(" X3_CMD04");
          isScreenOn = true;
        }
        LOG_DBG(TAG, "X3_OEM_TRIGGER=0x12(cond)");
        sendCommand(0x12);
        waitForRefresh(" X3_CMD12(cond)");
      }
    }

    // Sync RED RAM (0x10) with non-inverted current frame for next fast diff.
    // This is a controller memory write — doesn't need the charge pump.
    sendCommand(0x10);
    sendMirroredPlane(frameBuffer, false);
    _x3RedRamSynced = true;

    if (doFullSync && _x3InitialFullSyncsRemaining > 0) {
      _x3InitialFullSyncsRemaining--;
    }
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;
    return;
  }

  // X4 path
  setRamArea(0, 0, displayWidth, displayHeight);

  if (mode != FAST_REFRESH) {
    // For full refresh, write to both buffers before refresh
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
  } else {
    // For fast refresh, write to BW buffer only
    writeRamBuffer(CMD_WRITE_RAM_BW, frameBuffer, bufferSize);
    // In single buffer mode, the RED RAM should already contain the previous frame
    // In dual buffer mode, we write back frameBufferActive which is the last frame
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
    writeRamBuffer(CMD_WRITE_RAM_RED, frameBufferActive, bufferSize);
#endif
  }

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  swapBuffers();
#endif

  // Refresh the display
  refreshDisplay(mode, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // In single buffer mode always sync RED RAM after refresh to prepare for next fast refresh
  // This ensures RED contains the currently displayed frame for differential comparison
  setRamArea(0, 0, displayWidth, displayHeight);
  writeRamBuffer(CMD_WRITE_RAM_RED, frameBuffer, bufferSize);
#endif
}

// EXPERIMENTAL: Windowed update support
// Displays only a rectangular region of the frame buffer, preserving the rest of the screen.
// Requirements: x and w must be byte-aligned (multiples of 8 pixels)
void EInkDisplay::displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const bool turnOffScreen) {
  LOG_DBG(TAG, "Displaying window at (%d,%d) size (%dx%d)", x, y, w, h);

  // Validate bounds
  if (x + w > displayWidth || y + h > displayHeight) {
    LOG_ERR(TAG, "Window bounds exceed display dimensions!");
    return;
  }

  // Validate byte alignment
  if (x % 8 != 0 || w % 8 != 0) {
    LOG_ERR(TAG, "Window x and width must be byte-aligned (multiples of 8)!");
    return;
  }

  if (!frameBuffer) {
    LOG_ERR(TAG, "Frame buffer not allocated!");
    return;
  }

  // displayWindow is not supported while the rest of the screen has grayscale content, revert it
  grayscaleRevert();

  // Calculate window buffer size
  const uint16_t windowWidthBytes = w / 8;
  const uint32_t windowBufferSize = windowWidthBytes * h;

  LOG_DBG(TAG, "Window buffer size: %lu bytes (%d x %d pixels)", windowBufferSize, w, h);

  // Allocate temporary buffer on stack
  std::vector<uint8_t> windowBuffer(windowBufferSize);

  // Extract window region from frame buffer
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * displayWidthBytes + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&windowBuffer[dstOffset], &frameBuffer[srcOffset], windowWidthBytes);
  }

  // Configure RAM area for window
  setRamArea(x, y, w, h);

  // Write to BW RAM (current frame)
  writeRamBuffer(CMD_WRITE_RAM_BW, windowBuffer.data(), windowBufferSize);

#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // Dual buffer: Extract window from frameBufferActive (previous frame)
  std::vector<uint8_t> previousWindowBuffer(windowBufferSize);
  for (uint16_t row = 0; row < h; row++) {
    const uint16_t srcY = y + row;
    const uint16_t srcOffset = srcY * displayWidthBytes + (x / 8);
    const uint16_t dstOffset = row * windowWidthBytes;
    memcpy(&previousWindowBuffer[dstOffset], &frameBufferActive[srcOffset], windowWidthBytes);
  }
  writeRamBuffer(CMD_WRITE_RAM_RED, previousWindowBuffer.data(), windowBufferSize);
#endif

  // Perform fast refresh
  refreshDisplay(FAST_REFRESH, turnOffScreen);

#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // Post-refresh: Sync RED RAM with current window (for next fast refresh)
  setRamArea(x, y, w, h);
  writeRamBuffer(CMD_WRITE_RAM_RED, windowBuffer.data(), windowBufferSize);
#endif

  LOG_DBG(TAG, "Window display complete");
}

void EInkDisplay::displayGrayBuffer(const bool turnOffScreen) {
  if (_x3Mode) {
    // X3 AA pipeline: LSB->0x10 + MSB->0x13, trigger 0x12 with X3 LUT bank.
    drawGrayscale = false;
    inGrayscaleMode = false;

    if (!_x3GrayState.lsbValid) {
      return;
    }

    auto sendCommandDataX3 = [&](uint8_t cmd, const uint8_t* data, uint16_t len) {
      SPI.beginTransaction(spiSettings);
      digitalWrite(_cs, LOW);
      digitalWrite(_dc, LOW);
      SPI.transfer(cmd);
      if (len > 0 && data != nullptr) {
        digitalWrite(_dc, HIGH);
        SPI.writeBytes(data, len);
      }
      digitalWrite(_cs, HIGH);
      SPI.endTransaction();
    };
    auto sendCommandDataByteX3 = [&](uint8_t cmd, uint8_t d0, uint8_t d1) {
      const uint8_t d[2] = {d0, d1};
      sendCommandDataX3(cmd, d, 2);
    };

    const uint8_t* vcom = lut_x3_vcom_gray;
    const uint8_t* ww = lut_x3_ww_gray;
    const uint8_t* bw = lut_x3_bw_gray;
    const uint8_t* wb = lut_x3_wb_gray;
    const uint8_t* bb = lut_x3_bb_gray;
    const uint8_t dataInterval0 = 0x29;
    const uint8_t dataInterval1 = 0x07;
    LOG_DBG(TAG, "X3_GRAY_MODE=gray_tuned");
    sendCommandDataX3(0x20, vcom, 42);
    sendCommandDataX3(0x21, ww, 42);
    sendCommandDataX3(0x22, bw, 42);
    sendCommandDataX3(0x23, wb, 42);
    sendCommandDataX3(0x24, bb, 42);
    sendCommandDataByteX3(0x50, dataInterval0, dataInterval1);

    if (!isScreenOn) {
      sendCommand(0x04);
      waitForRefresh(" X3_CMD04(gray)");
      isScreenOn = true;
    }

    sendCommand(0x12);
    waitForRefresh(" X3_CMD12(gray)");

    if (turnOffScreen) {
      sendCommand(0x02);
      waitForRefresh(" X3_CMD02_POWEROFF(gray)");
      isScreenOn = false;
    }

    // RAM baseline is re-established from restored BW buffer by
    // cleanupGrayscaleBuffers() after this function returns.
    _x3RedRamSynced = false;
    _x3ForceFullSyncNext = false;
    _x3ForcedConditionPassesNext = 0;

    _x3GrayState.lsbValid = false;
    return;
  }

  drawGrayscale = false;
  inGrayscaleMode = true;

  // activate the custom LUT for grayscale rendering and refresh
  setCustomLUT(true, lut_grayscale);
  refreshDisplay(FAST_REFRESH, turnOffScreen);
  setCustomLUT(false);
}

void EInkDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  if (_x3Mode) {
    displayBuffer(mode, turnOffScreen);
    return;
  }

  // Configure Display Update Control 1
  sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
  sendData((mode == FAST_REFRESH) ? CTRL1_NORMAL : CTRL1_BYPASS_RED);  // Configure buffer comparison mode

  // best guess at display mode bits:
  // bit | hex | name                    | effect
  // ----+-----+--------------------------+-------------------------------------------
  // 7   | 80  | CLOCK_ON                | Start internal oscillator
  // 6   | 40  | ANALOG_ON               | Enable analog power rails (VGH/VGL drivers)
  // 5   | 20  | TEMP_LOAD               | Load temperature (internal or I2C)
  // 4   | 10  | LUT_LOAD                | Load waveform LUT
  // 3   | 08  | MODE_SELECT             | Mode 1/2
  // 2   | 04  | DISPLAY_START           | Run display
  // 1   | 02  | ANALOG_OFF_PHASE        | Shutdown step 1 (undocumented)
  // 0   | 01  | CLOCK_OFF               | Disable internal oscillator

  // Select appropriate display mode based on refresh type
  uint8_t displayMode = 0x00;

  // Enable counter and analog if not already on
  if (!isScreenOn) {
    isScreenOn = true;
    displayMode |= 0xC0;  // Set CLOCK_ON and ANALOG_ON bits
  }

  // Turn off screen if requested
  if (turnOffScreen) {
    isScreenOn = false;
    displayMode |= 0x03;  // Set ANALOG_OFF_PHASE and CLOCK_OFF bits
  }

  if (mode == FULL_REFRESH) {
    displayMode |= 0x34;
  } else if (mode == HALF_REFRESH) {
    // Write high temp to the register for a faster refresh
    sendCommand(CMD_WRITE_TEMP);
    sendData(0x5A);
    displayMode |= 0xD4;
  } else {  // FAST_REFRESH
    displayMode |= customLutActive ? 0x0C : 0x1C;
  }

  // Power on and refresh display
  const char* refreshType = (mode == FULL_REFRESH) ? "full" : (mode == HALF_REFRESH) ? "half" : "fast";
  LOG_DBG(TAG, "Powering on display 0x%02X (%s refresh)...", displayMode, refreshType);
  sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
  sendData(displayMode);

  sendCommand(CMD_MASTER_ACTIVATION);

  // Wait for display to finish updating
  LOG_DBG(TAG, "Waiting for display refresh...");
  waitWhileBusy(refreshType);
}

void EInkDisplay::setCustomLUT(const bool enabled, const unsigned char* lutData) {
  if (enabled) {
    LOG_DBG(TAG, "Loading custom LUT...");

    // Load custom LUT (first 105 bytes: VS + TP/RP + frame rate)
    uint8_t lutBuf[105];
    memcpy_P(lutBuf, lutData, 105);
    sendCommand(CMD_WRITE_LUT);
    sendData(lutBuf, 105);

    // Set voltage values from bytes 105-109
    sendCommand(CMD_GATE_VOLTAGE);  // VGH
    sendData(pgm_read_byte(&lutData[105]));

    sendCommand(CMD_SOURCE_VOLTAGE);         // VSH1, VSH2, VSL
    sendData(pgm_read_byte(&lutData[106]));  // VSH1
    sendData(pgm_read_byte(&lutData[107]));  // VSH2
    sendData(pgm_read_byte(&lutData[108]));  // VSL

    sendCommand(CMD_WRITE_VCOM);  // VCOM
    sendData(pgm_read_byte(&lutData[109]));

    customLutActive = true;
    LOG_DBG(TAG, "Custom LUT loaded");
  } else {
    customLutActive = false;
    LOG_DBG(TAG, "Custom LUT disabled");
  }
}

void EInkDisplay::deepSleep() {
  LOG_INF(TAG, "Preparing display for deep sleep...");

  // First, power down the display properly
  // This shuts down the analog power rails and clock
  if (isScreenOn) {
    sendCommand(CMD_DISPLAY_UPDATE_CTRL1);
    sendData(CTRL1_BYPASS_RED);  // Normal mode

    sendCommand(CMD_DISPLAY_UPDATE_CTRL2);
    sendData(0x03);  // Set ANALOG_OFF_PHASE (bit 1) and CLOCK_OFF (bit 0)

    sendCommand(CMD_MASTER_ACTIVATION);

    // Wait for the power-down sequence to complete
    waitWhileBusy(" display power-down");

    isScreenOn = false;
  }

  // Now enter deep sleep mode
  LOG_INF(TAG, "Entering deep sleep mode...");
  sendCommand(CMD_DEEP_SLEEP);
  sendData(0x01);  // Enter deep sleep
}

void EInkDisplay::saveFrameBufferAsPBM(const char* filename) {
#ifndef ARDUINO
  const uint8_t* buffer = getFrameBuffer();

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    LOG_ERR(TAG, "Failed to open %s for writing", filename);
    return;
  }

  // Rotate the image 90 degrees counterclockwise when saving the runtime panel.
  // Original buffer: displayWidth × displayHeight (landscape)
  // Output image: displayHeight × displayWidth (portrait)
  const int landscapeWidth = displayWidth;
  const int landscapeHeight = displayHeight;
  const int landscapeWidthBytes = displayWidthBytes;

  file << "P4\n";  // Binary PBM
  file << landscapeHeight << " " << landscapeWidth << "\n";

  // Create rotated buffer
  std::vector<uint8_t> rotatedBuffer((landscapeHeight / 8) * landscapeWidth, 0);

  for (int outY = 0; outY < landscapeWidth; outY++) {
    for (int outX = 0; outX < landscapeHeight; outX++) {
      int inX = outY;
      int inY = landscapeHeight - 1 - outX;

      int inByteIndex = inY * landscapeWidthBytes + (inX / 8);
      int inBitPosition = 7 - (inX % 8);
      bool isWhite = (buffer[inByteIndex] >> inBitPosition) & 1;

      int outByteIndex = outY * (landscapeHeight / 8) + (outX / 8);
      int outBitPosition = 7 - (outX % 8);
      if (!isWhite) {  // Invert: e-ink white=1 -> PBM black=1
        rotatedBuffer[outByteIndex] |= (1 << outBitPosition);
      }
    }
  }

  file.write(reinterpret_cast<const char*>(rotatedBuffer.data()), rotatedBuffer.size());
  file.close();
  LOG_INF(TAG, "Saved framebuffer to %s", filename);
#else
  (void)filename;
  LOG_ERR(TAG, "saveFrameBufferAsPBM is not supported on Arduino builds.");
#endif
}
