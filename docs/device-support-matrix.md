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

- **Checker target: `sticky`**
  - PlatformIO environments: `sticky`, `release_sticky`
  - MCU: ESP32-S3
  - Device: reTerminal Sticky
  - Status: implemented, not device-tested

- **Checker target: `murphy_m4`**
  - PlatformIO environments: `murphy_m4`, `release_murphy_m4`
  - MCU: ESP32-S3
  - Device: Murphy M4
  - Status: **Implemented, not device-tested**

## Release Artifacts

`make package` writes the release images and `manifest.json` to `dist/`.
Packaging checks target features before it copies an image.

- **`papyrix-xteink-c3.bin`:** X3, X4
- **`papyrix-x4pro.bin`:** X4 Pro
- **`papyrix-x4c.bin`:** X4 v2 Classic
- **`papyrix-sticky.bin`:** Sticky app-only update
- **`papyrix-sticky-install.zip`:** Sticky first-install parts, manifest, and license notices
- **`papyrix-murphy-m4.bin`:** M4 app-only update
- **`papyrix-murphy-m4-install.zip`:** M4 first-install parts, manifest, and license notices

The manifest contains the version, environment, MCU, flash offset, SHA-256,
profile schema, board values, and panel values.
The panel list identifies supported controllers.

## Firmware update compatibility

Current firmware checks the MCU, image structure, checksum/hash, and slot size before flash erase.
It also requires exactly one matching board tag.
The combined X3/X4 image uses the `xteink_c3` compatibility class.
Pro, Classic, Sticky, and M4 use `x4pro`, `x4c`, `sticky`, and `murphy_m4`.
Missing, malformed, duplicate, and incompatible tags fail validation.
Web upload stores the app image.
The on-device updater validates it before installation.
The emergency SD path uses the same validator.

Older untagged images require unlocked serial installation.
USB-locked devices can be unable to downgrade.
Old installed updaters and direct serial flashing do not enforce this board check.
Board tags prevent accidental mismatches.
They do not authenticate firmware.

App-only updates require an existing compatible PapyriX partition layout.
They do not detect or migrate a stock layout.
Emergency SD updates also require bootable PapyriX and working SD access.

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

- **Sticky**
  - Battery: BQ27220 factory configuration
  - RTC: PCF8563
  - Sensor bus: Wire1, separate from touch
  - Front light: None
  - USB detection: GPIO9 ADC supply divider; threshold is physically unverified
  - Serial: CH343P UART0, not native USB

- **Murphy M4**
  - Battery: ADC9 with divider2.0; GPIO43 active-low charging signal
  - RTC: RX8010
  - Touch/RTC bus: native I2C1, persistent handles; 100/400 kHz per device
  - Front light: GPIO47 cool, GPIO48 warm PWM
  - USB detection/console: native USB-Serial/JTAG; separate from charging
  - Storage: four-bit SDMMC

The CW2017 backend checks MODE, VERSION, and charge readings during polling. On Pro, repeated invalid readings trigger a bounded recovery attempt. Failed recovery attempts use a longer retry interval, up to 32 seconds. Health polling continues during this interval. A valid reading cancels a pending restart. The backend clears the restart command after the MODE sequence completes. Classic receives no calibration writes or restart commands. Both targets retain the last valid reading during a fault.

A valid RTC value sets system time.
A successful NTP update writes UTC time to an available RTC.
Wi-Fi starts on demand.
Deep sleep stops Wi-Fi and turns off the front light.
Settings > Screen contains Brightness and Warmth on X4 Pro and M4.
Reader Menu shows these controls above the navigation items.
Brightness zero turns the light off.

Except on M4, the CPU policy uses 10 MHz at idle.
It restores 160 MHz on ESP32-C3 and 240 MHz on ESP32-S3 for active work.
M4 keeps its 240 MHz boot clock and the active input-loop delay.

## Touch Input

The X4 Pro and Sticky artifacts include GT911 support.
Pro uses GPIO39/GPIO38, active-low rail2, reset4, and IRQ10.
Sticky uses GPIO3/GPIO2, active-high rail42, reset41, and IRQ21.
Each profile selects its controller byte layout and coordinate transform.
M4 uses FT6336U-compatible touch on native I2C1.
Its acquisition task polls every 10 ms and coordinates shared reset with the display.

X3, X4, and X4 v2 Classic use button controls and have no touch input. X4 Pro, Sticky, and M4 use touch for menu selection. X4 Pro maps its capacitive Home key to Back and does not show a Back item in Reader Menu. Press and release Home while reading to close the book. Sticky and M4 have no Home key or physical Back or Confirm buttons. They keep the Back menu item to close the book and return to its source screen. See the [reading controls](user_guide.md#4-reading-mode) for each device group.
On touch devices, a tap outside Reader Menu closes the menu without closing the book.

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
  - Sticky: Forbidden
  - M4: Shared power/reset helpers only; no Pro touch backend
- **Pro/Classic panel drivers**
  - C3: Forbidden
  - X4 Pro: Allowed
  - X4 Classic: Allowed
  - Sticky: Forbidden
  - M4: Forbidden
- **GT911**
  - C3: Forbidden
  - X4 Pro: Allowed
  - X4 Classic: Forbidden
  - Sticky: Allowed
  - M4: Forbidden
- **Front-light backend**
  - C3: Not used
  - X4 Pro: Allowed
  - X4 Classic: Forbidden
  - Sticky: Forbidden
  - M4: Allowed
- **PaperMono:** Forbidden on all targets.
- **FT6336U:** Allowed on M4; forbidden on all other targets.
- **Swipe:** Forbidden on all targets.
- **Bluetooth:** Forbidden on all targets.
- **`X4ProVariant`:** Forbidden on all targets.

## X4 Classic

Classic uses a separate firmware image and board profile.
It has physical buttons, 1-bit SDMMC, a CW2017 battery gauge, and a BM8563 RTC.
It has no touch input or front light.

See the [Classic specifications](x4-classic-specifications.md) for pins, panel selection, and power policy.

## Sticky

Sticky uses a separate image, SPI storage, two I2C controllers, and two panel variants.
It retains the existing PapyriX applications and reader functions.
It has no front light or native USB storage.
Its app-update validator rejects missing, duplicate, malformed, and wrong-board tags before erase.
First installation uses the complete bundle, not an app image at offset zero.
The bundle replaces stock firmware and can erase device data.
Stock recovery and all physical behavior remain unverified.

See the [Sticky specifications](sticky-specifications.md) for pins, waveforms, installation, and verification limits.

## Murphy M4

M4 retains the shared reader and all five applications.
It adds a fixed profile, SSD1677 batch policy, FT acquisition, RX8010, four-bit SDMMC, and front-light persistence.
The full install bundle validates the 16 MiB flash bound and exact PapyriX partition layout.
Before a future install, verify model/batch, flash/layout, security restrictions, and private flash/SD backups.
Physical operation and stock restoration remain unverified.
See the [M4 specifications](murphy-m4-specifications.md) for the complete contract and limits.
