# reTerminal Sticky specifications

## Support status

**Implemented, not device-tested.**
No Sticky device is available for this port.
Host tests, firmware builds, and package checks do not prove physical operation.
Both SSD1677 and SSD2677 paths are included.

The port uses the existing PapyriX states, views, content providers, applications, and UI/Reader reboot model.
It does not require Seeedash sources, accounts, or private services.
The port does not add stock applications.

## Stock firmware comparison

Existing functions use PapyriX formats and interfaces.
They do not provide exact stock-firmware compatibility.

| Function | PapyriX support or required addition |
|---|---|
| Clock, images, slideshow, themes, fonts, and sleep images | Existing PapyriX functions |
| Notes and Todo | New mini-apps, SD persistence, and editing controls |
| Timed reminders | New scheduling, time-zone, timer-wake, and missed-reminder policies |
| Auto-rotation and temperature/humidity display | New IMU and SHT40 backends with corresponding UI |
| Weather | A selected data provider, bounded requests, cached data, and error handling |
| Sound and voice | New buzzer/audio backends and a selected speech engine or service |
| AI services | A separate client and an explicit credential policy |
| Phone input | Existing web file upload; text/task editing needs new controls |

These additions are outside the device port.
Any speech service needs an explicit credential and audio-retention policy.
Full offline speech on ESP32-S3 remains unverified.

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

The current partition table uses the lower 16 MB of physical flash.
It keeps two app slots of `0x640000` bytes at `0x10000` and `0x650000`.
The unused upper 16 MB does not increase the app-slot limit.
The port does not change settings serialization.
Profile-specific cache roots prevent reuse of pages from another board or panel.

## Hardware connections

| Function | GPIO and configuration |
|---|---|
| Display SPI | SCK13, MOSI14, CS15, DC16, reset17, BUSY18; 10 MHz |
| Display rail | GPIO47, active-high |
| SD SPI | Shared SCK13/MOSI14, MISO12, CS8; rail10, detect11 |
| GT911 touch | Wire: SDA3, SCL2, IRQ21, reset41, rail42 active-high; address `0x5D`, alternate `0x14` |
| Sensor bus | Wire1: SDA1, SCL0, 400 kHz |
| BQ27220 | Sensor bus, address `0x55`; factory configuration remains unchanged |
| PCF8563 | Sensor bus, address `0x51`; RTC stores UTC |
| Buttons | Power/AI4, Up5, Down6; active-low |
| Power latches | GPIO45 and GPIO46, held high |
| Charger enable | GPIO39, held low |
| Microphone power | GPIO38, held low; no microphone application is added |
| USB supply | GPIO9 ADC; equal 5.1 kΩ divider resistors |
| UART0 | TX43, RX44; 115200 baud |

Touch uses its own I2C controller.
Gauge and RTC reads do not stop the touch controller.
The gauge uses `designCapacityMah=0` and does not run X3 calibration writes.
RTC tests cover voltage loss, date/weekday order, and time repair.

Power shutdown retains both latches and charger enable.
It disables the display, touch, SD, and microphone rails after the display sleep handshake.
Wake remains Power-button-only.
The Power/AI button uses the existing short-Power preferences.
It is not a second Confirm button.
Touch supplies the existing menu, Confirm, Back, and reader-zone actions.
Sticky has no capacitive Home key.

## Display behavior

Startup sends the upstream `0x70` probe and samples BUSY.
Revision `0x07` selects SSD2677.
Other stable readings use the upstream BUSY-polarity heuristic.
Logs identify a heuristic selection.
Unstable readings leave the panel unresolved and block display initialization.
This heuristic cannot prove that a panel is present in every failure condition.
Display probing and recovery preserve the SD MISO connection.

SSD1677 uses the Sticky booster, border, grayscale LUT, and power sequence.
It does not use the original X4 gray-return waveform.
SSD2677 supports full and fast monochrome refresh, absolute gray planes, and the existing AA overlay masks.
A missing or failed monochrome baseline promotes Fast to Full.
Failed gray copies cannot refresh stale planes.
Both controllers report a failed power-off handshake instead of claiming deep sleep.
SSD2677 window requests use the existing whole-frame S3 behavior.

SSD2677 uses fixed room-temperature OTP selections: Full `0xEE`, Fast `0x19`, and absolute gray `0xFA`.
Its upstream AA LUT comes from a different panel and is also used by the reference Sticky driver.
The port does not add automatic temperature compensation.
Temperature response, gray levels, ghosting, and long-term panel behavior require physical checks.

## USB supply detection

Sticky does not infer external power from battery charge current.
It reads calibrated ADC millivolts on GPIO9 with 11 dB attenuation and the shared ADC mutex.
The threshold is 2000 mV at the ADC, or approximately 4.0 V at the connector.
A nominal 5 V supply produces approximately 2500 mV at the ADC.
The threshold uses the schematic's divider ratio.
It is not a hardware calibration.
A full battery can report no charging current while USB power remains connected.

## Installation

**A complete installation replaces stock firmware and can erase device data.**
Save a full 32 MB flash backup and an SD-card backup before installation.
Flash backups can contain credentials.
Keep them private.
A backup and a factory download do not guarantee recovery.
Do not change eFuses or security settings for this port.

`make package` writes these Sticky outputs:

