# Troubleshooting

This guide helps developers repair problems on the Xteink X4 / X3 with Papyrix firmware.

---

## Soft-Brick Recovery

> **Note:** Soft-brick must not occur during usual operation. This section is only for developers who flash custom firmware.

### What causes it

A soft-brick occurs when firmware calls deep sleep or light sleep on each start. The CPU powers down immediately after reset. You cannot flash new firmware through the usual USB connection.

### Simple repair: remove the SD card

The easiest recovery method is to remove the SD card and start the device again. With no SD card, the defective code path usually does not start. The device can start far enough for you to flash again.

### Hardware method: download mode through the SD card slot

If removal of the SD card does not help, force the ESP32-C3 into download mode. Pull down a strapping pin through the SD card slot.

**Background:** The ESP32-C3 uses strapping pins that it samples at start to select the boot mode. If you pull GPIO9 low during start, the device goes into download mode. Then you can flash again through USB.

**Strapping pins on the Xteink X4:**

- **GPIO8** — Display/SD SPI CLK. You cannot get access with no disassembly.
- **GPIO9** — SD card CLK. You can get access through the SD card slot.
- **GPIO2** — Button ADC 2. This pin is not useful ([it does nothing for boot mode](https://esp32.com/viewtopic.php?t=31947)).

**Procedure:** Put in a changed SD card (or use a pin/wire) to pull GPIO9 low through the CLK contact of the SD card slot during start. This forces the ESP32-C3 into download mode. Then you can flash firmware again through USB.

> This method was tested on a standalone ESP32-C3 board. It is not verified on the Xteink X4 device.

### Schematic reference

The Xteink X4 schematic that shows the ESP32-C3 pin connections is at:
[Xteink X4 Schematic](https://github.com/sunwoods/Xteink-X4/blob/main/readme-img/sch.jpg)
(from the [sunwoods/Xteink-X4](https://github.com/sunwoods/Xteink-X4) repository)

### Attribution

[ngxson](https://github.com/ngxson) wrote this recovery procedure in [crosspoint-reader/crosspoint-reader#573](https://github.com/crosspoint-reader/crosspoint-reader/discussions/573).
