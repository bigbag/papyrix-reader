# Device Support Matrix

## Build Targets

- **Checker target: `default`**
  - PlatformIO environments: `default`, `release_xteink_c3`
  - MCU: ESP32-C3
  - Devices: X3, X4
- **Checker target: `x4pro`**
  - PlatformIO environments: `x4pro`, `release_x4pro`
  - MCU: ESP32-S3
  - Devices: X4 Pro
- **Checker target: `x4c`**
  - PlatformIO environments: `x4c`, `release_x4c`
  - MCU: ESP32-S3
  - Devices: X4 v2 Classic

## Release Artifacts

`make package` writes the release images and `manifest.json` to `dist/`.
Packaging checks target features before it copies an image.

- **`papyrix-xteink-c3.bin`:** X3, X4
- **`papyrix-x4pro.bin`:** X4 Pro
- **`papyrix-x4c.bin`:** X4 v2 Classic

The manifest contains the version, environment, MCU, flash offset, SHA-256,
profile schema, board values, and panel values.
The panel list identifies supported controllers.

## Hardware Services

- **X3**
  - Battery: BQ27220
  - RTC: DS3231
  - Front light: None
  - USB detection: Native USB and battery charge state
- **X4**
  - Battery: ADC
  - RTC: None
  - Front light: None
  - USB detection: Native USB and GPIO20
- **X4 Pro**
  - Battery: CW2017 and GPIO21
  - RTC: BM8563
  - Front light: GPIO8 cool, GPIO9 warm PWM
  - USB detection: Native USB and battery charge state
- **X4 v2 Classic**
  - Battery: CW2017 factory calibration, GPIO21
  - RTC: BM8563
  - Front light: None
  - USB detection: Native USB and battery charge state

The CW2017 backend checks MODE, VERSION, and charge readings during polling. On Pro, repeated invalid readings trigger a bounded recovery attempt. Failed recovery attempts use a longer retry interval, up to 32 seconds. Health polling continues during this interval. A valid reading cancels a pending restart. The backend clears the restart command after the MODE sequence completes. Classic receives no calibration writes or restart commands. Both targets retain the last valid reading during a fault.

A valid RTC value sets system time.
A successful NTP update writes UTC time to an available RTC.
Wi-Fi starts on demand.
Deep sleep stops Wi-Fi and turns off the front light.
Settings > Screen contains Brightness and Warmth on X4 Pro.

The CPU policy uses 10 MHz at idle.
It restores 160 MHz on ESP32-C3 and 240 MHz on ESP32-S3 for active work.

## Touch Input

Only the X4 Pro artifact includes GT911 support.
The controller uses the shared 100 kHz I2C bus on GPIO39/GPIO38.
GPIO2 controls its active-low power rail.
GPIO4 controls reset.
GPIO10 receives interrupts.

Reader touch zones use 24 percent of the width for Previous,
52 percent for Menu, and 24 percent for Next.
The side-button preference reverses the page zones.
Disabling Touch page turns disables the page zones only.
Menu taps, overlay taps, and physical buttons remain active.
The classifier rejects movement and multiple contacts.
It suppresses contacts during refresh and stale contacts after wake.

## Target Isolation

- **Pro touch board code**
  - C3: Forbidden
  - X4 Pro: Allowed
  - X4 Classic: Forbidden
- **Shared S3 panel drivers**
  - C3: Forbidden
  - X4 Pro: Allowed
  - X4 Classic: Allowed
- **GT911**
  - C3: Forbidden
  - X4 Pro: Allowed
  - X4 Classic: Forbidden
- **Front-light backend**
  - C3: Not used
  - X4 Pro: Allowed
  - X4 Classic: Forbidden
- **PaperMono:** Forbidden on C3, X4 Pro, and X4 Classic.
- **FT6336:** Forbidden on C3, X4 Pro, and X4 Classic.
- **Swipe:** Forbidden on C3, X4 Pro, and X4 Classic.
- **Bluetooth:** Forbidden on C3, X4 Pro, and X4 Classic.
- **`X4ProVariant`:** Forbidden on C3, X4 Pro, and X4 Classic.

## X4 Classic

Classic uses a separate firmware image and board profile.
It has physical buttons, 1-bit SDMMC, a CW2017 battery gauge, and a BM8563 RTC.
It has no touch input or front light.

See the [Classic specifications](x4-classic-specifications.md) for pins, panel selection, and power policy.
