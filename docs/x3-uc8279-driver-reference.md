# Xteink X3 UC8279d Driver Reference

This document gives the register-level reference for the UC8279d controller in newer Xteink X3 units. It supports `lib/EInkDisplay/src/Uc8279X3Driver.cpp`, `lib/EInkDisplay/src/Uc8279X3Luts.h`, and `lib/EInkDisplay/src/X3DisplayProbe.cpp`. See [Xteink X3 Specifications](x3-specifications.md) for the device overview. See [X3 LUT Waveforms](x3-lut-waveforms.md) for the UC8253 tables.

The waveform tables, the init script, and the refresh sequences come from the FreeInk SDK (`libs/display/FreeInkDisplay/src/lut/Uc8279X3Luts.h`, `libs/hardware/XteinkDetect`, `docs/xteink-x3-uc8279-support.md`). FreeInk recovered them from the stock Xteink X3 firmware (`update.bin`, July 2026, descriptor set `ZHX368_8279`). The license is in `lib/EInkDisplay/FREEINK_LICENSE`.

---

## Controller Identification

- **Controller** — UC8279d_B (UltraChip), KW mode
- **Panel** — ZHX368, 792 × 528, `LUT_VER` 0x66
- **Interface** — 4-wire SPI, Mode 0, MSB first. Reads use half-duplex SDA.
- **SPI clock** — 20 MHz maximum. The datasheet gives "Clock rate up to 20 MHz".
- **BUSY** — GPIO 6, `BUSY_N`. LOW = busy. HIGH = idle.
- **MTP** — Blank on field modules. The host must send all registers and all waveforms.

The module MTP does not contain the `0xA5` refresh-enable key, a Command Default Setting block, or temperature LUTs. A driver that uses `PSR REG=0` (OTP waveforms) leaves the panel dark.

### Read Commands

- `0x70` **VER** — 5 bytes: reserved `0x00`, `CHIP_VER` (datasheet default `0x03`), `LUT_VER[23:0]`
- `0x71` **FLG** — 1 status byte. Bit 0 is `BUSY_N` (1 = idle). The datasheet idle default is `0x13`.
- `0xA2` **RMTP** — 1 dummy byte, then `MTP[0..n]`

MTP layout (first 48 bytes):

- `0x000` — `0xA5` refresh-enable key (programmed MTP only)
- `0x001`–`0x016` — Factory Command Default Setting (PSR, TRES, GSST, CDI, TCON)
- `0x017`–`0x019` — Product ID
- `0x01A`–`0x027` — LUT version
- `0x028` and above — Temperature boundaries

### Identification Responses

The X3 probe uses the stock V6.3.15 protocol from FreeInk SDK.
It reads only the first three VER bytes.
The third byte `0x66` identifies UC8279d.
The third byte `0xFF` selects UC8253.
Other values are inconclusive.
The probe does not use FLG or MTP responses.

### Probe Timing

- Reset pulse: RST HIGH 10 ms, LOW 50 ms, then HIGH for 50 ms.
- Bit-bang clock: approximately 500 kHz. Release SDA without a pull-up. Sample each bit after SCLK becomes HIGH.
- Release the `gpio_hold` on RST before the reset pulse. The deep-sleep hold survives the wake reset.
- Keep SD CS (GPIO 12) HIGH during the probe. The SD card shares SCLK 8 and MOSI 10.
- Wait up to 300 ms for BUSY HIGH. Read VER even after a timeout.

---

## Power-On Register Script

Send this script after a hardware reset (RST HIGH 10 ms, LOW 10 ms, HIGH 10 ms, then 50 ms settle). FreeInk recovered it from stock firmware function `FUN_42014ad4`.

- `0x00` **PSR** — `3F 4A`. `REG=1` external LUT, KW mode.
- `0x91` **PTIN** — Enter partial window.
- `0x90` **PTL** — `00 00 03 17 00 00 02 0F 01`. X 0–791, Y 0–527 (792 × 528).
- `0x03` **PFS** — `20`
- `0x01` **PWR** — `43 00 78 78 17`. `VS_EN`, `VG_EN`. VSH = VSL = `0x78`. VDHR = `0x17`.
- `0x82` **VDCS** — `24`. VCOM DC.
- `0x06` **BTST** — `25 25 3C`. Booster.
- `0x30` **PLL** — `0F`. Frame rate.
- `0xE1` — `02`. Gate scan.

---

## Data Planes

- `0x10` **DTM1** — OLD plane (last displayed frame)
- `0x13` **DTM2** — NEW plane (target frame)
- `0x11` **DSP** — Data stop. Send after each plane.

Rules:

