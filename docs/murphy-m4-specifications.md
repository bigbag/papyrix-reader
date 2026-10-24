# Murphy M4 specifications

## Support status

**Implemented, not device-tested.**
No M4 device is available for this port.
Host tests, firmware builds, and package checks do not prove physical operation.
Panel behavior, touch IC identity, rail timing, battery accuracy, PSRAM stability, sleep current, and stock restoration remain unverified.

The target retains all existing PapyriX functions that apply to this hardware.
It uses the shared reader, views, applications, and UI/Reader reboot model.
It does not add MurphyOS, Panda AI OS, or CrossMux applications and cloud services.

## Build target and artifacts

| Item | Value |
|---|---|
| Development environment | `murphy_m4` |
| Release environment | `release_murphy_m4` |
| Target macro | `PAPYRIX_TARGET_MURPHY_M4=1` |
| Feature-check target | `murphy_m4` |
| MCU and CPU | ESP32-S3; 240 MHz boot clock; no runtime frequency changes |
| Flash | 16 MiB; DIO |
| PSRAM | 8 MiB OPI; `dio_opi` memory configuration |
| App update | `papyrix-murphy-m4.bin` |
| First-install bundle | `papyrix-murphy-m4-install.zip` |
| Board tag | `PAPYRIX-BOARD-V1:murphy_m4;` |
| Page-cache root | `/.papyrix/cache/murphy_m4/` |
| Render key | `murphy_m4` |

```sh
make test
pio run -e murphy_m4
pio run -e release_murphy_m4
make package
```

These commands build files. They do not flash a device.
Do not use another board's image or GPIO map.

## Hardware connections

These values are source contracts from the pinned references below.
They are not measurements from this project.

| Function | GPIO and configuration |
|---|---|
| SSD1677 display | 800 by 480 pixels; 48,000 bytes per monochrome plane |
| Display SPI | SCK4, MOSI3, CS5, DC6; 20 MHz; no display MISO |
| Display reset and BUSY | Shared reset7; BUSY8 active-high |
| Touch controller | FT6336U-compatible; seven-bit address `0x2E` |
| Shared I2C bus | Native ESP-IDF I2C1; SDA13, SCL12; persistent bus and device handles |
| Touch I2C rate and power | 100 kHz; GPIO45 active-low; no IRQ dependency |
| RTC | RX8010; address `0x32`; 400 kHz on the same I2C1 bus |
| Buttons | Up1, Down2, Power0; active-low; no physical Confirm or Home key |
| Reset button | CHIP_PU hardware reset; not an application input |
| SD transport | Four-bit SDMMC; CLK16, CMD15, D0=17, D1=18, D2=11, D3=14 |
| SD power | GPIO10 active-low |
| Battery | ADC9; divider multiplier 2.0; capacity remains unresolved |
| Charging signal | GPIO43 active-low; independent of USB host connection |
| Front light | Cool47, warm48; active-high; 25 kHz, 10-bit PWM |
| USB | Native USB-Serial/JTAG on GPIO19/20; no USB mass storage |
| Power latch | None assigned |

Power0 is also the ROM boot strap.
Do not infer a safe flashing button sequence from its application role.
The USB status reports native host connection.
It does not infer a wall adapter from battery voltage or charging state.
The battery status reports the separate GPIO43 charging signal.

## Hardware batches

Settings > Device > Hardware batch selects one configuration for both touch and display.
A missing or invalid selection uses Batch 2.
The setting uses `papyrix_hw/m4_batch` in NVS.
It does not change the serialized PapyriX settings format.

| Batch | HALF temperature byte | Touch short-axis calibration |
|---|---|---|
| Batch 1 | `0x3C` | `[-52,553]` |
| Batch 2 | `0x50` | `[-47,514]` |

A change opens a confirmation dialog.
Cancel keeps the current batch and other pending settings.
Confirmation saves pending PapyriX settings before it saves the batch.
A successful save restarts the device before either subsystem uses the new value.
If either save fails, an error appears and the device does not restart.
Both live batch consumers keep their current configuration until restart.
Do not select a batch to compensate for an unrelated display or touch fault.
Confirm the device batch before a future installation.

## Display and input behavior

The target selects SSD1677 directly. It does not use the Sticky panel probe.
Full refresh uses `0xF7`, Fast and window refresh use `0xFC`, and HALF uses `0xD4`.
HALF and window operations use the selected batch temperature byte.
The driver uses the M4 booster, border, grayscale LUT, and power sequence.
It does not use the original X4 gray-return waveform.

