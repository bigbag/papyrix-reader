# Architecture

## Layers

PapyriX supports ESP32-C3 and ESP32-S3 targets. The reader and UI share content providers and rendering code. Board profiles select hardware backends and display controllers.

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

`src/core/StateMachine` manages screen transitions. Each state implements `enter()`, `update()`, `exit()`, and `render()`. States own their data. View functions render that data without owning screen state. `StateTransition::to(StateId)` requests a transition.

UI mode provides library management, settings, and network functions. Reader mode provides book rendering with lower memory use. The device restarts when it changes boot modes. Persistent state records the book and reading position.

The reader records the resume target after a successful page render. If the settings save fails, it retries after the next successful render. It adds the book to recent books after the save succeeds. A pending open uses a separate path. If the reader restarts before it saves the resume target, the next boot returns to the UI. The previous resume target remains available.

Cancelling full-book indexing restarts the device in UI mode. It keeps the return destination and does not replace saved reading progress before the first page renders.

## Hardware

`lib/BoardSupport` owns board profiles and hardware selection. The C3 image selects X3 or X4. Each S3 image uses one fixed profile: X4 Pro, X4 Classic, Sticky, or Murphy M4. Display probing selects the controller independently of board selection. Classic first reads the factory panel identity, then probes when needed. An unresolved Classic identity blocks display initialization but permits headless SD recovery.

Rendering checks panel grayscale support before it writes grayscale planes. Classic preserves the factory battery calibration. Sticky supports SSD1677 and SSD2677 panels. Its probe uses FreeInk identity reads and BUSY polarity. It rejects unstable observations. An unresolved Sticky panel does not receive display commands.

Sticky uses SPI SD. Touch uses one I2C controller. The gauge and RTC share the other I2C controller. Its gauge preserves factory configuration. Sticky support is implemented, not device-tested.

M4 selects SSD1677 without a display probe. Its four-bit SDMMC transport uses all six data/clock/command pins. FT6336U-compatible touch and RX8010 share one persistent native I2C1 bus. Each device uses its own clock rate. The display stops touch acquisition before it changes shared reset GPIO7. Both subsystems load one hardware-batch selection at boot.

M4 support is implemented, not device-tested. See the [M4 specifications](murphy-m4-specifications.md).

Hardware backends provide battery, RTC, power, front-light, and touch services. Unsupported services remain unavailable to the UI. The build excludes hardware code for other targets. See the [device support matrix](device-support-matrix.md).

### Button Input

`InputManager` samples physical buttons in a `BackgroundTask` above the main-loop priority. The task uses a 10 ms idle interval and a 5 ms interval while changes are pending. The debounce interval remains 20 ms. The background task continues to capture input during rendering. If the task cannot start, the main loop samples buttons and limits its sleep to 10 ms.

A fixed queue stores up to 32 state changes with their timestamps. On overflow, the queue discards the oldest change and retains the newest state. The main loop consumes one change per update and remains the sole producer of button events. Pending changes suppress hold events until input processing reaches the current state. 

Input suppression keeps completed taps that precede the wait and cancels incomplete page gestures. A page button held when suppression ends needs a release and a new press before it can navigate again. Suppression does not remove other button events.

ADC button, battery, and USB-supply reads share one mutex. On X4, a possible ADC button press requires two more samples. The median of the three samples rejects one isolated outlier. Idle reads do not require these extra samples. Zero remains a valid Right or Down reading.

Startup checks sample directly before the task starts. Shutdown stops the task before GPIO configuration changes.

GT911 touch input remains on the main loop. M4 touch acquisition uses a separate `BackgroundTask`. The main loop classifies its bounded, timestamped samples and remains the event producer.

### Clock Power Modes

Normal mode keeps normal input sampling and uses the existing CPU idle policy. Power saving mode enters deep sleep after 15 seconds without interaction. The timeout also applies to Clock settings. Held input, queued input, and active operations defer sleep.

`ClockStandby` owns one checked RTC record. The record contains settings, appearance, the last frame, the next UTC sync deadline, and board, panel, and firmware identity. A valid timer wake runs before normal UI startup. It does not mount SD or LittleFS, initialize input tasks, or write settings on an ordinary minute update. A due synchronization can mount SD for saved credentials. Automatic connection attempts share a 15-second budget; NTP has a 10-second budget.

