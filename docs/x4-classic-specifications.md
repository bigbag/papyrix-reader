# Xteink X4 v2 Classic Specifications

These specifications describe the ESP32-S3 Xteink X4 v2 Classic.
Classic differs from the [original ESP32-C3 X4](x4-specifications.md) and [X4 Pro](x4pro-specifications.md).
Their firmware images are not interchangeable.
See [Device Specifications](device-specifications.md) for shared services.

## Build

- Artifact: `papyrix-x4c.bin`
- Development environment: `x4c`
- Release environment: `release_x4c`
- MCU: ESP32-S3
- Flash: 16 MB
- Board selection: fixed
- Cache directory: `/.papyrix/cache/x4c/`

The build enables octal PSRAM.

```sh
# Build development firmware
pio run -e x4c

# Build release firmware
pio run -e release_x4c
```

## Hardware

The firmware board profile defines these connections:

- **Display:** 800 × 480 pixels; SPI at 10 MHz.
  - SCLK: GPIO12.
  - MOSI: GPIO11.
  - CS: GPIO13.
  - DC: GPIO14.
  - Reset: GPIO10.
  - BUSY: GPIO18.
- **Storage:** 1-bit SDMMC.
  - CLK: GPIO41.
  - CMD: GPIO42.
  - DAT0: GPIO40.
  - Power rail: GPIO6, active low.
- **Shared I2C bus:** SDA GPIO39, SCL GPIO38, 400 kHz.
  - Battery gauge: CW2017 at `0x63`.
  - Charge status: GPIO21.
  - RTC: BM8563 at `0x51`.
- **Buttons:** Seven active-low inputs.
  - Back: GPIO9.
  - Confirm: GPIO8.
  - Left: GPIO5.
  - Right: GPIO2.
  - Up: GPIO7.
  - Down: GPIO0.
  - Power: GPIO3.
- **Power latch:** GPIO1, active high.
- **Touch and front light:** None.
- **GPIO4:** Unused by this profile.

## Display Controller Selection

The panel selector reads `hw_calib/screenType` without writing NVS.
Factory values select the controller:

- `1` or `0x0B`: UC8179.
- `2` or `0x0C`: UC8279.
- `3`: SSD1677.

Missing or invalid values use a bounded three-byte display probe.
UC8279 selection also probes the panel to identify its variant.
An unresolved controller leaves the display disabled.
USB/power recovery can retry selection.
SD `/force_update.bin` recovery can run without a display.

Classic UC8279 keeps the factory PLL setting.
Its grayscale policy depends on the panel variant:

- `0x02` and `0x03`: QY grayscale tables.
- `0x68` and `0x69`: ZHX grayscale tables.
- `0x67` and unknown variants: Monochrome rendering for text and images.

The firmware preserves the user antialiasing preference.

## Power and Battery

Sleep keeps GPIO1 and display reset GPIO10 HIGH and holds the SD rail off.
Classic does not program CW2017 BATINFO or restart the gauge to install a profile.
A running factory gauge supplies measurements.
An unavailable or uninitialized gauge reports unknown charge.

## Installation

Use `papyrix-x4c.bin` only for Classic.
Pro and Classic images are not interchangeable.
Do not erase the chip or install a full-flash image from another device.
Check the partition table and active OTA slot before an application-only update.
The application must fit the selected slot.
Back up factory NVS and preserve it at `0x9000`.
Factory NVS supplies the panel identity used by Classic.

To build and flash the Classic release image over USB, use:

```sh
make flash-x4c
```

The target selects one connected Espressif USB-Serial/JTAG port and stops if
it finds none or more than one. Pro and Classic use the same USB identifier.
Confirm that the connected reader is Classic before you flash. Stop serial log
capture and keep the reader awake during the upload. Set
`PLATFORMIO_UPLOAD_PORT=/dev/ttyACM0` to select a port when needed.

The standard partition table uses these regions:

- **NVS:** `0x9000`, size `0x5000`
- **OTA metadata:** `0xE000`, size `0x2000`
- **Application 0:** `0x10000`, size `0x640000`
- **Application 1:** `0x650000`, size `0x640000`
- **Internal filesystem:** `0xC90000`, size `0x360000`
- **Core dump:** `0xFF0000`, size `0x10000`

Factory partition layouts can differ.
An application-only update does not change the partition table.
