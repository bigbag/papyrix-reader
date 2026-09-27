# Architecture

## Layers

PapyriX supports ESP32-C3 and ESP32-S3 targets.
The reader and UI share content providers and rendering code.
Board profiles select hardware backends and display controllers.

```text
States and views
    |
Content providers and page cache
    |
Text layout, fonts, and image conversion
    |
Display, input, storage, and network interfaces
    |
BoardSupport and controller drivers
```

## State Machine

`src/core/StateMachine` manages screen transitions.
Each state implements `enter()`, `update()`, `exit()`, and `render()`.
States own their data.
View functions render that data without owning screen state.
`StateTransition::to(StateId)` requests a transition.

UI mode provides library management, settings, and network functions.
Reader mode provides book rendering with lower memory use.
The device restarts when it changes boot modes.
Persistent state records the book and reading position.

## Hardware

`lib/BoardSupport` owns board profiles and hardware selection.
The C3 image selects X3 or X4.
Each S3 image uses one fixed profile: X4 Pro or X4 Classic.
Display probing selects the controller independently of board selection.
Classic first reads the factory panel identity, then probes when needed.
An unresolved Classic identity blocks display initialization but permits headless SD recovery.
Rendering checks panel grayscale support before it writes grayscale planes.
Classic preserves the factory battery calibration.

Hardware backends provide battery, RTC, power, front-light, and touch services.
Unsupported services remain unavailable to the UI.
The build excludes hardware code for other targets.
See the [device support matrix](device-support-matrix.md).

### Button Input

`InputManager` samples physical buttons in a `BackgroundTask` above the main-loop priority.
The task uses a 5 ms interval and the existing 20 ms debounce.
If the task cannot start, the main loop samples buttons.
A fixed queue stores up to 32 state changes with their timestamps.
On overflow, the queue discards the oldest change and retains the newest state.
The main loop consumes one change per update and remains the sole producer of button events.
Pending changes suppress hold events until input processing reaches the current state.
ADC button reads share a mutex with battery reads.
Startup checks sample directly before the task starts.
Shutdown stops the task before GPIO configuration changes.
Touch input remains on the main loop.

## Content

`src/content/` provides format-specific content handles.
The handles expose metadata, navigation, and reading progress.
EPUB, FB2, HTML, Markdown, and TXT require text layout.
XTC and XTCH contain pre-rendered pages.

`lib/PageCache` stores rendered page records on the SD card.
The reader loads the requested page instead of repeating its layout.
Cache extension runs in bounded chunks.
Device-specific cache roots separate incompatible page geometry.
See [file formats](file-formats.md).

## Rendering

`GfxRenderer` renders text, bitmaps, and UI elements into the active framebuffer.
The `EInkDisplay` library sends that data to the selected controller.
The controller driver owns waveform selection, RAM synchronization, and sleep.

The reader reuses the framebuffer for grayscale masks.
It does not require a full-page backup for antialiasing.
See the [rendering pipeline](rendering-pipeline.md) for memory and cache ownership.
See the [S3 reader platform](s3-platform.md) for optional PSRAM buffers and fallback paths.

## Fonts and Text

`FontManager` selects built-in or external fonts.
Built-in bitmaps remain in Flash.
Streaming fonts load metadata and cache glyph bitmaps from the SD card.
A missing external font uses a built-in fallback.

The text layout code applies font metrics, line spacing, alignment, and hyphenation.
CSS parsing supplies supported EPUB style properties.
`ScriptDetector` identifies script-specific layout requirements.
`ThaiShaper` groups Thai marks.
`ArabicShaper` selects contextual forms and Lam-Alef ligatures.
External CJK fonts provide one-bit glyph data.

See [fonts](fonts.md), [customization](customization.md), and
[localization](localization.md) for configuration and format limits.

## Images

Image converters decode supported images and scale them to the viewport.
The cache stores converted images on SD.
Size and allocation limits bound parser memory use.
The HTML preprocessing path removes embedded image data URIs before XML parsing.
See [image rendering](images.md).

## UI

`src/ui/Elements.h` provides buttons, menus, keyboards, and shared geometry.
`src/ui/views/` contains state-specific view functions.
States dispatch physical-button and touch actions.
Touch hit testing uses the same logical geometry as rendering.

## Background Work

`lib/AsyncTask` provides cooperative cancellation and scoped mutex handling.
Background tasks stop themselves.
Do not force-delete a task.
Stop the cache task before the main thread accesses its parser or page cache.
Do not hold a mutex during file I/O or other blocking work.

## Network

Wi-Fi starts on demand.
The web server provides file management and upload functions.
Calibre provides wireless book transfer.
KOReaderSync provides reading-progress synchronization.
RTC and NTP services maintain system time where available.

`lib/Ipp` is the IPP print server core.
It parses the IPP wire protocol (RFC 8010/8011).
It decodes Apple raster and PWG raster streams row by row.
It scales each page onto the 1-bit panel framebuffer with box downsample and
Floyd-Steinberg dither.
The core has no Arduino types.
`IppTransport` is the port point between the firmware WiFi client and the
host test harness.
`src/apps/PrinterApp.cpp` owns the WiFi session, the mDNS advertisement, and
the printout queue on the SD card.
`lib/FsHelpers` keeps the bounded newest-64 printout queue sorted in natural
order.
`lib/Localsend` is the LocalSend receive core.
The app saves received files to `/received`.

See the [web server guide](webserver.md), the [Calibre guide](calibre.md),
the [printer guide](printer.md), and the [LocalSend guide](localsend.md).

## Desktop Parser Tool

`tools/reader-test` runs the content parser with built-in font metrics.
Its default viewport matches the X4 portrait reader.
Use batched parsing to exercise cache suspend and resume:

```bash
make reader-test
reader-test --dump --batch 5 book.epub /tmp/cache
reader-test --dump --no-statusbar book.epub /tmp/cache-no-statusbar
reader-test --cache-dump /path/to/device-cache/
```

Use a separate output directory for each layout configuration.
Compare the text output with a device cache dump to locate page differences.
