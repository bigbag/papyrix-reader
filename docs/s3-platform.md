# ESP32-S3 Reader Platform

See [architecture](architecture.md) for the shared system design.
See the [device support matrix](device-support-matrix.md) for hardware capabilities.

## Targets

Each build selects exactly one `PAPYRIX_TARGET_*` macro.
`lib/BoardSupport/include/TargetConfig.h` defines the target capabilities and memory limits.

- `PAPYRIX_TARGET_XTEINK_C3`: X3 and original X4, with ESP32-C3 and no PSRAM.
- `PAPYRIX_TARGET_X4PRO`: X4 Pro, with ESP32-S3 and 8 MB PSRAM.
- `PAPYRIX_TARGET_X4CLASSIC`: X4 v2 Classic, with ESP32-S3 and 8 MB PSRAM.

The two S3 targets use separate firmware images and board profiles.
Do not interchange their images or pins.
C3 uses a separate bitmap object for each render pass.
Its glyph cache holds up to 192 entries.
Its parsers use the framebuffer for scratch storage.
Its optional PSRAM limits are zero.

All targets use one framebuffer.
`kTargetFrameBufferBytes` sets its size to 48,000 bytes on S3.

## Memory allocation

Ordinary allocations use `malloc` or `new`.
The S3 allocator prefers internal RAM for small requests.
It prefers PSRAM for larger requests.
It can use either memory pool if the first allocation fails.
Reader cache work, library indexing, EPUB metadata, EPUB splitting, and BMP row conversion use ordinary allocations.
Their memory guards use `MALLOC_CAP_8BIT`.
This capability includes both memory pools on S3.

The optional reader buffers use `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`.
Each implementation checks the allocation result.
Each uses its specified fallback if the allocation fails.
Glyph buffers and parser scratch require the request to fit within 80 percent of the largest free PSRAM block.
Bitmap row preloads use this guard when the request exceeds 1 KiB.

## Bitmap turn cache

On S3, `ReaderState` creates a `BitmapTurnCache` for one page render.
`ImageBlock::render()` asks this cache for each BMP.
The cache finds entries by their BMP path.
Each entry keeps its `FsFile` and `Bitmap` until the render turn ends.
`Bitmap::parseHeaders()` reads the headers once per entry.
`Bitmap::preloadRowsInPsram()` loads the rows into PSRAM.
`Bitmap::rewindToData()` resets the row position and dither state before each pass.
Each entry destroys its bitmap before it closes the file.

The cache has these limits:

- At most 4 retained BMP files.
- At most `kTargetBitmapTurnCacheBytes` (256 KiB) of retained rows.
- At most 256 KiB of rows in one bitmap.

If an image exceeds a limit, the cache returns no entry.
It also returns no entry if the PSRAM allocation fails.
`ImageBlock` then opens, parses, draws, and closes the BMP for that pass.
C3 uses this path for every image.
`Bitmap` can preload rows for one pass without keeping them across passes.

## External font glyph buffer

`StreamingEpdFont` reads glyphs from user-installed `.epdfont` files on SD.
It keeps bitmap data in a least-recently-used cache.
On S3, it requests a PSRAM buffer of `kTargetGlyphBitmapSlabBytes` (96 KiB).
With this buffer, the cache holds up to 256 glyph bitmaps.
If the buffer allocation fails, S3 uses the 192-entry cache.
C3 always uses the 192-entry cache.
This smaller cache uses ordinary heap allocations for each glyph bitmap.
Built-in flash glyphs do not use the PSRAM buffer.
The external `.bin` font cache does not use this buffer.

## Parser scratch

`EpubChapterParser` and `Fb2Parser` use `ParserScratch` for temporary parsing storage.
They supply the framebuffer as the borrowed buffer.
On S3, `ParserScratch` requests at least `kTargetParserScratchBytes` (64 KiB) of PSRAM.
With this allocation, parsing does not use the framebuffer.
If the allocation fails, parsing uses the borrowed buffer.
C3 always uses the borrowed buffer.
The `ParserScratch` destructor releases the buffer that it owns.
Parser API signatures do not depend on the target.

## USB behavior

The firmware does not support USB mass storage.
The S3 builds set `ARDUINO_USB_MODE=1`.
Normal firmware uses USB-Serial/JTAG for the serial console.
The firmware does not expose the SD card over USB.
The [web server](webserver.md), [LocalSend](localsend.md), and [Calibre](calibre.md) use Wi-Fi for file transfer.
