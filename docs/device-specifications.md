# Device Specifications

Shared hardware documentation for both the **Xteink X4** and **Xteink X3** e-readers running Papyrix firmware. For device-specific specs (display, battery, pins), see:

- [Xteink X4 Specifications](x4-specifications.md) — 4.26" 800×480 panel, ADC battery, 40 MHz SPI
- [Xteink X3 Specifications](x3-specifications.md) — 3.68" 792×528 panel, BQ27220 fuel gauge, 10 MHz SPI

---

## Overview

**Shared:**
- **Processor** — ESP32-C3 (RISC-V)
- **RAM** — ~380 KB usable
- **Flash** — 16 MB
- **Storage** — SD Card (FAT32/exFAT)

**Xteink X4:**
- **Display** — 4.26" 800×480 (~217 PPI)
- **Battery** — LiPo (ADC)
- **SPI Clock** — 40 MHz
- **Detection** — Default (fallback)

**Xteink X3:**
- **Display** — 3.68" 792×528 (~259 PPI)
- **Battery** — LiPo (BQ27220 fuel gauge)
- **SPI Clock** — 10 MHz
- **Detection** — I²C probe at boot

A single firmware binary supports both devices. The device variant is auto-detected at boot (see [Device Auto-Detection](#device-auto-detection)).

---

## Microcontroller

### ESP32-C3 Specifications

- **Architecture** — RISC-V RV32IMC
- **Clock Speed** — 160 MHz
- **SRAM** — 400 KB (380 KB usable)
- **Flash** — 16 MB external
- **WiFi** — 802.11 b/g/n (2.4 GHz)
- **Bluetooth** — BLE 5.0
- **GPIO** — 22 pins
- **ADC** — 2 channels, 12-bit

### Memory Layout

- **DROM** — `0x3C140020` — ~5 MB — Data ROM (strings, constants)
- **DRAM** — `0x3FC91C00` — ~14 KB — Initialized data
- **IROM** — `0x42000020` — ~1.2 MB — Main executable code
- **IRAM** — `0x40380000` — ~72 KB — Hot path code
- **RTC** — `0x50000000` — 56 bytes — RTC retention memory

### Power Consumption

- **Active (reading)** — ~50 mA
- **WiFi active** — ~150 mA
- **Deep sleep** — ~10 µA

---

## Device Auto-Detection

The firmware detects the device variant at boot using a two-pass I²C probe for X3-specific chips. This runs in `Device::probe()` before display initialization or battery/USB detection.

### Probe Sequence

1. Initialize I²C bus (SDA=GPIO 20, SCL=GPIO 0, 400 kHz)
2. Run two probe passes (2ms apart) scanning three chips:

- **BQ27220** (fuel gauge, 0x55) — SOC register (0x2C) ∈ [0, 100] AND voltage (0x08) ∈ [2500, 5000] mV
- **DS3231** (RTC, 0x68) — Seconds register (0x00) is valid BCD: tens ≤ 5, ones ≤ 9
- **QMI8658** (IMU, 0x6B / alt 0x6A) — WHO_AM_I register (0x00) = 0x05

3. Each chip that ACKs with sane register values scores 1 point per pass
4. **X3** if both passes score ≥ 2; **X4** if both passes score 0; defaults to X4 if inconclusive

### Caching and Override

- Result cached in NVS (`papyrix_hw` namespace, key `dev_det`) to skip probing on subsequent boots
- Manual override available via NVS key `dev_ovr` (for development)
- Probe order: override → cache → full probe → default to X4

Source: `src/drivers/Device.h`, `src/drivers/Device.cpp`

---

## Input System

### Button Configuration

The device uses a resistor ladder connected to two ADC pins plus a dedicated power button GPIO. Identical on both X4 and X3.

- **BACK** — Index 0 — ADC1 — ~3512 mV
- **CONFIRM** — Index 1 — ADC1 — ~2694 mV
- **LEFT** — Index 2 — ADC1 — ~1493 mV
- **RIGHT** — Index 3 — ADC1 — ~5 mV
- **UP** — Index 4 — ADC2 — ~2242 mV
- **DOWN** — Index 5 — ADC2 — ~5 mV
- **POWER** — Index 6 — GPIO3 — Digital (active LOW)

### ADC Pin Configuration

- **ADC_PIN_1** — GPIO 1 — Back/Confirm/Left/Right buttons
- **ADC_PIN_2** — GPIO 2 — Up/Down buttons
- **POWER_PIN** — GPIO 3 — Power button (digital, INPUT_PULLUP)

### ADC Voltage Ranges

**ADC Channel 1 (GPIO 1):**

- **3800+ mV** — None
- **2090-3800 mV** — BACK
- **750-2090 mV** — CONFIRM
- **0-750 mV** — LEFT/RIGHT

**ADC Channel 2 (GPIO 2):**

- **1120+ mV** — None
- **0-1120 mV** — UP/DOWN

### Input Settings

- **Debounce delay** — 20 ms
- **ADC attenuation** — 11 dB
- **ADC resolution** — 12-bit
- **Power button** — Active LOW with pullup

---

## Storage

### SD Card Interface

- **Interface** — SPI
- **Filesystem** — FAT32 and exFAT
- **Max size** — 32 GB recommended
- **SPI Clock** — 40 MHz

### SD Card Pin Mapping

- **CS** — GPIO 12 — Output
- **MISO** — GPIO 7 — Input
- **MOSI** — GPIO 10 — Output (shared with display)
- **SCLK** — GPIO 8 — Output (shared with display)

### Cache Directory Structure

Page caches are stored in device-specific subdirectories to prevent layout mismatches when an SD card is moved between devices:

```
/.papyrix/
├── cache/                    # X4 page caches
│   ├── epub_12471232/
│   ├── txt_98765432/
│   └── ...
├── cache/x3/                 # X3 page caches (separate viewport dimensions)
│   ├── epub_12471232/
│   ├── txt_98765432/
│   └── ...
├── settings.bin              # User settings (shared)
├── state.bin                 # Application state (shared)
└── wifi.bin                  # WiFi credentials (shared)
```

Source: `src/drivers/Device.cpp` — `Device::cacheDir()`

---

## Deep Sleep Wakeup

- Wake on power button press (GPIO 3)
- Configurable hold duration for power on
- Software reset detection via `ESP_RST_SW`

---

## Build Configuration

### PlatformIO Settings

```ini
[env:release]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
board_build.flash_mode = dio
board_build.flash_size = 16MB
board_build.partitions = partitions.csv
upload_speed = 921600
monitor_speed = 115200
```

### Build Flags

- `-DARDUINO_USB_MODE=1` — USB mode enabled
- `-DARDUINO_USB_CDC_ON_BOOT=1` — USB CDC on boot
- `-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1` — Single framebuffer
- `-DUSE_UTF8_LONG_NAMES=1` — UTF-8 filename support

### C++ Standard

C++20 (`-std=gnu++2a`)

---

## Supported File Formats

### Books

- **EPUB** — `.epub` — Standard e-book format (EPUB 2 and 3)
- **FB2** — `.fb2` — FictionBook 2.0 (metadata, TOC, no inline images)
- **HTML** — `.html`, `.htm` — Standalone HTML documents
- **XTC** — `.xtc` — Xteink native format (1-bit)
- **XTCH** — `.xtch` — Xteink high-quality (2-bit grayscale)
- **Markdown** — `.md`, `.markdown` — Markdown with basic formatting
- **TXT** — `.txt`, `.text` — Plain text

### Images

- **JPEG** — `.jpg`, `.jpeg` — Baseline only, max 2048×3072
- **PNG** — `.png` — Max 2048×3072
- **BMP** — `.bmp` — Max 2048×3072

### Fonts

- **EpdFont** — `.epdfont` — Custom e-ink optimized format

---

## Hardware Libraries

Located in `lib/`:

- **EInkDisplay** — `lib/EInkDisplay/` — SSD1677 driver (supports both X4 and X3 panels)
- **InputManager** — `lib/InputManager/` — Button handling
- **BatteryMonitor** — `lib/BatteryMonitor/` — Battery ADC (X4) / BQ27220 fuel gauge (X3)
- **SDCardManager** — `lib/SDCardManager/` — SD card interface

---

## References

- [ESP32-C3 Technical Reference Manual](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
- [ESP32-C3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [SSD1677 Datasheet](https://www.solomon-systech.com/)
