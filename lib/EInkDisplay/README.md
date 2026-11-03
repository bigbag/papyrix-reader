# EInkDisplay

EInkDisplay provides controller access for supported PapyriX hardware profiles. See the [device support matrix](../../docs/device-support-matrix.md). `papyrix::hal::Display` selects the driver from the active hardware profile. `GfxRenderer` provides text, image, shape, and orientation handling.

## Frame Buffers

`getFrameBuffer()` returns the active frame buffer. A zero bit represents black. A one bit represents white. Use the active profile dimensions for all buffer access.

`displayBuffer()` sends a black-and-white frame. The controller driver implements full, half, and fast refresh requests. The grayscale methods send the two bit planes and apply the controller waveform. See the [rendering pipeline](../../docs/rendering-pipeline.md).

## Power

Call `deepSleep()` before device sleep. The driver completes the controller power-off sequence before deep sleep. Do not remove panel power during a refresh.

## Controller References

- [SSD1677](../../docs/ssd1677-driver.md)
- [X3 UC8253 waveforms](../../docs/x3-lut-waveforms.md)
- [X3 UC8279d](../../docs/x3-uc8279-driver-reference.md)
- [X4 Pro](../../docs/x4pro-specifications.md)

## Sources

- [open-x4-epaper/community-sdk](https://github.com/open-x4-epaper/community-sdk)
- FreeInk controller code: see `FREEINK_LICENSE`.