- The plane size is 99 bytes × 528 rows = 52,272 bytes. 1 = white. 0 = black.
- The driver sends rows in reverse order (row 527 first). The stock firmware uses the same orientation.
- Write both planes inside the PTIN window. A DTM1 write outside the window misaligns the planes. The DU diff then drives incorrect data.
- After each refresh, write DTM1 = the displayed frame. The next refresh diffs against the real on-screen content.
- Seed DTM1 white only on the first paint after init. A white baseline on later refreshes leaves unchanged high-contrast pixels undriven (ghosting).

---

## Refresh Sequences

### B/W GC (full and half refresh)

The stock firmware function is `FUN_42015786`.

1. PTIN (`0x91`)
2. DTM2 = new frame, DSP
3. CDI (`0x50`): `0x97` on the first refresh after init, `0xD7` after that
4. Load `BW_GC` into `0x20`–`0x24`
5. PON (`0x04`), wait BUSY
6. DRF (`0x12`), wait BUSY
7. CDI `0xD7`
8. DTM1 = new frame, DSP
9. PTOUT (`0x92`)
10. Optional POF (`0x02`), wait BUSY

`BW_GC` has WW ≠ KW and WK ≠ KK. It clears through the true old-to-new transition. Do not write `E0`/`E5` in a B/W refresh.

### B/W DU (fast refresh)

The stock firmware function is `FUN_4201580a`. The DU sequence is the GC sequence with `BW_DU`. Use DU only when DTM1 holds a valid baseline. The first two refreshes after init use GC.

A DU refresh does not drive unchanged pixels. On a dark background, the light residue of each white-to-black transition collects between GC passes. Before a DU refresh on inverted content, write DTM1 = the complement of the new frame. Every pixel then classifies as changed. Every pixel drives toward its target. Restore DTM1 = new frame after the refresh.

### 4-Level Grayscale (XTF AA)

The stock firmware functions are `FUN_42015108` (planes) and `FUN_42013be0` (LUT load).

1. PTIN, PTL (full window)
2. DTM1 = plane A (LSB), DSP
3. DTM2 = plane B (MSB), DSP
4. PTOUT
5. Load `XTF_AA`: for each of `0x20`–`0x24`, send the command, then 49 raw bytes
6. CDI (`0x97` first, `0xD7` later)
7. PON, DRF, wait BUSY

This is the stock firmware sequence. The PapyriX driver keeps the window open until the refresh completes. The gray planes overwrite DTM1 and DTM2. The next B/W refresh seeds DTM1 white and uses GC. The FreeInk driver adds an optional pre-conditioning pass before the gray frame. The pass uses `XTF_PRE_BW_MID` with CCSET (`0xE0`) = `0x02` and TSSET (`0xE5`) = `0x5A` (stock function `FUN_42015944`). PapyriX does not implement this pass. `XTH4` is an alternative built-in 4-gray table set.

### Deep Sleep

1. If the screen is on: POF (`0x02`), wait BUSY
2. DSLP (`0x07`), data `0xA5`

A hardware reset and the full register script wake the controller.

---

## Waveform Table Format

- `BW_GC` — 5 × 43 bytes. Byte 0 is the register (`0x20`–`0x24`). Then 42 data bytes. Registers: VCOM, WW, BW, WB, BB.
- `BW_DU` — 5 × 43 bytes. Same layout.
- `XTF_PRE_BW_MID` — 5 × 43 bytes. Same layout. Pre-conditioning only.
- `XTF_AA` — 5 × 49 bytes, raw. The loader sends the register separately. 4-level grayscale.
- `XTH4` — 5 × 49 bytes, raw. Built-in 4-gray alternative.

The UC8279d waveforms differ from the UC8253 X3 banks. Do not copy the UC8253 X3 banks.

---

## BUSY Handling

- After PON, DRF, and POF the controller drives `BUSY_N` LOW, then HIGH when complete.
- Wait for the LOW edge with a bounded timeout. Then wait for HIGH. If BUSY does not go LOW within the timeout, the driver treats the command as complete. A RAM write during the waveform corrupts the frame.
- PapyriX uses 1 s for the assertion and 30 s for completion. On a timeout the driver marks DTM1 invalid and forces GC on the next refresh.

---

## References

- FreeInk SDK, `docs/xteink-x3-uc8279-support.md`, `docs/display-driver-references.md`
- [FreeInk SDK revision `83f8e01f`](https://github.com/Free-Ink/freeink-sdk/tree/83f8e01f6f7b1419917f610d7b223387df41fc7d) — stock X3 V6.3.15 detection protocol
- UC8279d_B datasheet 0.1 (December 2025)
- UC8253 datasheet (UltraChip / Good Display): https://www.elecrow.com/download/product/DIE01237S/UC8253_Datasheet.pdf