The timer path reconstructs the previous frame in the existing framebuffer. SSD1677, SSD2677, and UC8279 Pro/Classic restore their reference image. SSD1677 loads temperature and the waveform before its first differential update after reset. This preparation does not drive the panel. See commands `0x1A` and `0x22` in the [SSD1677 specification](https://cursedhardware.github.io/epd-driver-ic/SSD1677.pdf).

UC8279 Pro/Classic restores the old image in DTM1 with the 120-gate offset before differential refresh. This RAM write does not activate the panel. Normal startup and recovery after an error keep the full-refresh guard.

UC8253 restores both reference planes from the previous Clock frame before differential refresh. This RAM write keeps the X3 row order and polarity and does not activate the panel. Normal startup and recovery after an error keep the full-refresh guard. UC8279 X3 and UC8179 Pro/Classic keep their full-refresh startup guards. No extra C3 framebuffer is required.

The timer wakes at the next minute boundary from current RTC-backed UTC time. Clock checks the displayed minute again after it stops services. If the minute changes during shutdown, Clock cancels standby and returns to its controls. It draws the current time and uses the normal 15-second timeout before it tries standby again. On X3, Clock keeps GPIO13 HIGH through sleep and timer startup. It does not switch off this shared power control as normal shutdown does.

A short Power press unlocks Clock controls without Booting or Home. Ordinary sleep and Clock standby preserve the pull configuration of the profile's Power pin. Touch and the front light stay off during standby. A normal reset or invalid record uses normal startup. A fatal wake error clears automatic resume and opens time setup or Error. Display initialization allows one reset and retry; a failed panel requires explicit user recovery.

S3 sleep configures the Power pin's RTC pull before it enables EXT1 wake. Digital GPIO pulls do not control the S3 RTC input. The SDK holds the RTC pull through deep sleep without keeping the RTC peripheral domain on. A wake-setup error prevents sleep. Ordinary sleep restarts after teardown; Clock standby returns the error.

## Content

`src/content/` provides format-specific content handles. The handles expose metadata, navigation, and reading progress. EPUB, FB2, HTML, Markdown, and TXT require text layout. XTC and XTCH contain pre-rendered pages.

`lib/PageCache` stores rendered page records on the SD card. The reader loads the requested page instead of repeating its layout. Cache extension runs in bounded chunks. Device-specific cache roots separate incompatible page geometry. See [file formats](file-formats.md).

## Rendering

`GfxRenderer` renders text, bitmaps, and UI elements into the active framebuffer. The `EInkDisplay` library sends that data to the selected controller. The controller driver owns waveform selection, RAM synchronization, and sleep.

The reader reuses the framebuffer for grayscale masks. It does not require a full-page backup for antialiasing. See the [rendering pipeline](rendering-pipeline.md) for memory and cache ownership. See the [S3 reader platform](s3-platform.md) for optional PSRAM buffers and fallback paths.

## Fonts and Text

`FontManager` selects built-in or external fonts. Built-in bitmaps remain in Flash. Streaming fonts load metadata and cache glyph bitmaps from the SD card. A missing external font uses a built-in fallback.

The text layout code applies font metrics, line spacing, alignment, and hyphenation. CSS parsing supplies supported EPUB style properties. `ScriptDetector` identifies script-specific layout requirements. `ThaiShaper` groups Thai marks. `ArabicShaper` selects contextual forms and Lam-Alef ligatures. External CJK fonts provide one-bit glyph data.

See [fonts](fonts.md), [customization](customization.md), and [localization](localization.md) for configuration and format limits.

## Images

Image converters decode supported images and scale them to the viewport. The cache stores converted images on SD. Size and allocation limits bound parser memory use. The HTML preprocessing path removes embedded image data URIs before XML parsing. See [image rendering](images.md).

## UI

`src/ui/Elements.h` provides buttons, menus, keyboards, and shared geometry. `src/ui/views/` contains state-specific view functions. States dispatch physical-button and touch actions. Touch hit testing uses the same logical geometry as rendering.

### Mini-apps

`lib/MiniApps` provides the shared app host. It has no firmware dependencies and does not allocate memory. `src/apps/MiniApp.h` binds its typed callbacks to `Core` and `Event`.

The host manages app and menu state, redraw requests, and checked exits. All registered apps use this interface. The app descriptor remains valid while the app runs. Add an app through `src/apps/AppRegistry.cpp`.

App IDs follow the registry order. `appIdForMenuItem()` in `src/ui/views/AppLauncherViews.h` sets the menu order separately. Update this mapping when you add an app. `AppMenuView::rowPitch()` limits rows to the space above the footer. Rendering and touch input use this pitch with `rowY()`. Separator gaps do not accept input.

Apps return a `Frame` with the refresh mode and panel power-off policy. Return `Refresh::None` when the app already refreshes the panel. The launcher uses the existing renderer and a CPU performance lock.

The launcher restores active CPU speed before app event handling. An exit action can write files before the host returns.

`MiniAppInput` maps physical button presses and touch footer actions. Apps keep their special input rules. Touch is optional. The 2048 app also accepts swipes inside its game board.

`prepareExit` checks an exit before cleanup. `Blocked` retains the exit destination for retry or discard. `OpenMenu` opens editable settings and cancels that destination. A pending exit prevents idle sleep.

`CheckedFile.h` checks temporary writes, sync, and close before publication. It retains the previous file through a backup rename. Clock, 2048, and Go keep their own file formats, validation, and load rules.

Clock keeps board-specific standby and recovery in the firmware. After hardware recovery invalidates an app context, `Host::abandon` clears host state without calling the app exit callback.

`lib/GoGame` provides the 9 by 9 Tromp–Taylor rules and a bounded Monte Carlo opponent. `GoStore` keeps the full position history on the SD card. `GoApp` runs the opponent through `lib/AsyncTask`. The task reads an immutable position. Only the main thread commits a move.

## Background Work

`lib/AsyncTask` provides cooperative cancellation and scoped mutex handling. Background tasks stop themselves. Do not force-delete a task. Stop the cache task before the main thread accesses its parser or page cache. Do not hold a mutex during file I/O or other blocking work.

`BackgroundTask::getState()` reports `COMPLETE` after the worker publishes its exit generation, including after a stop timeout. Call `stop()` before reuse to finish the lifecycle transition.

## Network

Wi-Fi starts on demand. The web server provides file management and upload functions. Calibre provides wireless book transfer. RTC and NTP services maintain system time where available.

Firmware updates validate a PapyriX board tag before flash erase when one is present. The combined X3/X4 image is one compatibility class. Pro, Classic, Sticky, and M4 use separate tags. Images without a PapyriX tag can pass the MCU, integrity, and slot-size checks. The tag check helps prevent accidental wrong-board updates. It does not authenticate firmware.

`lib/Ipp` is the IPP print server core. It parses the IPP wire protocol (RFC 8010/8011). It decodes Apple raster and PWG raster streams row by row. It scales each page onto the 1-bit panel framebuffer with box downsample and Floyd-Steinberg dither. The core has no Arduino types.

`IppTransport` is the port point between the firmware WiFi client and the host test harness. `src/apps/PrinterApp.cpp` owns the WiFi session, the mDNS advertisement, and the printout queue on the SD card. `lib/FsHelpers` keeps the bounded newest-64 printout queue sorted in natural order.

`lib/Localsend` is the LocalSend receive core. The app saves received files to `/received`.

See the [web server guide](webserver.md), the [Calibre guide](calibre.md), the [printer guide](printer.md), and the [LocalSend guide](localsend.md).

## Desktop Parser Tool

`tools/reader-test` runs the content parser with built-in font metrics. Its default viewport matches the X4 portrait reader. Use batched parsing to exercise cache suspend and resume:

```bash
make reader-test
tools/reader-test/build/reader-test --dump --batch 5 book.epub /tmp/cache
tools/reader-test/build/reader-test --dump --no-statusbar book.epub /tmp/cache-no-statusbar
tools/reader-test/build/reader-test --cache-dump /path/to/device-cache/
```

Use a separate output directory for each layout configuration. Compare the text output with a device cache dump to locate page differences.
