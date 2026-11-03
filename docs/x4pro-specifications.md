# Xteink X4 Pro Specifications

## Build

- Artifact: `papyrix-x4pro.bin`
- PlatformIO environment: `release_x4pro`
- MCU: ESP32-S3
- PSRAM: 8 MB, octal
- Flash: 16 MB
- Application offset: `0x10000`
- Board selection: fixed
- Display controller selection: UC8279 for LUT versions `0x02`, `0x68`, or `0x69`; UC8179 for `0x01`

Unconfirmed probes and other LUT versions use the SSD1677 fallback. The X3 field-fallback signature does not select UC8179.

See the [device support matrix](device-support-matrix.md) for build targets and hardware services.

## Hardware

- **Display bus:** SCLK GPIO12, MOSI GPIO11, CS GPIO13, DC GPIO18, reset GPIO14, BUSY GPIO6
- **Storage:** 1-bit SDMMC; CLK GPIO41, CMD GPIO42, DAT0 GPIO40; active-low GPIO5 rail
- **Touch:** GT911 at `0x5D`; SDA GPIO39, SCL GPIO38, IRQ GPIO10, reset GPIO4, active-low GPIO2 rail; 100 kHz
- **Battery:** CW2017 on GPIO39/GPIO38; charge status GPIO21
- **RTC:** BM8563 at `0x51`
- **Front light:** GPIO8 cool and GPIO9 warm PWM
- **Buttons:** Up GPIO0, Down GPIO7, Power GPIO3
- **Power latch:** GPIO1, active high

The touch transform swaps X and Y and reverses Y. It maps the portrait sensor to the 800 × 480 panel frame. Settings > Screen contains Brightness and Warmth. Zero brightness turns off both light channels. Sleep turns off the light.

Wake restores the light settings.

## USB Installation

Native USB uses Espressif identifier `303A:1001`. Close the serial monitor and connect the device through unlocked USB. From the repository root, build and flash the release firmware:

```bash
make flash-x4pro
```

To select a port, use `PLATFORMIO_UPLOAD_PORT=/dev/ttyACM0 make flash-x4pro`. Do not use `make flash-release` for X4 Pro. That command selects the X3/X4 release environment.

USB flashing does not require holding Power.

## Read-Only Display Probe

The `x4pro_characterize` environment leaves display pins as inputs at startup. Its `p` command resets the controller and uses SPI mode 0 at approximately 500 kHz. The command reads FLG (`0x71`) and VER (`0x70`). It reads RMTP (`0xA2`) only when the status line is driven. It releases the display pins after the probe.

It does not send power-on, RAM, waveform, activation, or refresh commands.

## Power and Safety

The CPU policy uses 10 MHz at idle and restores 240 MHz for active work. The board enables the SD rail before each mount attempt. Deep sleep disables the touch and storage rails and turns off the front light. The display driver completes its power-off sequence before deep sleep. A second display initialization failure disables display and storage rails.

The device then waits for an explicit retry.

The 2048 app accepts swipes that start on its board. Other screens do not use swipe navigation. The artifact does not include Bluetooth.