The driver retains grayscale, image/text antialiasing, and all four orientations.
Invalid windows send no controller commands.
A failed refresh invalidates the monochrome baseline.
Grayscale staging requires a successful monochrome image.
After a failure, grayscale requests do not reset the controller or send masks.
The next monochrome operation resets the controller and sends the full requested frame.
Optical shades, ghosting, temperature response, and long-term panel behavior need device tests.

The touch task reads eleven-byte frames every 10 ms.
It retains a short completed contact across monochrome and grayscale passes while the main loop performs a refresh.
If a refresh interrupts a later held contact, input ignores that contact until release.
Movement, multiple contacts, malformed frames, overflow, and stale reads do not become taps.
The decoder checks frame length, contact count, event type, and coordinate bounds.
It does not treat coordinate byte patterns as error codes.
A held contact expires after 100 ms without a valid read.
Input checks this limit before it processes a recovered contact or release.
Replay includes the last valid held sample, even when the contact does not move.
Touch uses the existing menu, keyboard, overlay, and reader-zone actions.
A reset, orientation change, or input resynchronization discards contacts from the previous layout.
Swipes and touch firmware updates are not included.

The display owns GPIO7.
It stops touch acquisition before it changes that pin.
Initial reset drives LOW for 50 ms, then HIGH for 100 ms.
After a reset, the touch backend checks its volatile configuration before acquisition restarts.
A failed cooperative stop leaves the shared reset and rails unchanged.