- `dist/papyrix-sticky.bin`: app-only image for an existing compatible PapyriX installation.
- `dist/sticky-install/manifest.json`: complete first-install manifest.
- `dist/papyrix-sticky-install.zip`: manifest and all first-install parts.

The install manifest records SHA-256, size, and offset for each part.
The bundle also contains the applicable FreeInk and CrossMux MIT notices.
The build records the actual upload inputs.
Packaging does not copy upstream firmware binaries.
Packaging checks partition bounds, part overlap, app size, image integrity, and board identity.
The current builder records flash mode `dio`, frequency `80m`, and flash size `32MB`.
The Arduino build uses QSPI flash with octal PSRAM.
Its esptool image/upload mode is `dio`.

For the current generated bundle, run from `dist/sticky-install` and replace `PORT` with the confirmed Sticky serial port:

```sh
esptool --chip esp32s3 --port PORT write-flash --flash-size 32MB --flash-mode dio --flash-freq 80m \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 papyrix-sticky.bin
```

Check the generated manifest before using these offsets with a different build.
Never write the app-only image at `0x0`.
Use a USB-C data cable and the CH343P serial port.
No flash, erase, or recovery operation runs during this implementation.
See [Seeed's development guide](https://www.seeedstudio.com/sticky/docs/en/device-guide/esp-basics/) for connection guidance.

## Updates and recovery

For normal SD updates, place the app-only image at `/firmware.bin` and use Settings > Firmware Update.
Web upload stores that same file.
The browser does not flash or validate it.
The emergency `/force_update.bin` path uses the same on-device validator.
Validation checks MCU, structure, checksum/hash, partition size, and exactly one matching board tag before erase.

Compatibility classes are `xteink_c3`, `x4pro`, `x4c`, and `sticky`.
The Sticky tag is `PAPYRIX-BOARD-V1:sticky;`.
A matching MCU is not enough for another board's image.
Missing, malformed, duplicated, and incompatible tags are rejected.
This prevents accidental wrong-board updates.
It does not authenticate firmware.
Older untagged images require unlocked serial flashing for downgrade.
Old installed updaters and direct serial flashing do not enforce this check.
A first installation still requires the correct board image and confirmed hardware.
Stock restoration remains unverified.

## Verification

Host tests cover panel selection, controller commands, gray transitions, and allocation failures.
They also cover shared SPI pins, touch/buttons, I2C bus separation, RTC, and power policy.
Other tests cover full-battery USB detection, memory pressure, update rejection, and install packaging.
Recovery coverage includes SD teardown, rail restoration, SD remount, and a display refresh on both controllers.
The actual desktop reader tool passes HTML/EPUB block boundaries, FB2 batched resume, and CSS emphasis scenarios.
All four development and all four release environments build.
The real package command passes all four target-isolation checks.
The generated Sticky parts, hashes, board tag, chip ID, partition table, and archive contents match their build inputs.
`make test` passes all 251 host suites.

`make test-tools` has two unrelated baseline fixture failures:

- `test_clock_app.py` lacks the current Wi-Fi priority method and Pomodoro app identifier.
- `test_clock_display.py` lacks Pomodoro app stubs.

The affected simulator fixtures and production inputs are unchanged from the branch base.
These two simulations do not provide evidence for this port.
The remaining host-tool checks run separately.
The shared Clock application remains in the compiled registry.

Physical checks remain unavailable for both panel variants and touch calibration.
Radio transfers and SD-card compatibility require a device.
Charging current, sleep current, rail timing, and the USB threshold require measurements.
RTC retention, initial flashing, and stock recovery also remain unverified.
The port does not advertise a front light, native USB storage, BLE, or swipe navigation.

## Source revisions

| Reference | Revision |
|---|---|
| FreeInk hardware and display code | [`683fe03009fdd03c725015d167da5362b01911c1`](https://github.com/Free-Ink/freeink-sdk/tree/683fe03009fdd03c725015d167da5362b01911c1) |
| CrossPoint integration reference | [`5d18bee7f3b8212e5d602adbad7b2e3462d8abc8`](https://github.com/crosspoint-reader/crosspoint-reader/tree/5d18bee7f3b8212e5d602adbad7b2e3462d8abc8) |
| CrossMux power and board-tag reference | [`eaf5c73a71aaa03e372b6bc274923c2cc621a033`](https://github.com/0x1abin/crossmux/tree/eaf5c73a71aaa03e372b6bc274923c2cc621a033) |
| Playground registry | [`ee3d931e46bbb0c94d8a39e9937edc685a39391b`](https://github.com/Seeed-Projects/reterminal-sticky-playground-registry/tree/ee3d931e46bbb0c94d8a39e9937edc685a39391b) |

The supplied checkouts use different SDK revisions.
The CrossMux SDK gitlink is `9aa7e95e02c6374a23a562c883eb302552ff24f1`; its local submodule is empty.
It is not inspected driver evidence.
Applicable FreeInk and CrossMux MIT notices remain with the adapted code.

## References

- [Seeed hardware overview](https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/)
- [Seeed schematic, revision 01](https://files.seeedstudio.com/wiki/reterminal_sticky/res/reTerminal_Sticky_Schematic_diagram_260609.pdf)
- [Stock Quick Start](https://www.seeedstudio.com/sticky/docs/en/quick-start/)
- [Stock release notes](https://www.seeedstudio.com/sticky/docs/en/quick-start/release-notes/)
