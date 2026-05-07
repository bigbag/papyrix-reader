# X3 E-Ink LUT Waveform Reference

Documentation for the SSD1677 look-up table (LUT) waveforms used on the Xteink X3 (792x528, 3.68" e-ink panel).

---

## LUT Format

Each LUT is 42 bytes covering up to 7 phases. Each phase is 6 bytes:

- **Byte 0 — VS**: Voltage select for 4 sub-phases (A/B/C/D), 2 bits each
- **Byte 1 — TP0**: Frame groups for sub-phase A
- **Byte 2 — TP1**: Frame groups for sub-phase B
- **Byte 3 — TP2**: Frame groups for sub-phase C
- **Byte 4 — TP3**: Frame groups for sub-phase D
- **Byte 5 — RP**: Repeat count (0 = phase inactive)

Total frame groups per phase = (TP0 + TP1 + TP2 + TP3) * RP.

### VS Voltage Encoding (2 bits per sub-phase)

- `00` — GND: no drive (hold)
- `01` — VDH: positive drive (towards black)
- `10` — VDL: negative drive (towards white)
- `11` — VCOM: common electrode

### Frame Group Timing

At PLL=0x09 (CMD 0x30), each frame group takes ~18.2ms.

---

## Five LUT Registers

- **VCOM** (CMD 0x20) — common electrode waveform
- **WW** (CMD 0x21) — white-to-white (unchanged white pixels)
- **BW** (CMD 0x22) — black-to-white transition
- **WB** (CMD 0x23) — white-to-black transition
- **BB** (CMD 0x24) — black-to-black (unchanged black pixels)

The controller compares RAM 0x10 (old frame) vs RAM 0x13 (new frame) per pixel to determine which LUT applies.

---

## LUT Sets in Papyrix

### `lut_x3_*_full` — Quality Refresh

Used for: full sync, conditioning passes, and the periodic "pages per refresh" clean update.

```
Phase 0: TP=(6,2,6,6) RP=1 = 20 frame groups
Phase 1: TP=(5,1,0,0) RP=1 =  6 frame groups
Total: 26 frame groups, ~472ms
```

VS patterns per LUT:

- **VCOM**: Phase 0 = GND,GND,GND,GND / Phase 1 = GND,GND,GND,GND
- **WW**: Phase 0 = GND,VDL,GND,GND / Phase 1 = GND,GND,GND,GND
- **BW**: Phase 0 = VDL,VDL,VDL,VDL / Phase 1 = VDL,GND,GND,GND
- **WB**: Phase 0 = VDH,VDH,VDH,VDH / Phase 1 = VDH,GND,GND,GND
- **BB**: Phase 0 = GND,VDH,GND,GND / Phase 1 = GND,GND,GND,GND

Phase 0 drives the main pixel transition. Phase 1 is a settle pass (extra drive pulse for BW/WB, GND hold for unchanged pixels).

WW/BB get a brief refresh pulse in sub-phase B to prevent accumulated ghosting on unchanged pixels.

### `lut_x3_*_turbo` — Balanced Fast Refresh

Used for: fast differential page turns (the default fast path on X3).

```
Phase 0: TP=(4,2,4,4) RP=1 = 14 frame groups
Phase 1: TP=(4,1,0,0) RP=1 =  5 frame groups
Total: 19 frame groups, ~382ms
```

Same VS patterns as `_full`. Reduced timing gives ~90ms faster page turns with slightly more ghosting. The periodic full refresh (pagesPerRefresh setting) cleans up any accumulated artifacts.

### `lut_x3_*_img` — Image Write (Full Sync)

Used for: initial display write when both RAMs are populated (inverted data).

```
Phase 0: TP=(8,11,2,3) RP=1 = 24 frame groups
Phase 1: TP=(12,2,7,2) RP=1 = 23 frame groups
Phase 2: TP=(1,0,2,0)  RP=1 =  3 frame groups
Total: 50 frame groups, ~908ms
```

Three-phase waveform with complex VS patterns for high-quality initial image rendering.

### `lut_x3_*_gray` — Grayscale (4-level)

Used for: anti-aliased cover images and grayscale rendering.

```
Phase 0: TP=(3,2,1,1) RP=1 = 7 frame groups
Total: 7 frame groups, ~127ms
```

Single-phase with tuned drive strengths per transition to produce 4 gray levels:

- WW (dark gray): brief VDL pulse in sub-phase B
- BW (light gray): VDL pulse in sub-phase A
- WB/BB: GND hold

### `lut_x3_*_fast` — Reserved (Currently Unused)

Defined but not loaded in any code path. Single-phase with TP=(24,24,1,0) = 49 frame groups (~890ms) — slower than `_full`. Kept for reference.

---

## Hardware-Tested Speed Variants

All variants use the same VS voltage patterns as `_full`, only timing differs:

- **`_full`** — 26 groups, ~472ms, no ghosting. Used for quality/full refresh.
- **`_turbo` (balanced)** — 19 groups, ~382ms, slight ghosting. Default fast path.
- **v1 (TP 3,1,3,3 + 3,1)** — 14 groups, ~317ms, moderate ghosting. Tested, usable.
- **DU (TP 3,1,2,0)** — 6 groups, ~215ms, heavy ghosting. Tested, too aggressive for reading.

---

## Controller Configuration

### PLL (CMD 0x30)

Value `0x09` sets the internal oscillator frequency. This determines the duration of each frame group in the LUT waveform. Higher values increase the frame rate (shorter refresh) but risk insufficient pixel drive.

### VCOM Data Interval (CMD 0x50)

- Full sync: `0xA9, 0x07` — border drive active during image write
- Fast differential: `0x29, 0x07` — border held during partial update

### SPI Speed

X3 is limited to 10 MHz SPI (20 MHz causes pixel corruption). X4 runs at 40 MHz. This means each 52KB frame transfer takes ~42ms on X3 vs ~12ms on X4.

---

## Refresh Flow (Fast Differential)

```
1. Load turbo LUTs (skipped if already cached via _x3LoadedLuts)
2. Write new frame to RAM 0x13 (52KB, Y-mirrored)     ~42ms
3. CMD 0x04 — power on charge pump (if screen was off) ~127ms
4. CMD 0x12 — trigger refresh, wait for BUSY           ~382ms
5. Sync RAM 0x10 with current frame (for next diff)    ~42ms
```

The controller reads RAM 0x13 (new) and RAM 0x10 (old) per pixel, selects the matching LUT (WW/BW/WB/BB), and applies the waveform.
