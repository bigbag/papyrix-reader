# Troubleshooting

Use the firmware file for the device:

- X3 or original X4: `papyrix-xteink-c3.bin`
- X4 Pro: `papyrix-x4pro.bin`
- X4 v2 Classic: `papyrix-x4c.bin`
- Sticky: `papyrix-sticky.bin` for app updates
- Murphy M4: `papyrix-murphy-m4.bin` for app updates

Pro, Classic, Sticky, and M4 images are not interchangeable.
A wrong-target image can drive incorrect GPIOs and damage hardware.
Use the [firmware recovery procedure](#firmware-recovery) to install the correct image.
Do not erase the chip or factory NVS.
Classic uses factory NVS data for panel selection.
For Sticky first installation, use the [complete install bundle](sticky-specifications.md#installation).
Sticky support is implemented, not device-tested.
For M4 first installation, use the [M4 guide](murphy-m4-specifications.md#installation).
M4 support is **Implemented, not device-tested**.
Do not erase or write before model/batch, flash/layout, security checks, and private verified flash/SD backups.
No physical operation or stock-restoration procedure is verified for M4.

## Serial Log Capture

Use `scripts/capture_serial.py` to record the device serial log.
The script appends data to the output file across USB disconnects.
The script saves each received data block without waiting for a full serial read.
Use a Python environment that contains `pyserial`.

```bash
python3 scripts/capture_serial.py USB_SERIAL_NUMBER serial.log
```

Replace `USB_SERIAL_NUMBER` with the number that `pio device list` shows for the connected reader.
The script reconnects when USB lists the device again.
It waits while the reader sleeps.
Opening the serial port can reset the reader.
Stop the script with Ctrl+C.

## X3 Display Controller Diagnostics

X3 production units can use a UC8253 or UC8279d display controller. The firmware detects the controller. It stores a conclusive result.

After a live controller probe, the firmware writes the raw probe data to the serial log with the `DEVICE` tag:

```
display probe: VER1=.. .. .. .. .. FLG1=..
display probe: VER2=.. .. .. .. .. FLG2=..
display probe MTP: ..
display probe verdict: uc8279-confirmed | uc8253-stable-default | inconclusive
display controller: UC8279_X3 (probe, cached)
```

The probe runs only when no valid cache or override exists. Clear the `epd_det` key in the `papyrix_hw` NVS namespace to run the probe again.

If an X3 screen stays blank or shows an incorrect image, attach the serial log to the issue report. Report the results for full refresh, fast refresh, grayscale, sleep, and wake.

## X4 v2 Classic Display Selection

Classic first reads the factory panel identity.
If that value is missing or invalid, it probes the display.
An unknown response leaves the display disabled.
Headless SD recovery remains available.
A panel without a supported grayscale waveform uses monochrome rendering.
See the [Classic support matrix](device-support-matrix.md#x4-classic) and
[Classic specifications](x4-classic-specifications.md) for device details.

## Firmware Recovery

Use **Settings → Firmware Update** for a device that can open the UI. Copy the
correct artifact to the SD root as `/firmware.bin`.

Current firmware checks MCU, image integrity, and slot size before flash erase.
It rejects incompatible, malformed, and duplicate PapyriX board tags.
Compatible third-party and older images do not need a PapyriX tag.
Without this tag, the updater cannot verify the board within the same MCU family.
Select an app image for the correct board and partition layout.
See [update compatibility](device-support-matrix.md#firmware-update-compatibility).

If the UI cannot start, copy the artifact as `/force_update.bin`. The boot path
applies it before UI initialization. It shows **Firmware update in progress**
and **Do not power off** when the display is available. A display initialization
failure does not block the update, so the previous e-ink frame can remain
visible during a headless recovery. Wait for the device to restart. The boot
path removes the file after the attempt. Copy it again before a retry.

There is no boot-button recovery mode.
Emergency SD recovery requires bootable PapyriX and working SD access.
It cannot restore an erased bootloader or damaged partition table.

## Display Initialization Recovery

The firmware performs one controller reset and initialization retry. If the
second attempt fails, it closes the SD transport, disables display power, then
disables storage power. The power button remains active. The
firmware waits without rebooting.

Press the Power button to restore storage power and retry display initialization.
SPI targets restore the profile pins before the SD card remounts.

## Murphy M4 faults

Do not treat a firmware build or host test as proof of correct wiring.
Confirm the hardware batch before changing Settings > Device > Hardware batch.
A successful change restarts before touch and display use the new configuration.
The USB status reports native host connection, not wall-adapter presence.
GPIO43 supplies the separate charging signal.
Touch and display share reset GPIO7.
Do not add an independent touch reset or replace the four-bit SD pins with a one-bit example.
Record logs and compare the [M4 specifications](murphy-m4-specifications.md) before another hardware write.

## Repeated Sleep at Startup

Custom firmware can enter sleep immediately after reset.
This can prevent USB flashing.
Remove the SD card and restart the device.
If USB becomes available, install the correct release artifact.
Do not connect wires to SD contacts to force download mode.
This repository does not provide a verified device-specific procedure for that operation.

## Full SD Card

A full card cannot store page caches. The device shows **SD card full. Free
space.** and does not open the book. Free space to continue reading. Delete
books over USB, or use **Clear Book Cache** in settings. Clearing the cache
also deletes reading progress.
