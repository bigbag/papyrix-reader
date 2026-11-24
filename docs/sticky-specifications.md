# reTerminal Sticky specifications

## Support status

One SSD1677 device passes USB installation, startup, SD access, Wi-Fi keyboard input, and touch checks with USB connected. The SSD2677 panel and stock recovery are not verified on hardware.

Sticky has no front light, native USB storage, BLE, or swipe navigation in PapyriX.

## Build target

- Development environment: `sticky`
- Release environment: `release_sticky`
- Target macro: `PAPYRIX_TARGET_STICKY=1`
- CPU: ESP32-S3
- Physical flash: 32 MB QSPI
- PSRAM: 8 MB octal
- Visible panel: 800 by 480 pixels
- One monochrome plane: 48,000 bytes
- Reader cache task and loop stack: 20,480 bytes
- SD transport: shared SPI, not SDMMC
- Console and upload: UART0 through CH343P, not native USB CDC

```sh
pio run -e sticky
pio run -e release_sticky
make package
```

The partition table uses the lower 16 MB of flash. The two app slots start at `0x10000` and `0x650000`. Each slot has `0x640000` bytes.

## Hardware connections

| Function | GPIO and configuration |
|---|---|
| Display SPI | SCK13, MOSI14, CS15, DC16, reset17, BUSY18; 10 MHz |
| Display rail | GPIO47, active-high |
| SD SPI | Shared SCK13/MOSI14, MISO12, CS8; rail10, detect11 |
| GT911 touch | Wire: SDA3, SCL2, IRQ21, reset41, rail42 active-high; 400 kHz; address `0x5D`, alternate `0x14` |
| Sensor bus | Wire1: SDA1, SCL0, 400 kHz |
| BQ27220 | Sensor bus, address `0x55`; factory configuration remains unchanged |
| PCF8563 | Sensor bus, address `0x51`; RTC stores UTC |
| Buttons | Power/AI4, Up5, Down6; active-low |
| Power latches | GPIO45 and GPIO46, held high |
| Charger enable | GPIO39, held low |
| Microphone power | GPIO38, held low; no microphone application is added |
| USB supply | GPIO9 ADC; equal 5.1 kΩ divider resistors |
| UART0 | TX43, RX44; 115200 baud |

Startup configures the touch bus before it resets the GT911. The gauge uses a separate I2C controller and keeps its factory configuration.

Shutdown keeps the power latches and charger enabled. It switches off the display, touch, SD, and microphone rails. In ordinary sleep, GPIO17 stays low while display power is off. Press Power/AI to wake Sticky. Use touch for Confirm, Back, and the reader controls.

## Display behavior

Startup reads the panel identity with command `0x70` and samples BUSY. Identity `0x07` selects SSD2677. Other stable readings use BUSY polarity. Unstable readings block display initialization. The probe does not change SD MISO.

SSD1677 uses the Sticky grayscale waveform. SSD2677 uses full and fast monochrome refresh and absolute grayscale. Fast refresh needs a valid reference image. If the reference is missing, the driver uses a full refresh. SSD2677 refreshes the full frame.

SSD2677 uses fixed OTP settings: full `0xEE`, fast `0x19`, and absolute grayscale `0xFA`. It does not adjust them for temperature.

## USB supply detection

Sticky measures USB voltage with the ADC on GPIO9. The threshold is 2000 mV at the ADC, about 4.0 V at the connector. This value comes from the schematic, not hardware measurements.

## Installation

**Switching firmware can delete settings and other data in internal flash.** The USB installer does not erase the SD card. To keep the data, save a private 32 MB flash backup and a separate SD backup. Do not change eFuses or security settings.

### Install from source

Connect Sticky with a USB-C data cable. Close any serial monitor. Install `esptool` and run:

```sh
make install-sticky
```

Confirm the device and type `install`. The installer writes the bootloader, partition table, OTA data, and app. It clears internal LittleFS, not the whole chip.

Keep USB connected until the write check and reset finish. If the screen stays asleep, hold Power/AI for half a second.

Both USB targets select the only port with ID `1A86:55D3`. The ID identifies the USB bridge, not the board. Disconnect other devices with the same bridge or set `PLATFORMIO_UPLOAD_PORT` to the confirmed Sticky port. The targets reject ports with a different ID. On Linux, use a `/dev/serial/by-id/` path when available.

### Install from a bundle

Download and extract `papyrix-sticky-install.zip`, or build it with `make package`. Use the part list and offsets in `manifest.json`. Run this command from the extracted folder. Replace `PORT` with the confirmed Sticky port.

```sh
esptool --chip esp32s3 --port PORT --baud 460800 write-flash --flash-size 32MB --flash-mode dio --flash-freq 80m \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 papyrix-sticky.bin 0xc90000 littlefs-blank.bin
```

Never write the app-only image at `0x0`.

## Updates and recovery

For an SD update, copy `papyrix-sticky.bin` to the SD card as `/firmware.bin`. Open **Settings → Firmware Update**. Use the Sticky image only. See [firmware update compatibility](device-support-matrix.md#firmware-update-compatibility) for validation limits.

For a USB update, run `make flash-sticky`. It checks the partition table before it writes. It does not clear LittleFS. If it rejects a stock partition table, use `make install-sticky`.

### Return to stock firmware

1. Connect Sticky with a USB-C data cable. Close any serial monitor.
2. Open [Seeed's official firmware installer](https://www.seeedstudio.com/sticky/playground/official-firmware/) in desktop Chrome or Edge.
3. Select **Sticky official firmware**. If available, select **Erase flash + flash**.
4. Click **Connect & flash** and select the confirmed Sticky port. Keep USB connected until installation finishes.

**Stock recovery does not restore settings or Seeedash pairing.** Do not use the PapyriX SD updater to restore stock firmware.

## References

- [Seeed hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/)
- [Seeed schematic](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf)