The RX8010 stores UTC time.
After voltage loss, initialization disables alarms and timers and restores the required reserved values.
If the oscillator rejects a VLF clear, initialization waits 3 s and tries once more.
A transfer error stops initialization immediately.
If the second clear fails, the RTC remains unavailable.
The clock stays stopped until a valid time write completes.
Reads reject the voltage-loss flag and STOP, including after an interrupted time write.
The [RX8010SJ manual](https://download.epsondevice.com/td/pdf/app/RX8010SJ_en.pdf), sections 7, 9, 12, and 14, defines the startup and initialization sequence.

The front light uses the existing Screen settings and separate `frontlight/m4_b` and `frontlight/m4_w` keys.
A failed save reports an error but keeps the current live light adjustment.
Wake restores the last successfully saved values.
Sleep stops background input, turns off the front light, removes touch/SD power, and releases all six SD signal pins.
Only Power0 enables wake.
Physical wake and current consumption remain unverified.

## Shared-function coverage

The table identifies software evidence. It does not claim on-device acceptance.

| Function group | Shared consumer and M4 integration | Exercised evidence |
|---|---|---|
| EPUB, FB2, HTML, Markdown, TXT, XTC, XTCH; layout and languages | Content providers, parsers, text layout, shaping; M4 enables existing S3 scratch/font buffers | Desktop batch/full/cold-resume checks; XTC, text, UTF-8, Thai, Arabic, and encoding host suites |
| Resume, bookmarks, statistics, caches | Reader state, progress, bookmark and statistics stores; separate M4 cache identity | Reader-resume tool; bookmark, progress, statistics, and page-cache suites |
| Images, grayscale, AA, rotation | Image converters and GfxRenderer; M4 SSD1677 policy and S3 bitmap cache | Real renderer/controller traces for both batches; image, progressive JPEG, and pixel suites |
| Buttons, touch, keyboard, overlays | InputManager, input HAL, touch layouts; FT acquisition and shared-reset ownership | M4 button/touch/transform tests; common navigation, keyboard/layout, and overlay tests |
| Front light and battery | Existing HAL and settings; M4 pins, ADC divider, charging signal, and separate persistence keys | Real M4 peripheral HAL at hardware-boundary mocks; duty, persistence failure, reload, and shutdown cases |
| RTC, NTP, Clock | Clock HAL, RX8010 backend, existing synchronization and Clock app | RX8010 time/VLF/failure cases; Clock HAL and application/display tools |
| Five applications | Existing Image Viewer, Printer, LocalSend, Pomodoro, and Clock registry | All five compile in M4 firmware; existing app, timer, image, IPP, LocalSend, and Clock suites |
| Wi-Fi, web, Calibre, LocalSend, printing | Existing radio sessions, transfer services, and applications; no separate M4 clients | Firmware builds; web/upload, credential, LocalSend, IPP, and host-tool checks. Radio transfers need a device |
| Updates, cleanup, trash | Existing updater, file actions, and maintenance states; strict M4 board tag and install package | Updater pre-erase rejection, package corruption/bounds cases, filesystem and trash suites; maintenance states compile |
| FAT32/exFAT and PSRAM | Four-bit block device and existing SdFat; existing S3 allocation limits and fallbacks | Actual block-device lifecycle; FAT16/FAT32/exFAT file round trips; M4 parser/font/bitmap/framebuffer allocation failures |
| Themes, fonts, sleep choices | Existing theme/font managers, settings, and sleep states | Theme, font, settings, boot/sleep-view suites; M4 settings layout exports and shutdown smoke |

No USB SD-drive mode, runtime TTF engine, KOReader sync, OPDS browser, Panda cloud client, audio, or sensor application is added.

## Installation

**No physical installation or stock-restoration test is available.**
The full bundle contains the generated bootloader, partition table, OTA data, app, manifest, and license notices.
It is not a verified stock-recovery method.

Before any future write or erase:

1. Confirm the model, hardware batch, flash capacity, and installed partition layout.
2. Check secure-boot, flash-encryption, and download-mode restrictions. Do not change eFuses for this port.
3. Save a complete 16 MiB flash backup and a separate SD backup. Read the flash again and compare both copies.
4. Keep backups private. They can contain credentials and device-specific data.
5. Inspect the generated manifest and verify its part hashes. Confirm a same-device recovery procedure before risking the stock installation.

The bundle requests an erase for a new installation.
An erase can destroy stock firmware, settings, and device data.
A saved backup does not prove that this project can restore it.

The PapyriX partition contract is:

| Partition | Start | Size |
|---|---|---|
| NVS | `0x9000` | `0x5000` |
| OTA data | `0xE000` | `0x2000` |
| App 0 | `0x10000` | `0x640000` |
| App 1 | `0x650000` | `0x640000` |
| Filesystem | `0xC90000` | `0x360000` |
| Core dump | `0xFF0000` | `0x10000` |

The generated install manifest places the bootloader at `0x0` and the partition table at `0x8000`.
These offsets describe this PapyriX bundle, not a stock layout.
A vendor `app_offset:131072` is `0x20000`, not `0x10000`.
Do not reuse a vendor app offset for this bundle or write the app-only file at offset zero.
This guide supplies no universal stock-layout write command.

## Updates and recovery

App updates require an existing compatible PapyriX M4 partition layout.
The app image cannot detect or migrate a stock layout.
For a normal update, copy `papyrix-murphy-m4.bin` to `/firmware.bin` and use Settings > Firmware Update.
Web upload stores the same app image for the on-device validator.

Emergency SD recovery uses `/force_update.bin` and the same validator.
It requires bootable PapyriX and working SD access.
It does not replace an erased bootloader or damaged partition table.
Validation checks MCU, image structure, checksum/hash, slot size, and exactly one M4 board tag before erase or write.
Board tags prevent accidental mismatches. They do not authenticate firmware.

## Software verification

The port passes the host suite, host-tool suite, formatter, static analysis, all five development and release targets, and real package checks.
The M4 artifact excludes foreign profiles, panel drivers, and fuel-gauge code.
Other targets still reject FT6336U code.
Both install ZIPs retain deterministic metadata and their target-specific flash bounds.
Independent M4 ZIP inspection checks every part hash, app segments/checksum/digest/tag, partition MD5/layout, notices, and 16 MiB extents.

The desktop reader compares complete batched and uninterrupted text for all five text formats.
EPUB also passes cold-parser extension.
The low-heap EPUB case leaves a partial cache and completes after the budget is restored.
Hardware-boundary tests and captured renderer planes are not optical or electrical proof.

Before device acceptance, test both batches, all refresh modes, touch corners, rapid taps during refresh, all orientations, RTC retention, radio transfers, repeated SD writes, low-brightness PWM, PSRAM, and sleep/wake.
Measure battery voltage, rail timing, and standby current.
Verify same-device stock restoration before making any recovery guarantee.

## Source revisions

| Reference | Pinned source |
|---|---|
| Later FreeInk SDK profile, native bus, touch, RTC, batch, and display | [CrossMux SDK `9aa7e95e02c6374a23a562c883eb302552ff24f1`](https://github.com/0x1abin/freeink-sdk/tree/9aa7e95e02c6374a23a562c883eb302552ff24f1) |
| Lifecycle and upstream hardware notes | [CrossMux `eaf5c73a71aaa03e372b6bc274923c2cc621a033`](https://github.com/0x1abin/crossmux/blob/eaf5c73a71aaa03e372b6bc274923c2cc621a033/docs/engineering/murphy-m4.md) |

Upstream device observations remain upstream evidence.
Applicable MIT and community-driver notices remain beside the adapted code and in the install bundles.
See the [device support matrix](device-support-matrix.md), [S3 platform](s3-platform.md), and [user guide](user_guide.md) for shared behavior.
