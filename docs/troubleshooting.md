# Troubleshooting

Use the firmware file for the device:

- X3 or original X4: `papyrix-xteink-c3.bin`
- X4 Pro: `papyrix-x4pro.bin`
- X4 v2 Classic: `papyrix-x4c.bin`
- Sticky: `papyrix-sticky.bin` for app updates
- Murphy M4: `papyrix-murphy-m4.bin` for app updates

Pro, Classic, Sticky, and M4 images are not interchangeable. A wrong-target image can drive incorrect GPIOs and damage hardware. Use the [firmware recovery procedure](#firmware-recovery) to install the correct image. Do not erase the chip or factory NVS. Classic uses factory NVS data for panel selection.

For Sticky first installation, use the [complete install bundle](sticky-specifications.md#installation). Sticky support is implemented, not device-tested.

For M4 first installation, use the [M4 guide](murphy-m4-specifications.md#installation). M4 support is **Implemented, not device-tested**. Do not erase or write before model/batch, flash/layout, security checks, and private verified flash/SD backups. No physical operation or stock-restoration procedure is verified for M4.

## Serial Log Capture

Use `scripts/capture_serial.py` to record the device serial log. The script appends data to the output file across USB disconnects. The script saves each received data block without waiting for a full serial read. Use a Python environment that contains `pyserial`.

```bash
python3 scripts/capture_serial.py USB_SERIAL_NUMBER serial.log
```

Replace `USB_SERIAL_NUMBER` with the number that `pio device list` shows for the connected reader. The script reconnects when USB lists the device again. It waits while the reader sleeps. Opening the serial port can reset the reader. Stop the script with Ctrl+C.

## Go Opponent

If Thinking continues, capture the serial log from app entry through a reply or a failed stop. Include the device model and firmware version.

With debug logging enabled, the `[GO] Opponent:` line reports completed simulations, elapsed opponent time, and minimum free task stack. The elapsed time excludes move publication and the following display refresh.

See [Go controls and recovery](user_guide.md#313-go) for the search budget, Retry, Cancel, and exit actions.

## X4 Input, Display, and Clock Checks

An old Booting image does not prove that the reader restarts. If a button restores the current view, report this separately from a reset. Record whether USB is connected and whether it supplies power from a computer, hub, or charger.

The X4 display uses 10 MHz SPI and restores both display RAM planes after refresh. The ADC filter rejects an isolated outlier but cannot identify sustained electrical interference.

Normal Clock mode keeps the normal 10 ms idle button interval. Power saving mode uses deep sleep with timer wake at each minute boundary.

In Clock settings, check **Mode** before a battery comparison. Power saving enters standby after 15 seconds without input, even with settings open or USB connected. Use a short Power press to return to controls. Other buttons and touch do not wake standby. Check several real minute updates and Clock exit before a battery comparison. A static clock image alone does not prove that timer wake works.

If Clock cannot retain valid time, it opens WiFi setup instead of staying awake on an invalid clock. On X4 and M4, each minute wake updates the retained battery estimate. Awake battery reads use a five-second polling interval.

Record the device, panel, face, mode, NTP interval, USB state, and start/end times. Record the initial and final battery percentage and voltage. Percentage alone does not measure current or battery capacity. Host checks and simulated time do not confirm device sleep or battery savings.

## X3 Display Controller Diagnostics

X3 production units can use a UC8253 or UC8279d display controller. The firmware detects the controller. It stores a conclusive result.

After a live controller probe, the firmware writes the raw probe data to the serial log with the `BOARD` tag:

```
X3 stock probe: VER=.. .. .. busy_timeout=0
display probe verdict: uc8279-confirmed | uc8253-stable-default | inconclusive
panel: UC8279_X3 (probe, cached)
```

The probe runs only when no valid cache or override exists. Clear the `epd_det` key in the `papyrix_hw` NVS namespace to run the probe again. Cache version 2 causes a new probe for results from the previous protocol. A BUSY timeout does not prevent the VER read.

If an X3 screen stays blank or shows an incorrect image, attach the serial log to the issue report. Report the results for full refresh, fast refresh, grayscale, sleep, and wake.

## X4 v2 Classic Display Selection

Classic first reads the factory panel identity. If that value is missing or invalid, it probes the display. An unknown response leaves the display disabled. Headless SD recovery remains available. A panel without a supported grayscale waveform uses monochrome rendering. See the [Classic support matrix](device-support-matrix.md#x4-classic) and [Classic specifications](x4-classic-specifications.md) for device details.

## Firmware Recovery

For a device that can open the UI, use the [normal update procedure](user_guide.md#firmware-update). Do not remove power or the SD card during either update procedure.

Emergency recovery requires bootable PapyriX and working SD card access. It cannot restore an erased bootloader or a damaged partition table. There is no boot-button recovery mode. Select the app image for the correct board and partition layout. The updater validates the image before flash erase. See [update compatibility](device-support-matrix.md#firmware-update-compatibility) for validation limits.

1. Copy the correct app image to the SD card root as `/force_update.bin`.
2. Insert the card.
3. Start the device.
4. Wait for the update attempt to finish.

The boot path applies the image before UI initialization. When the display is available, it shows **Firmware update in progress** and **Do not power off**. If the display is unavailable, the update can continue while the previous e-ink frame remains unchanged. The device restarts after a successful update.

If the update fails, the boot path continues startup. The boot path removes `/force_update.bin` after the attempt. Copy it again before a retry.

## Display Initialization Recovery

The firmware performs one controller reset and initialization retry. If the second attempt fails, it closes the SD transport, disables display power, then disables storage power. The power button remains active. The firmware waits without rebooting.

Press the Power button to restore storage power and retry display initialization. SPI targets restore the profile pins before the SD card remounts.

Clock timer wake uses a separate bounded error path. A second display failure clears automatic Clock resume. It does not start another minute cycle or an automatic reset loop. The Error screen appears only if the panel is initialized. A button requests recovery. Invalid time opens time setup.

## Murphy M4 faults

Do not treat a firmware build or host test as proof of correct wiring. Confirm the hardware batch before changing Settings > Device > Hardware batch. A successful change restarts before touch and display use the new configuration. The USB status reports native host connection, not wall-adapter presence. GPIO43 supplies the separate charging signal.

Touch and display share reset GPIO7. Do not add an independent touch reset or replace the four-bit SD pins with a one-bit example. Record logs and compare the [M4 specifications](murphy-m4-specifications.md) before another hardware write.

## Repeated Sleep at Startup

Custom firmware can enter sleep immediately after reset. This can prevent USB flashing. Remove the SD card and restart the device. If USB becomes available, install the correct release artifact. Do not connect wires to SD contacts to force download mode. This repository does not provide a verified device-specific procedure for that operation.

## Full SD Card

A full card cannot store page caches. The device shows **SD card full. Free space.** and does not open the book.

Free space to continue reading. Delete books over USB, or use **Clear Book Cache** in settings. Clearing the cache also deletes reading progress.
