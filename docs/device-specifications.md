# Device Specifications

## Devices

- **[X3](x3-specifications.md)**
  - MCU: ESP32-C3
  - Display: 792 × 528, UC8253 or UC8279d
  - Storage: SPI SD
  - Touch: None
- **[X4 (original)](x4-specifications.md)**
  - MCU: ESP32-C3
  - Display: 800 × 480, SSD1677
  - Storage: SPI SD
  - Touch: None
- **[X4 Pro](x4pro-specifications.md)**
  - MCU: ESP32-S3, 8 MB octal PSRAM
  - Display: 800 × 480, UC8279 or UC8179
  - Storage: 1-bit SDMMC
  - Touch: GT911
- **[X4 v2 Classic](x4-classic-specifications.md)**
  - MCU: ESP32-S3
  - Display: 800 × 480, SSD1677, UC8179, or UC8279
  - Storage: 1-bit SDMMC
  - Touch: None

- **[reTerminal Sticky](sticky-specifications.md)**
  - MCU: ESP32-S3, 8 MB octal PSRAM
  - Display: 800 × 480, SSD1677 or SSD2677
  - Storage: SPI SD
  - Touch: GT911
  - Status: implemented, not device-tested

See the [device support matrix](device-support-matrix.md) for build targets and hardware services.
Classic uses a separate board profile from the original X4 and X4 Pro.

## Firmware

- `papyrix-xteink-c3.bin` selects X3 or X4 at startup.
- `papyrix-x4pro.bin` uses the fixed X4 Pro board profile.
- `papyrix-x4c.bin` uses the fixed X4 v2 Classic board profile.
- `papyrix-sticky.bin` uses the fixed Sticky board profile.

The standard partition table starts app0 at `0x10000`.
The partition layout uses 16 MB.
Sticky has 32 MB of physical flash.
The Xteink targets have 16 MB of physical flash.
Sticky first installation requires the [complete install bundle](sticky-specifications.md#installation).

Do not install an artifact for a different MCU or board.
An incorrect image can drive the wrong GPIOs and damage hardware.

## Hardware Selection

`lib/BoardSupport` owns board profiles, hardware identity, and selection policy.
The C3 artifact checks the manual override before the saved board selection.
It probes X3 I2C devices when no saved selection applies.
Board detection and display-controller detection are separate operations.
See [X3 controller detection](x3-specifications.md#controller-detection).
Each probe pass checks the BQ27220, DS3231, and QMI8658.
Two or more detected devices in both passes select X3.
Zero detected devices in both passes select X4.
The firmware saves either conclusive result in NVS.
Other results select X4 for that boot without saving a detection result.

C3 board and panel selections use the `papyrix_hw` NVS namespace.
The C3 keys are `dev_ovr`, `dev_det`, `epd_ovr`, `epd_det`, and `epd_ver`.
Classic reads the factory panel identity from `hw_calib/screenType` without changing it.
A missing or invalid identity starts a bounded display probe.
An unknown response leaves the display disabled.
Headless SD recovery remains available.

Sticky uses a fixed board profile and probes its display controller.
An unresolved Sticky panel blocks display initialization.
See the [Sticky specifications](sticky-specifications.md#display-behavior) for the probe limits.

## Power

The CPU policy uses 10 MHz at idle.
Active work uses 160 MHz on ESP32-C3 and 240 MHz on ESP32-S3.
Xteink devices use GPIO3 for the Power button.
Sticky uses GPIO4.

The firmware powers the SD rail before storage access.
It completes the display power-off sequence before deep sleep.
Board-specific backends set the required rail and GPIO hold states.

## Storage

PapyriX supports FAT32 and exFAT SD cards.
Page caches use device-specific folders:

```text
/.papyrix/cache/          X4
/.papyrix/cache/x3/       X3
/.papyrix/cache/x4pro/    X4 Pro
/.papyrix/cache/x4c/      X4 v2 Classic
/.papyrix/cache/sticky/   reTerminal Sticky
```

Moving an SD card between devices does not reuse incompatible page layouts.
See [file formats](file-formats.md) for cache records.
See the [user guide](user_guide.md) for supported book formats.

## Hardware Libraries

- **`BoardSupport`:** Board profiles, selection, power, touch, battery, and RTC backends
- **`EInkDisplay`:** Display controller access and frame buffers
- **`InputManager`:** Physical button input
- **`BatteryMonitor`:** Battery measurements
- **`SDCardManager`:** SD card access

## References

- [ESP32-C3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
- [ESP32-C3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
