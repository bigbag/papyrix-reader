# Xteink X3 Specifications

Hardware specifications specific to the Xteink X3 e-reader. For shared hardware (ESP32-C3, buttons, SD card, build config), see [Device Specifications](device-specifications.md).

The X3 is auto-detected at boot via I²C probe. See [Device Specifications § Auto-Detection](device-specifications.md#device-auto-detection) for the detection algorithm.

---

## Display

### Panel

- **Size** — 3.68 inches diagonal
- **Resolution** — 792 × 528 pixels (landscape), 528 × 792 (portrait)
- **Pixel Density** — ~259 PPI
- **Colors** — Black/White (4-level grayscale)

### Display Controller (SSD1677)

- **Interface** — 4-wire SPI
- **SPI Clock** — 10 MHz (the X3 controller does not tolerate faster speeds; 20 MHz causes pixel corruption)
- **SPI Mode** — Mode 0 (CPOL=0, CPHA=0)
- **Data Order** — MSB First

### Framebuffer

**Calculation:** 792 pixels / 8 bits = 99 bytes per row × 528 rows = 52,272 bytes

The static buffer is allocated at `MAX_BUFFER_SIZE = 52,272` bytes to accommodate the larger of the two panels (X3 vs X4's 48,000 bytes).

### Refresh Modes

The X3 uses custom LUT waveform sets for each refresh mode. See [X3 LUT Waveforms](x3-lut-waveforms.md) for timing details and register-level documentation.

- **Full** (`lut_x3_*_full`, ~472 ms) — Periodic full refresh, display conditioning
- **Turbo** (`lut_x3_*_turbo`, ~382 ms) — Default page turns (fast differential)
- **Image** (`lut_x3_*_img`, ~908 ms) — Initial image write (both RAMs populated)
- **Grayscale** (`lut_x3_*_gray`, ~127 ms) — Anti-aliased covers, grayscale text

A LUT caching state machine (`X3LutSet` enum) tracks which set is currently loaded to avoid redundant SPI transfers.

### Display Pin Mapping

Same as X4:

- **SCLK** — GPIO 8 — Output — SPI Clock
- **MOSI** — GPIO 10 — Output — SPI Data Out
- **CS** — GPIO 21 — Output — Chip Select (active LOW)
- **DC** — GPIO 4 — Output — Data/Command select
- **RST** — GPIO 5 — Output — Hardware reset (active LOW)
- **BUSY** — GPIO 6 — Input — Busy status (LOW = busy)

### LUT Architecture

The X3 uses a fundamentally different LUT structure from the X4. Instead of a single 111-byte register (command 0x32), the X3 has **five separate LUT registers**, each 42 bytes with 7 phases:

- **VCOM** (0x20) — Common voltage waveform
- **WW** (0x21) — White → White transition
- **BW** (0x22) — Black → White transition
- **WB** (0x23) — White → Black transition
- **BB** (0x24) — Black → Black transition

See [X3 LUT Waveforms](x3-lut-waveforms.md) for the full structure, voltage encoding, and frame group calculations.

### Frame Transfer Timing

At 10 MHz SPI, transferring a full frame (52,272 bytes) takes ~42 ms, compared to ~12 ms at 40 MHz on the X4. This is the dominant overhead difference between devices.

---

## I²C Bus

The X3 has an I²C bus used for battery monitoring and device detection:

- **SDA** — GPIO 20
- **SCL** — GPIO 0
- **Frequency** — 400 kHz

Three chips are connected:

- **BQ27220** (0x55) — Battery fuel gauge. Active — used for battery level and USB detection.
- **DS3231** (0x68) — Real-time clock. Detected only — used for device identification scoring.
- **QMI8658** (0x6B / alt 0x6A) — 6-axis IMU. Detected only — used for device identification scoring.

**Note:** GPIO 0 is the battery ADC pin on X4, and GPIO 20 is UART0_RXD (USB detect) on X4. The pin repurposing is why battery monitoring and USB detection use different methods on each device.

---

## Power Management

### Battery Monitoring (BQ27220)

- **Method** — I²C fuel gauge (BQ27220)
- **I²C Address** — 0x55
- **State of Charge** — Register 0x2C (0-100%, calibrated by the chip)
- **Voltage** — Register 0x08 (millivolts)
- **Polling** — Rate-limited to 1-second minimum to avoid I²C bus saturation
- **Error handling** — Falls back to cached values on transient I²C glitches

### USB Detection

- **Method** — BQ27220 current register
- **Logic** — Positive current = charging (USB connected)

This differs from X4 which reads UART0_RXD (GPIO 20). On X3, GPIO 20 is I²C SDA and floats HIGH when the bus is idle. Without device detection running first, the X3 would misread this as "USB connected" and sleep on cold boot instead of booting normally.

### Power States

Same as X4:
- **Active** — Normal operation
- **Deep sleep** — Ultra-low power (wake on power button GPIO 3)

---

## Pin Summary

Most pins are identical to the X4 (see [X4 Specifications § Pin Summary](x4-specifications.md#pin-summary)). The differences:

- **GPIO 0** — I²C SCL (X4: Battery ADC)
- **GPIO 20** — I²C SDA (X4: UART0_RXD / USB detect)

---

## Cache Path

X3 page caches are stored in `/.papyrix/cache/x3/` (vs `/.papyrix/cache/` for X4). This prevents layout mismatches when an SD card is moved between devices — X3 pages are laid out for a 528×792 viewport, not X4's 480×800.

Source: `src/drivers/Device.cpp` — `Device::cacheDir()`
