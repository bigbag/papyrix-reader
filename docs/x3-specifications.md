# Xteink X3 Specifications

See [Device Specifications](device-specifications.md) for shared hardware information.

The C3 firmware supports both X3 and X4. Startup selection uses the policy below.
See [Hardware Selection](device-specifications.md#hardware-selection).

---

## Display

### Panel

- **Size** — 3.68 inches diagonal
- **Resolution** — 792 × 528 pixels (landscape), 528 × 792 (portrait)
- **Pixel Density** — approximately 259 PPI
- **Levels** — Four grayscale levels

### Display Controllers

X3 production units use one of two display controllers:

- **UC8253** — Original X3 controller. The controller uses a 10 MHz SPI clock.
- **UC8279d** — New X3 controller. The controller uses a 20 MHz SPI clock and external LUT data.

Both controllers use 4-wire SPI, Mode 0, and MSB-first data. See [X3 UC8279d Driver Reference](x3-uc8279-driver-reference.md) for the UC8279d register script, refresh sequences, and probe signatures.

### Controller Detection

Device detection runs first. An X4 uses SSD1677. An X4 does not run this probe. On an X3, the firmware powers the SD rail. It then drives SD CS HIGH.

The controller selection order is:

1. The firmware uses a valid `epd_ovr` service override.
2. The firmware uses a valid `epd_det` controller cache.
3. The firmware runs the live display probe.
4. The firmware uses UC8253 after an inconclusive probe.

The keys use the `papyrix_hw` NVS namespace. The `epd_ovr` values are `0` for automatic detection, `1` for UC8253, and `2` for UC8279d. The `epd_det` values are `0` for no cache, `1` for UC8253, and `2` for UC8279d. The `epd_ver` key contains the cache format version.

#### Live Probe

The probe reads `FLG` (0x71) and `VER` (0x70). It reads `RMTP` (0xA2) after a driven idle `FLG` response. It reads `RMTP` a second time when the first dump has no `0xA5` key and is not uniform. The first screening pass uses a 1 ms reset. The firmware repeats a failed screening pass with a 50 ms reset. The firmware reads the second sample after an independent reset. The result contains two samples. The probe uses a maximum of three read sequences.

A structured UC8279d result has these properties:

- Each `FLG` value shows a driven idle state.
- Each `VER` response is not uniform.
- The two `VER` responses are equal.

A uniform UC8279d result has these properties:

- Each `VER` response contains only `0xFF` values.
- Each `FLG` value shows a driven idle state.
- The `MTP` dump satisfies one of these conditions:
  - `MTP[0]` contains `0xA5`.
  - The dump is not uniform and the second `RMTP` read returns the same 48 bytes.

Field UC8279d modules have a blank `MTP`. The dump contains zeros and the LUT version stamp at offset `0x1A`. The controller drives this readback. A floating bus cannot repeat 48 bytes. The firmware writes the probe samples, the `MTP` dump, and the verdict to the serial log with the `DEVICE` tag. The `MTP` line shows `(repeat matched)` after a successful second read.

A UC8253 result has equal uniform `VER` responses and one of these shapes:

- Equal `FLG` values of `0x00` or `0xFF`.
- A driven idle `FLG` value and a uniform `MTP` dump. The UC8253 has no `RMTP` command. The line floats.

All other results are inconclusive.

#### Cache Across Boot Modes

The firmware stores a conclusive result in `epd_det`. It stores the cache format in `epd_ver`. It does not store an inconclusive result. It uses UC8253 for that start.

PapyriX restarts when it changes between UI mode and Reader mode. A valid cache prevents a new probe after these restarts. The firmware runs a new probe when no valid cache exists. An inconclusive probe runs again at each start. The `epd_ovr` override does not change the automatic cache.

### Framebuffer

**Calculation:** 792 pixels / 8 bits = 99 bytes for each row × 528 rows = 52,272 bytes

The firmware allocates a static buffer of `MAX_BUFFER_SIZE = 52,272` bytes. This buffer holds the larger X3 frame. The X4 frame uses 48,000 bytes.

### Refresh Modes

The UC8253 uses the X3 Full, Half, Turbo, Image, and Grayscale LUT sets. Fast refreshes use Turbo. Half refreshes use the Half scrub bank without a flash. Full refreshes use Image, then one no-op Turbo pass. See [X3 LUT Waveforms](x3-lut-waveforms.md) for timing data and register-level documentation.

The UC8279d uses an external GC waveform for full and half refreshes. It uses an external DU waveform for fast refreshes after it has a valid previous-frame baseline. The first two black-and-white refreshes use GC. A resynchronization refresh also uses GC.

Both X3 controllers use a full-frame fast refresh for a window request. The UC8253 has no window RAM commands. The UC8279d driver does not support a safe small window update.

The UC8279d grayscale path uses two 1-bit planes and the external XTF AA waveform.

### Display Pin Mapping

The X3 uses the same display pins as the X4:

- **SCLK** — GPIO 8 — Output — SPI Clock
- **MOSI** — GPIO 10 — Output — SPI Data Out
- **CS** — GPIO 21 — Output — Chip Select (active LOW)
- **DC** — GPIO 4 — Output — Data/Command select
- **RST** — GPIO 5 — Output — Hardware reset (active LOW)
- **BUSY** — GPIO 6 — Input — Busy status (LOW = busy)

### UC8253 LUT Architecture

The UC8253 uses a different LUT structure from the X4. The X4 uses one 111-byte register (command 0x32). The UC8253 has **five LUT registers**. Each register is 42 bytes with 7 phases:

- **VCOM** (0x20) — Common voltage waveform
- **WW** (0x21) — White → White transition
- **BW** (0x22) — Black → White transition
- **WB** (0x23) — White → Black transition
- **BB** (0x24) — Black → Black transition

See [X3 LUT Waveforms](x3-lut-waveforms.md) for the full structure, voltage encoding, and frame group calculations.

### Frame Transfer Timing

At 10 MHz SPI, a UC8253 full-frame transfer takes approximately 42 ms. At 20 MHz SPI, a UC8279d full-frame transfer takes approximately 21 ms. The host writes two planes for a differential refresh.


## I²C Bus

The X3 has an I²C bus. The firmware uses it for battery monitor and device detection:

- **SDA** — GPIO 20
- **SCL** — GPIO 0
- **Frequency** — 400 kHz

The bus connects three chips:

- **BQ27220** (0x55) — Battery fuel gauge. The firmware uses it for the battery level and USB detection.
- **DS3231** (0x68) — Real-time clock. The firmware detects it for device identification scoring.
- **QMI8658** (0x6B / alternative 0x6A) — 6-axis IMU. The firmware detects it for device identification scoring.

**Note:** GPIO 0 is the battery ADC pin on X4. GPIO 20 is UART0_RXD (USB detect) on X4. The pins have different functions. Battery monitor and USB detection use different methods on each device.

---

## Power Management

### Battery Monitoring (BQ27220)

- **Method** — I²C fuel gauge (BQ27220)
- **I²C Address** — 0x55
- **State of Charge** — Register 0x2C (0-100%). The chip calibrates this value.
- **Voltage** — Register 0x08 (millivolts)
- **Polling** — The firmware waits at least one second between reads.
- **Error handling** — The firmware uses cached values after short I²C errors.

### USB Detection

- **Method** — BQ27220 current register plus the native USB peripheral
- **Logic** — Positive current means the battery charges, so external power is present. Near-zero current does not tell cable state: charge termination at 100 % reads as "not connected" in the same way as an unplugged cable. The native USB serial/JTAG peripheral reports an attached host without this limit.

The X4 reads UART0_RXD on GPIO20. The X3 uses GPIO20 for I²C SDA. The pin is HIGH when the bus is idle. If device detection does not run first, the firmware can detect a false USB connection. The device can then enter sleep during a cold startup.

### SD Power and Sleep

GPIO13 is the active-high SD power enable on X3. The firmware releases the GPIO hold before it drives GPIO13 HIGH. It waits 10 ms for the SD rail. It then drives SD CS on GPIO12 HIGH. The firmware probes the display after these operations. Before deep sleep, it closes SD access and drives GPIO13 LOW. It holds this level during deep sleep. The firmware also holds display RESET on GPIO5 HIGH after the controller enters deep sleep.

The power button on GPIO3 wakes the device from deep sleep.

---

## Pin Summary

Most pins are the same as the X4. See [X4 Specifications § Pin Summary](x4-specifications.md#pin-summary). These pins are different:

- **GPIO 0** — I²C SCL (X4: Battery ADC)
- **GPIO 13** — SD power enable, active HIGH (X4: battery power latch)
- **GPIO 20** — I²C SDA (X4: UART0_RXD / USB detect)

---

## Cache Path

The firmware stores X3 page caches in `/.papyrix/cache/x3/`. It stores X4 page caches in `/.papyrix/cache/`. The separate paths prevent layout errors when you move an SD card between devices. X3 pages use a 528×792 viewport. X4 pages use a 480×800 viewport.
