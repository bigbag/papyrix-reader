# Troubleshooting

Developer-focused troubleshooting guide for the Xteink X4 running Papyrix firmware.

---

## Soft-Brick Recovery

> **Note:** Soft-bricking should never occur during normal usage. This section is only relevant for developers flashing custom firmware.

### What causes it

A soft-brick happens when firmware calls deep sleep or light sleep on every boot. The CPU powers down immediately after reset, making it impossible to flash new firmware through the normal USB connection.

### Simple fix: remove the SD card

The easiest recovery method is to remove the SD card and reboot the device. Without the SD card, the problematic code path is typically not triggered, allowing the device to boot far enough for re-flashing.

### Hardware method: download mode via SD card slot

If removing the SD card doesn't help, you can force the ESP32-C3 into download mode by pulling down a strapping pin through the SD card slot.

**Background:** The ESP32-C3 uses strapping pins sampled at boot to select the boot mode. Pulling GPIO9 low during boot forces download mode, which allows re-flashing via USB.

**Strapping pins on the Xteink X4:**

- **GPIO8** — Display/SD SPI CLK — not accessible without disassembly
- **GPIO9** — SD card CLK — accessible via the SD card slot
- **GPIO2** — Button ADC 2 — not useful ([does nothing for boot mode](https://esp32.com/viewtopic.php?t=31947))

**Procedure:** Insert a modified SD card (or use a pin/wire) to pull GPIO9 low through the SD card slot's CLK contact during boot. This forces the ESP32-C3 into download mode, allowing you to re-flash firmware via USB.

> This method was tested on a standalone ESP32-C3 board. It has not been verified on the Xteink X4 device itself.

### Schematic reference

The Xteink X4 schematic showing the ESP32-C3 pin connections is available at:
[Xteink X4 Schematic](https://github.com/sunwoods/Xteink-X4/blob/main/readme-img/sch.jpg)
(from the [sunwoods/Xteink-X4](https://github.com/sunwoods/Xteink-X4) repository)

### Attribution

This recovery procedure was documented by [ngxson](https://github.com/ngxson) in [crosspoint-reader/crosspoint-reader#573](https://github.com/crosspoint-reader/crosspoint-reader/discussions/573).
