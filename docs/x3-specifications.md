# Xteink X3 Specifications

These hardware specifications are for the Xteink X3 e-reader. For shared hardware (ESP32-C3, buttons, SD card, build config), see [Device Specifications](device-specifications.md).

The firmware finds the X3 at start with an I²C probe. See [Device Specifications § Auto-Detection](device-specifications.md#device-auto-detection) for the detection algorithm.

---

## Display

### Panel

- **Size** — 3.68 inches diagonal
- **Resolution** — 792 × 528 pixels (landscape), 528 × 792 (portrait)
- **Pixel Density** — approximately 259 PPI
- **Colors** — Black/White (4-level grayscale)

### Display Controller (SSD1677)

- **Interface** — 4-wire SPI
- **SPI Clock** — 10 MHz (the X3 controller does not tolerate faster speeds; 20 MHz causes pixel damage)
- **SPI Mode** — Mode 0 (CPOL=0, CPHA=0)
- **Data Order** — MSB First

### Framebuffer

**Calculation:** 792 pixels / 8 bits = 99 bytes for each row × 528 rows = 52,272 bytes

The static buffer is allocated at `MAX_BUFFER_SIZE = 52,272` bytes. This size holds the larger of the two panels (X3 compared to the X4 48,000 bytes).

### Refresh Modes

The X3 uses custom LUT waveform sets for each refresh mode. See [X3 LUT Waveforms](x3-lut-waveforms.md) for timing data and register-level documentation.

- **Full** (`lut_x3_*_full`, approximately 472 ms) — Periodic full refresh, display conditioning
- **Turbo** (`lut_x3_*_turbo`, approximately 382 ms) — Default page turns (fast differential)
- **Image** (`lut_x3_*_img`, approximately 908 ms) — Initial image write (both RAMs have data)
- **Grayscale** (`lut_x3_*_gray`, approximately 127 ms) — Anti-aliased covers, grayscale text

A LUT cache state machine (`X3LutSet` enum) records which set is loaded. This prevents SPI transfers that are not necessary.

### Display Pin Mapping

Same as X4:

- **SCLK** — GPIO 8 — Output — SPI Clock
- **MOSI** — GPIO 10 — Output — SPI Data Out
- **CS** — GPIO 21 — Output — Chip Select (active LOW)
- **DC** — GPIO 4 — Output — Data/Command select
- **RST** — GPIO 5 — Output — Hardware reset (active LOW)
- **BUSY** — GPIO 6 — Input — Busy status (LOW = busy)

### LUT Architecture

The X3 uses a different LUT structure from the X4. The X4 uses one 111-byte register (command 0x32). The X3 has **five LUT registers**. Each register is 42 bytes with 7 phases:

- **VCOM** (0x20) — Common voltage waveform
- **WW** (0x21) — White → White transition
- **BW** (0x22) — Black → White transition
- **WB** (0x23) — White → Black transition
- **BB** (0x24) — Black → Black transition

See [X3 LUT Waveforms](x3-lut-waveforms.md) for the full structure, voltage encoding, and frame group calculations.

### Frame Transfer Timing

At 10 MHz SPI, a full frame transfer (52,272 bytes) takes approximately 42 ms. At 40 MHz on the X4, the transfer takes approximately 12 ms. This is the primary overhead difference between the devices.

---

## I²C Bus

The X3 has an I²C bus. The firmware uses it for battery monitor and device detection:

- **SDA** — GPIO 20
- **SCL** — GPIO 0
- **Frequency** — 400 kHz

Three chips are connected:

- **BQ27220** (0x55) — Battery fuel gauge. Active. Used for battery level and USB detection.
- **DS3231** (0x68) — Real-time clock. Found only. Used for device identification scoring.
- **QMI8658** (0x6B / alternative 0x6A) — 6-axis IMU. Found only. Used for device identification scoring.

**Note:** GPIO 0 is the battery ADC pin on X4. GPIO 20 is UART0_RXD (USB detect) on X4. The pins have different functions. Battery monitor and USB detection use different methods on each device.

---

## Power Management

### Battery Monitoring (BQ27220)

- **Method** — I²C fuel gauge (BQ27220)
- **I²C Address** — 0x55
- **State of Charge** — Register 0x2C (0-100%, calibrated by the chip)
- **Voltage** — Register 0x08 (millivolts)
- **Polling** — Rate-limited to a 1-second minimum to prevent I²C bus saturation
- **Error handling** — Uses cached values if there are short I²C defects

### USB Detection

- **Method** — BQ27220 current register
- **Logic** — Positive current = charging (USB connected)

This is different from X4. X4 reads UART0_RXD (GPIO 20). On X3, GPIO 20 is I²C SDA. It goes HIGH when the bus is idle. If device detection does not run first, the X3 can read this as "USB connected". Then the device sleeps on a cold start. It does not do a usual start.

### Power States

Same as X4:
- **Active** — Usual operation
- **Deep sleep** — Very low power (wake on power button GPIO 3)

---

## Pin Summary

Most pins are the same as the X4 (see [X4 Specifications § Pin Summary](x4-specifications.md#pin-summary)). The differences:

- **GPIO 0** — I²C SCL (X4: Battery ADC)
- **GPIO 20** — I²C SDA (X4: UART0_RXD / USB detect)

---

## Cache Path

X3 page caches are in `/.papyrix/cache/x3/` (compared to `/.papyrix/cache/` for X4). This prevents layout mismatches when you move an SD card between devices. X3 pages use a 528×792 viewport, not the X4 480×800 viewport.

Source: `src/drivers/Device.cpp` — `Device::cacheDir()`
