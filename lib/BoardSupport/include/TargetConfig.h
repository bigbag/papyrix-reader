#pragma once

// The build sets exactly one target macro to 1.

#ifndef PAPYRIX_TARGET_XTEINK_C3
#define PAPYRIX_TARGET_XTEINK_C3 0
#endif
#ifndef PAPYRIX_TARGET_X4PRO
#define PAPYRIX_TARGET_X4PRO 0
#endif
#ifndef PAPYRIX_TARGET_X4CLASSIC
#define PAPYRIX_TARGET_X4CLASSIC 0
#endif

#ifndef PAPYRIX_X4PRO_CHARACTERIZE
#define PAPYRIX_X4PRO_CHARACTERIZE 0
#endif

#if PAPYRIX_X4PRO_CHARACTERIZE && !PAPYRIX_TARGET_X4PRO
#error "PAPYRIX_X4PRO_CHARACTERIZE requires PAPYRIX_TARGET_X4PRO"
#endif

#if (PAPYRIX_TARGET_XTEINK_C3 + PAPYRIX_TARGET_X4PRO + PAPYRIX_TARGET_X4CLASSIC) != 1
#error "Set exactly one PAPYRIX_TARGET_* to 1: XTEINK_C3, X4PRO, or X4CLASSIC"
#endif

// Host tests do not define the ESP-IDF target macros.
#if PAPYRIX_TARGET_XTEINK_C3 && defined(CONFIG_IDF_TARGET_ESP32S3)
#error "PAPYRIX_TARGET_XTEINK_C3 requires the ESP32-C3 toolchain"
#endif
#if (PAPYRIX_TARGET_X4PRO || PAPYRIX_TARGET_X4CLASSIC) && defined(CONFIG_IDF_TARGET_ESP32C3)
#error "The selected S3 board requires the ESP32-S3 toolchain"
#endif

#include <DisplayController.h>

#include "BoardProfile.h"

namespace papyrix::board {

inline constexpr McuFamily kTargetMcuFamily = PAPYRIX_TARGET_XTEINK_C3 ? McuFamily::Esp32C3 : McuFamily::Esp32S3;

// The C3 artifact uses X4 until the runtime probe selects X3.
// Each S3 artifact uses one fixed board. X4 Pro probes its production panel variant.
inline constexpr BoardId kBootBoardId = PAPYRIX_TARGET_XTEINK_C3   ? BoardId::X4
                                        : PAPYRIX_TARGET_X4CLASSIC ? BoardId::X4Classic
                                                                   : BoardId::X4Pro;
inline constexpr eink::DisplayController kBootPanelController = eink::DisplayController::SSD1677;

inline constexpr uint16_t kTargetMaxDisplayWidth = 800;
inline constexpr uint16_t kTargetMaxDisplayHeight = PAPYRIX_TARGET_XTEINK_C3 ? 528 : 480;
inline constexpr uint32_t kTargetFrameBufferBytes =
    PAPYRIX_TARGET_XTEINK_C3 ? static_cast<uint32_t>(792 / 8) * 528 : static_cast<uint32_t>(800 / 8) * 480;
inline constexpr uint32_t kTargetBitmapTurnCacheBytes = PAPYRIX_TARGET_XTEINK_C3 ? 0 : 256 * 1024;
inline constexpr uint32_t kTargetGlyphBitmapSlabBytes = PAPYRIX_TARGET_XTEINK_C3 ? 0 : 96 * 1024;
inline constexpr uint32_t kTargetParserScratchBytes = PAPYRIX_TARGET_XTEINK_C3 ? 0 : 64 * 1024;
inline constexpr bool kTargetHasVerifiedPanelDriver = true;

}  // namespace papyrix::board

// These macros control which backend source files enter each artifact.
// The C3 values combine X3 and X4 because the artifact selects the board at runtime.
#if PAPYRIX_TARGET_XTEINK_C3
#define PAPYRIX_CAP_TOUCH 0
#define PAPYRIX_CAP_FRONTLIGHT 0
#define PAPYRIX_CAP_RTC 1
#define PAPYRIX_CAP_USB_DETECT 1
#define PAPYRIX_CAP_SDMMC 0
#define PAPYRIX_CAP_BATTERY_ADC 1
#define PAPYRIX_CAP_BATTERY_BQ27220 1
#define PAPYRIX_CAP_BATTERY_CW2017 0
#elif PAPYRIX_TARGET_X4PRO
#define PAPYRIX_CAP_TOUCH 1
#define PAPYRIX_CAP_FRONTLIGHT 1
#define PAPYRIX_CAP_RTC 1
#define PAPYRIX_CAP_USB_DETECT 1
#define PAPYRIX_CAP_SDMMC 1
#define PAPYRIX_CAP_BATTERY_ADC 0
#define PAPYRIX_CAP_BATTERY_BQ27220 0
#define PAPYRIX_CAP_BATTERY_CW2017 1
#elif PAPYRIX_TARGET_X4CLASSIC
#define PAPYRIX_CAP_TOUCH 0
#define PAPYRIX_CAP_FRONTLIGHT 0
#define PAPYRIX_CAP_RTC 1
#define PAPYRIX_CAP_USB_DETECT 1
#define PAPYRIX_CAP_SDMMC 1
#define PAPYRIX_CAP_BATTERY_ADC 0
#define PAPYRIX_CAP_BATTERY_BQ27220 0
#define PAPYRIX_CAP_BATTERY_CW2017 1
#endif
