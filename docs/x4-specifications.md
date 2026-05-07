# Xteink X4 Specifications

Hardware specifications specific to the Xteink X4 e-reader. For shared hardware (ESP32-C3, buttons, SD card, build config), see [Device Specifications](device-specifications.md).

---

## Display

### Panel

- **Model** — GDEQ0426T82
- **Size** — 4.26 inches diagonal
- **Resolution** — 800 × 480 pixels
- **Pixel Density** — ~217 PPI
- **Colors** — Black/White (4-level grayscale)
- **Viewing Angle** — 180 degrees

### Display Controller (SSD1677)

- **Interface** — 4-wire SPI
- **SPI Clock** — 40 MHz
- **SPI Mode** — Mode 0 (CPOL=0, CPHA=0)
- **Data Order** — MSB First

See [SSD1677 Driver Guide](ssd1677-driver.md) for the full command reference and LUT documentation.

### Framebuffer

- **Single buffer** — 48 KB — One framebuffer (current default)
- **Dual buffer** — 96 KB — BW + RED RAM for differential updates

**Calculation:** 800 pixels / 8 bits = 100 bytes per row × 480 rows = 48,000 bytes

### Refresh Modes

- **Full refresh** — ~1600 ms — Page turns, complete redraw
- **Fast refresh** — ~600 ms — Quick updates with custom LUT
- **Half refresh** — ~1720 ms — Reduced ghosting, no flash
- **Partial (window)** — ~50-100 ms — Status bar, UI elements

#### Refresh Mode Details

- **Full** — Write BW RAM → Copy to RED RAM → Refresh — 48KB write + copy + 1.5s waveform
- **Half** — Write BOTH BW and RED RAM separately → Refresh — 48KB × 2 writes + 1.5s waveform
- **Fast** — Write BW RAM only → Short waveform — 48KB write + 0.5s short waveform

**Why Half refresh takes longer than Full:**
- Full refresh writes 48KB to BW RAM, then uses fast internal copy to RED RAM
- Half refresh writes 48KB twice (to each RAM separately) = 96KB total transfer
- The extra ~120ms is the second 48KB SPI transfer
- "Half" refers to reduced voltage swing in the waveform, not time

### Display Pin Mapping

- **SCLK** — GPIO 8 — Output — SPI Clock
- **MOSI** — GPIO 10 — Output — SPI Data Out
- **CS** — GPIO 21 — Output — Chip Select (active LOW)
- **DC** — GPIO 4 — Output — Data/Command select
- **RST** — GPIO 5 — Output — Hardware reset (active LOW)
- **BUSY** — GPIO 6 — Input — Busy status (LOW = busy)

### LUT Table Structure (111 bytes)

The X4 uses a single 111-byte LUT loaded via command 0x32:

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

- **VGH** — `0x17` — Gate high (~17-22V)
- **VSH1** — `0x41` — Source high 1 (~15V)
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
1. Send 0x4E (Set X address) + start column
2. Send 0x4F (Set Y address) + start/end row
3. Send 0x24 (Write BW RAM)
4. Send 48,000 bytes of image data
5. Send 0x26 (Write RED RAM) [for differential refresh]
6. Send 48,000 bytes of previous frame
7. Send 0x20 (Master activation)
8. Send 0x22 (Display refresh trigger)
9. Wait for BUSY pin LOW
```

---

## Power Management

### Battery Monitoring

- **Method** — ADC voltage divider
- **ADC Pin** — GPIO 0
- **Voltage range** — 0-3.3V (after divider)
- **Divider ratio** — 2:1
- **Full battery** — ~4.2V
- **Empty battery** — ~3.0V

### Battery Percentage Formula

Uses polynomial fit for LiPo discharge curve:
```
percentage = -144.9390*V^3 + 1655.8629*V^2 - 6158.8520*V + 7501.3202
```
Where V is battery voltage (clamped to 0-100% range).

### USB Detection

- **Method** — UART0_RXD pin (GPIO 20)
- **Logic** — HIGH = USB connected

### Power States

- **Active** — Normal operation (~50 mA)
- **WiFi active** — ~150 mA
- **Deep sleep** — ~10 µA (wake on power button GPIO 3)

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
- **GPIO 20** — UART0_RXD / USB detect (Input)
- **GPIO 21** — Display CS (Output)

---

## Sunlight Fading

The X4's SSD1677 driver IC is packaged as "Gold Bump Die" without resin protection, making it susceptible to UV radiation. White X4 devices are more affected. Enable **Sunlight Fading Fix** in Settings to power down the display after each refresh (~100-200ms overhead per page turn). See [SSD1677 Driver Guide § Sunlight Fading](ssd1677-driver.md#sunlight-fading-issue) for details.
