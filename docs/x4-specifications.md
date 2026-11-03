# Xteink X4 Specifications

These specifications describe the original ESP32-C3 Xteink X4. They do not apply to the ESP32-S3 X4 v2 Classic. See the [Classic specifications](x4-classic-specifications.md) for its connections and support limits. See [Device Specifications](device-specifications.md) for firmware selection and device services. The [Pin Summary](#pin-summary) below lists the X4 connections.

---

## Display

### Panel

- **Model** — GDEQ0426T82
- **Size** — 4.26 inches diagonal
- **Resolution** — 800 × 480 pixels
- **Pixel Density** — approximately 217 PPI
- **Colors** — Black/White (4-level grayscale)
- **Viewing Angle** — 180 degrees

### Display Controller (SSD1677)

- **Interface** — 4-wire SPI
- **SPI Clock** — 10 MHz
- **SPI Mode** — Mode 0 (CPOL=0, CPHA=0)
- **Data Order** — MSB First

The X4 board profile sets the display SPI clock in [`BoardProfiles.cpp`](../lib/BoardSupport/src/BoardProfiles.cpp). The display driver uses that value in [`Display.cpp`](../lib/EInkDisplay/src/Display.cpp).

See [SSD1677 Driver Guide](ssd1677-driver.md) for the full command reference and LUT documentation.

### Framebuffer

- **Single buffer** — 48 KB — One framebuffer (current default)
- **Dual buffer** — 96 KB — BW + RED RAM for differential updates

**Calculation:** 800 pixels / 8 bits = 100 bytes for each row × 480 rows = 48,000 bytes

### Refresh Modes

- **Full refresh** — approximately 1600 ms — Page turns, complete redraw
- **Fast refresh** — approximately 600 ms — Quick updates with custom LUT
- **Half refresh** — approximately 1720 ms — Decreased ghosting, no flash
- **Partial (window)** — approximately 50-100 ms — Status bar, UI elements

#### Refresh Mode Details

- **Full** — Write BW RAM → Copy to RED RAM → Refresh — 48KB write + copy + 1.5s waveform
- **Half** — Write BOTH BW and RED RAM separately → Refresh — 48KB × 2 writes + 1.5s waveform
- **Fast** — Write BW RAM only → Short waveform — 48KB write + 0.5s short waveform

**Why Half refresh takes more time than Full:**
- Full refresh writes 48KB to BW RAM, then uses a fast internal copy to RED RAM.
- Half refresh writes 48KB two times (to each RAM) = 96KB total transfer.
- The extra approximately 120ms is the second 48KB SPI transfer.
- "Half" refers to decreased voltage swing in the waveform, not time.

### Display Pin Mapping

- **SCLK** — GPIO 8 — Output — SPI Clock
- **MOSI** — GPIO 10 — Output — SPI Data Out
- **CS** — GPIO 21 — Output — Chip Select (active LOW)
- **DC** — GPIO 4 — Output — Data/Command select
- **RST** — GPIO 5 — Output — Hardware reset (active LOW)
- **BUSY** — GPIO 6 — Input — Busy status (HIGH = busy; see [`Display.cpp`](../lib/EInkDisplay/src/Display.cpp))

### LUT Table Structure (111 bytes)

The X4 uses one 111-byte LUT loaded with command 0x32:

- **0-49** — 50 bytes — VS waveforms (5 groups × 10 bytes)
- **50-99** — 50 bytes — TP/RP timing (10 groups × 5 bytes)
- **100-104** — 5 bytes — Frame rate control
- **105** — 1 byte — VGH (gate high voltage)
- **106** — 1 byte — VSH1 (source high 1)
- **107** — 1 byte — VSH2 (source high 2)
- **108** — 1 byte — VSL (source low)
- **109** — 1 byte — VCOM
- **110** — 1 byte — Reserved

### Voltage Settings

- **VGH** — `0x17` — Gate high (approximately 17-22V)
- **VSH1** — `0x41` — Source high 1 (approximately 15V)
- **VSH2** — `0xA8` — Source high 2
- **VSL** — `0x32` — Source low (negative)
- **VCOM** — `0x30` — Common voltage

### SSD1677 Commands

- **SWRESET** — `0x12` — Software reset
- **DRIVER_OUTPUT** — `0x01` — Gate driver control
- **DATA_ENTRY** — `0x11` — X/Y direction mode
- **RAM_X_ADDR** — `0x44` — Set X address range
- **RAM_Y_ADDR** — `0x45` — Set Y address range
- **RAM_X_CNT** — `0x4E` — Set X counter
- **RAM_Y_CNT** — `0x4F` — Set Y counter
- **WRITE_BW** — `0x24` — Write to BW RAM
- **WRITE_RED** — `0x26` — Write to RED RAM
- **MASTER_ACT** — `0x20` — Activate display update
- **UPDATE_SEQ** — `0x22` — Display update sequence
- **WRITE_LUT** — `0x32` — Write LUT table
- **DEEP_SLEEP** — `0x10` — Enter deep sleep

### Display Refresh Sequence

```
1. Set the RAM area and address counters.
2. Write the current image to BW RAM (0x24).
3. For FULL or HALF, write the image to RED RAM (0x26).
4. Set display update controls (0x21 and 0x22).
5. Send master activation (0x20).
6. Wait for BUSY LOW.
7. After success, set the RAM area again.
8. Write the displayed image to both BW and RED RAM.
```

---

## Power Management

### Battery Monitoring

- **Method** — ADC voltage divider
- **ADC Pin** — GPIO 0
- **Voltage range** — 0-3.3V (after divider)
- **Divider ratio** — 2:1
- **Full battery** — approximately 4.2V
- **Empty battery** — approximately 3.0V

### Battery Percentage Formula

Uses polynomial fit for LiPo discharge curve:
```
percentage = -144.9390*V^3 + 1655.8629*V^2 - 6158.8520*V + 7501.3202
```
V is battery voltage (limited to the 0-100% range).

Each status read derives voltage and raw percentage from one ADC sample. The X4 HAL limits samples to one every five seconds and applies a 9:1 exponential moving average in tenths of a percent. It keeps the filtered estimate in RTC memory across UI, Reader, and deep-sleep restarts. A true power-on discards the retained estimate. A missing marker, an invalid range, or a raw-to-retained difference above 25 percentage points also resets the estimate to the raw

value. Home, Reader, Clock, and System Info use this one filtered status.

### USB Detection

- **Method** — UART0_RXD pin (GPIO 20)
- **Logic** — HIGH = USB connected

### Power States

- **Active** — The firmware drives GPIO13 HIGH before serial initialization to latch battery power.
- **WiFi active** — Approximately 150 mA.
- **Battery-only sleep** — The firmware drives GPIO13 LOW and holds it for the lowest-power hardware shutdown path.
- **USB-connected sleep** — The firmware drives and holds GPIO13 HIGH so charging continues.
- **Wake initialization** — The firmware configures GPIO13 HIGH before it releases its sleep hold or initializes serial, USB, or buttons.
- **USB edge** — A connect or disconnect edge requests a full Home render. This replaces a stale BootView frame after the X4 power transition.

Clock provides Normal and Power saving modes. Normal keeps the 10 ms idle button interval and the existing CPU idle policy. Power saving enters deep sleep after 15 seconds without interaction. It wakes at the next minute boundary and updates the clock. GPIO13 stays HIGH during this standby; the battery shutdown path is not used.

A short Power press on GPIO3 opens Clock controls directly. USB does not disable Clock standby.

The SSD1677 path restores both monochrome RAM planes from the retained frame. It uses the existing framebuffer, not a second C3 allocation. Clock clears automatic resume after a fatal wake error. Startup disables automatic GPIO sleep switching for the X4 battery latch. Normal shutdown drives the latch LOW on battery.

Battery percentage is a voltage estimate, not a current measurement.

---

## Pin Summary

- **GPIO 0** — Battery ADC (Input)
- **GPIO 1** — Button ADC 1 — Back/Confirm/Left/Right (Input)
- **GPIO 2** — Button ADC 2 — Up/Down (Input)
- **GPIO 3** — Power button, pullup (Input)
- **GPIO 4** — Display DC (Output)
- **GPIO 5** — Display RST (Output)
- **GPIO 6** — Display BUSY (Input)
- **GPIO 7** — SD MISO (Input)
- **GPIO 8** — SPI SCLK (Output)
- **GPIO 10** — SPI MOSI (Output)
- **GPIO 12** — SD CS (Output)
- **GPIO 13** — Battery power latch (Output)
- **GPIO 20** — UART0_RXD / USB detect (Input)
- **GPIO 21** — Display CS (Output)

---

## Sunlight Fading

Enable **Settings > Screen > Sunlight Fading Fix** if the screen fades in sunlight. The setting powers down the controller after each refresh. See [Sunlight Fading](ssd1677-driver.md#sunlight-fading).
