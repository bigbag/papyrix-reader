

## v1.7.0 (2026-02-16)

*  Add UTF-8 NFC normalization and extend builtin font coverage with Vietnamese, Thai and Greek [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Elements: Fix reader status bar vertical alignment [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix FileListState NFC normalization, add EpdFontFamily external style index   tests [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update CHANGELOG.md [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.6.5 (2026-02-16)

*  SlimParser: Fix text loss at batch boundaries; reader-test uses real font metrics  . Issue #48 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ParsedText: Skip paragraph indentation for center-aligned text. Issue #50 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Centralize external font style mapping and lazy-load bold variant on text width/draw. Issue #49 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.6.4 (2026-02-15)

*  Update changelog [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Free cache capacity on clear; fix addrinfo leak on DNS failure [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  SlimParser: Drop unknown entities and non-content XML in default handler [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.6.3 (2026-02-15)

*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add HTML entity resolution for undeclared entities [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Increase idle loop delay for power saving after 3s inactivity [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  WebServer: Gzip-compress HTML assets for smaller flash footprint [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reader: Skip background caching for XTC content. Issue #45 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Sleep: Show immediate Sleeping... feedback before rendering sleep screen. Issue #37 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bumv version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.6.2 (2026-02-14)

*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reader TOC: Add page-up/page-down navigation with Left/Right buttons. Issue #26 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Sleep: Check lastBookPath instead of content.isOpen() for cover sleep screen [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Flush part word buffer before page layout. Issue #48 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add reader-test [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.6.1 (2026-02-14)

*  EPUB: Silently skip unsupported image formats instead of showing placeholder [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  EPUB: Preserve overflow line on page-break abort and check abort during layout. Issue #47 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.6.0 (2026-02-13)

*  EPUB: Add anchor-based TOC navigation for intra-spine positioning. Issue #41 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ReaderState: Simplify thumbnail generation and retry caching if interrupted [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.5.0 (2026-02-13)

*  Add pytrhon reqs for platformio [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Remove cover dithering option, always use 1-bit; skip anti-aliasing for custom fonts [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  CSS: Remove text-indent and margin parsing, reduce max file size to 64KB [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ZipFile: Reduce memory fragmentation and allocation sizes [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fonts: Remove italic support, reduce cache sizes for memory optimization [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ReaderState: Clear screen before rendering cached pages [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeView: Remove card borders and simplify book info layout. Issue #42 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Use external font for TOC only when one is configured [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ReaderState: Force full refresh after cover page to clear residual image [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeView: Use external font for book title and author on home screen [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.4.3 (2026-02-11)

*  Reader: Fix backward navigation by fully caching chapter for last-page lookup. Issue #41 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Raise decorative image skip threshold from 3px to 20px [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.4.2 (2026-02-10)

*  Docs: Add soft-brick recovery troubleshooting guide [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Font: Fix Latin character spacing with CJK external font loaded [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  PageCache: Add incremental (hot) extend for EPUB parser resume. Issue #41 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reader: Fix SPI bus conflict by stopping cache task during TOC overlay [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.4.1 (2026-02-09)

*  Update CHANGELOG.md [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add CSS parser safety limits and low-memory CSS skip [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Network: Fix WiFi scan reliability and add scan retry logic. Issue #39 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Network: Move Scan button to rightmost position in WiFi list [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.4.0 (2026-02-09)

*  Add Arabic text support with shaping, RTL layout, and CSS direction. Issue #33 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  TOC: Use current reader font instead of theme default for chapter list [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reader: Fix crash on exitToUI when background cache task times out [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Fix data URI buffer sizing, parse error recovery, and add no-progress guard. Issue #34 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Skip tiny decorative images (≤3px) during chapter parsing. Issue #34 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add abort support to image converters and increase cache task stack [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## monitor-v0.1.0 (2026-02-08)

*  Add CI workflow and docs for serial monitor tool [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.3.1 (2026-02-08)

*  ThemeManager: Validate theme filenames to reject special characters [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.3.0 (2026-02-08)

*  Epub: Give tall images dedicated pages with vertical centering. Issue #30 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  FileList: Add natural sort for file and directory names [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  WebServer: Buffer file uploads with 4KB write coalescing [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.2.3 (2026-02-08)

*  Change default font size from Small to Normal [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Track parser abort state to correctly resume partially parsed chapters. Issue #34 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Cache framebuffer pointer and make rotateCoordinates static [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update tests [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.2.2 (2026-02-08)

*  Fix wakeup verification after full power cycle by loading settings before button check. Issue #17 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Defer emergency text split outside XML callback to prevent stack overflow. Issue #35 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.2.1 (2026-02-07)

*  Fix power button held-time tracking across slow loops and add short-press page turn. Issue #32 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.2.0 (2026-02-07)

*  Move front button layout from theme to settings, add side button settings UI. Issue #31, #24 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix power button wakeup detection for USB-connected and standalone modes [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add side button remapping and defer button layout changes until settings exit [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.13 (2026-02-06)

*  Add XSmall (12pt) font size option with settings migration. Issue #15. [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  JpegToBmpConverter: Switch to contain-mode scaling and add tests. Issue #30 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  TOC: Use reader font for non-XTC content to support non-Latin glyphs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ChapterHtmlSlimParser: Skip aria-hidden anchors and add parsing tests [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ParsedText: Attach punctuation to preceding word without extra spacing [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Enable LTO for smaller binary size [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  SettingsViews: Fix font size option count from 3 to 4 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix XSmall default font [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Rewrite font conversion from Node.js to Python, standardize font naming, and update example fonts/themes [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.11 (2026-02-05)

*  Add line spacing setting with compact/normal/relaxed/large presets. Issue #23. [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Add multi-style support for streaming fonts. Issue #15 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Skip custom font loading for XTC content to save memory. Issue #21 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Strip embedded data URIs from EPUB HTML to prevent expat OOM. Issue #28 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.10 (2026-02-04)

*  Add XTC flatPage navigation path and unit tests for ReaderNavigation. Fix issue #20. [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*   Add streaming font system with LRU cache for memory efficient custom fonts. Fix issue #21. [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.9 (2026-02-02)

*  Fix coverDithering option and expand sunlightFadingFix range [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add unit tests [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Use BT.601 coefficients for RGB to grayscale conversion [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add quick cover preview mode with LUT-based RGB-to-gray conversion [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  JpegToBmp: Add detection for arithmetic-coded JPEG (SOF9) [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  BitmapHelpers: Add 1-bit BMP support for thumbnail scaling [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Restore HTML pages for web-based file manager interface [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Hide 'sleep' folder from file browser [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix short power button press ignored on wake-up [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.8 (2026-01-31)

*  Add AsyncTask lib with BackgroundTask for safe FreeRTOS task cancellation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add heuristic cover detection for common file paths [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.7 (2026-02-01)

*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Remove cumulative size tracking for page-based progress [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix reader state transitions and improve error handling for content loading and settings parsing [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Add magic signature and bounds checks for corrupted files [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.6 (2026-01-31)

*  Epub: Add better deserialization with checked reads and truncation limits [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix code style [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.5 (2026-01-31)

*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Html5Normalizer: Strip void element closing tags and handle EOF state [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.4 (2026-01-31)

*  SettingsViews: Fix status bar enum value count from 3 to 2 [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  EpdFont: Add size limits and memory validation for custom fonts [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ThemeManager: Add theme limit (16) and skip invalid themes [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  IniParser: Fix EOF handling with proper int return type [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.3 (2026-01-31)

*  ReaderState: Use mutex timeout to prevent deadlock after force-deleted task [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ReaderState: Reset pageCache on creation failure and recreate mutex after force-delete [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.2 (2026-01-31)

*  FileListState: Add pagination and persist directory position [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.1 (2026-01-30)

*  FileListState: Add file/folder delete with confirmation dialog [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ContentHandle: Add getCoverPath method and move thumbnail generation to ReaderState [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix background caching start [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.1.0 (2026-01-30)

*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update changelog [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix Settings menu freeze on first entry (#8) [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update documentation images with new device photos [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update documentation images with new device photos [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Refactor: Use unique_ptr for server, add explicit, extract epub locals [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update fonts samples and fonts converter [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix off-by-one in ZipFile nameLen bounds check [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add sunlight fading fix setting to prevent UV-induced screen fade on SSD1677 displays [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v1.0.0 (2026-01-29)

*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update changelog [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Validate enum values on load to prevent crashes [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  System and UI refactoring [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Rewrite font manager for CJK and Thai [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add boot mode system for memory-optimized Reader mode [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ReaderState: Extract cache helpers to reduce code duplication [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Skip boot splash when returning from Reader mode to UI mode [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix off-by-one validation bounds for transition settings [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add NetworkState with WiFi scanning, AP mode, and web file transfer [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeView: Replace menu navigation with direct button shortcuts [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeView: Move title/author block below book cover [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  SyncState: Store selected sync mode in Core before network transition [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  PageCache: Fix stack overflow when caching EPUB images [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ImageConverter: Extract unified image conversion with factory pattern [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Elements: Add bookPlaceholder for missing book covers [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Markdown: Extract title from ATX headers with SD caching [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeView: Extract cover area calculation and fix placeholder rendering [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  StatusBar: Simplify to 2 modes and fix null cache display [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Markdown: Replace md4c with streaming line-based parser [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add bounds checking and defensive guards across parsers and utilities [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add memory safety guards and compress HomeState cover thumbnail [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ZipFile: Add batch size lookup for large EPUBs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ReaderState: Use EPUB guide text-start for initial content navigation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  XTC: Add author metadata parsing and display [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ChapterHtmlSlimParser: Extract flushPartWordBuffer and add null guards [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Calibre wireless sync support [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix sync screens to navigate back to Sync menu instead of Settings [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Calibre: Implement wireless book sync with Calibre desktop [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  UI: Redesign book placeholder with new layout and bookmark ribbon [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add ButtonBar struct and integrate into UI views for consistent button hints [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Remove OPDS catalog support and related views [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.18.2 (2026-01-23)

*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Cache themes for instant switching and defer font loading [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Optimize section creation with word width cache and greedy line breaking [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Fall back to space width for missing typographic spaces [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.18.1 (2026-01-22)

*  BitmapHelpers: Increase contrast and make gamma correction optional [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeActivity: Generate cover thumbnails asynchronously [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Restructure flat list into category-based sub-UIs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix USB boot detection by releasing GPIO hold after deep sleep [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeActivity: Fix white-on-white cover fallback and add atomic thumbnail generation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Generate thumbnails from cached cover.bmp when available [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.18.0 (2026-01-22)

*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Markdown format support with MD4C parser [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Scripts: Add variable font support and preview to font converter [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add CSS parser for stylesheet-based text styling [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Text: Replace line breaking with Knuth-Plass algorithm [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Settings: Add cover dithering toggle for 1-bit BMP output [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Covers: Add PNG support, case-insensitive extensions, shared helpers [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  FsHelpers: Consolidate file type helpers from StringUtils and CoverHelpers [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix LittleFS/SdFat FILE_READ macro redefinition warnings [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.17.0 (2026-01-21)

*  Update CHANGELOG.md [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add glyph cache and enable image gamma/contrast enhancement [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Optimize spine-to-TOC mapping from O(n²) to O(n) [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Text Layout presets for paragraph indentation and spacing [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Refactor rendering parameters into RenderConfig struct [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add startup behavior setting to choose between home screen and last document [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.16.0 (2026-01-21)

*  Update changelog [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Group5 compression and ordered dithering from bb_epaper. Xtc: Fix misleading comment about 1-bit polarity [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.15.0 (2026-01-21)

*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add inline image support with PNG and JPEG rendering [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ImageBlock: Add [Image] placeholder fallback for failed renders [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Store power button duration in RTC memory [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add GPIO hold during deep sleep to keep X4 LDO enabled [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Skip long press requirement after flashing firmware [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Remove OTA firmware update functionality [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  SystemInfo: Add battery state display with percentage and voltage [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Show Images setting to toggle inline images and covers [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.14.0 (2026-01-17)

*  GfxRenderer: Add clearArea for partial framebuffer clearing [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add CJK font support with memory-optimized single-style loading [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Remove bold_italic fonts to save ~234KB flash, use bold fallback instead [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Add HTML5 void element normalizer to fix parsing errors [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Replace Python build scripts with Node.js for faster CJK font conversion [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Settings menu with Cleanup and System Info screens [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Home: Add book cover thumbnail display with 1-bit BMP conversion [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add text wrapping with hyphenation and multi-line chapter titles [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.13.0 (2026-01-15)

*  Update CHANGELOG.md [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix code style [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Nix shell for reproducible dev environment and CI builds [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Store PlatformIO packages in local project directory [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add System Info screen and display MAC address on WiFi selection [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add UTF-8 safe string truncation to prevent corrupting multi-byte characters [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Skip BOM characters in HTML parser to prevent rendering as question marks [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Show table omitted placeholder instead of silently dropping content [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Show image alt text as placeholder instead of skipping silently [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add explicit author prefixes to library dependencies [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Bump version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.12.0 (2026-01-13)

*  Settings: Add Clear Cache option to delete all book caches [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ClearCache: Use styled buttons matching main UI [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  StatusBar: Use dynamic margin for chapter title when battery hidden [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reader: Preserve page refresh counter across chapter boundaries [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  HomeActivity: Increase task stack size to prevent crashes [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Epub: Fix TOC navigation when nav file is in subdirectory [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix signed/unsigned mismatches, unaligned memory access, and add defensive checks [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.11.0 (2026-01-13)

*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  TOC: Use theme item height for consistent long-press navigation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add theme font support with example light themes and refactored font converter [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.10.0 (2026-01-13)

*  Fix BMP rendering gamma/brightness (#302) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Add page turn on power button press (#286) [[David Fischer](mailto:85546373+fischer-hub@users.noreply.github.com)]
*  Update documentation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  ContentOpfParser: Use hash map for O(1) manifest item lookup [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Add font grayscale support check for anti-aliasing [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  GfxRenderer: Add diagonal line support with Bresenham's algorithm [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  BookMetadataCache: Use hash map for O(1) TOC-to-spine lookup [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.9.0 (2026-01-07)

*  Update changelog [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add soft hyphen support for improved text layout [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add text anti-aliasing toggle setting [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  dd plain text (.txt) file reading support [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Optimize EPUB indexing and XTC rendering performance [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add plain text (.txt, .text) file reading support [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update version [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add "Never" option to auto-sleep timeout setting [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add case-insensitive file extension checks [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Support up to 500 character file names [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix conflicts after master merge [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Consolidate file type checks into StringUtils (DRY/KISS) [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Remove authentication type from hotspot QR code (#235) [[Dave Allie](mailto:dave@daveallie.com)]
*  Remove HTML entity parsing (#274) [[Dave Allie](mailto:dave@daveallie.com)]


## v0.8.0 (2026-01-04)

*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Calibre wireless device connection support [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Rename system directory from .crosspoint to .papyrix [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Consolidate calibre.ini, opds.ini, themes/, and fonts/ under /config/.   Hide /config from file browsers [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.7.3 (2026-01-04)

*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.7.2 (2026-01-04)

*  Fix OPDS crash on large feeds by streaming HTTP to parser [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add OPDS catalog search by keyword [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.7.0 (2026-01-03)



## v0.7.1 (2026-01-03)

*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix keyboard view [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix keyboard view [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Net Library (OPDS) for browsing and downloading books [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Refactor: Add FsHelpers::isHiddenFsItem() to consolidate duplicate logic [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix OPDS: match Settings UI style, fix URL building, improve SSL timeout [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix OPDS server list: display on first load, correct initial selection [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add selection indicator to OPDS server list [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix Settings navigation: return to correct view and preserve selection [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Show Back instead of Home in file browser when in subdirectory [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.6.0 (2026-01-02)

*  Filter system directories from book browser [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update CHANGELOG [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update images [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Optimize ePub chapter loading speed [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Remove redundant inTocNav variable from TocNavParser [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix race condition causing screen freeze after WiFi password entry [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix battery display always showing 100% on invalid readings [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.5.0 (2026-01-01)

*  Update readme [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Prevent device sleep during webserver and OTA activity [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix text overlap in File Transfer UI [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix memory error after WiFi use by auto-restarting device [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix keyboard entry UI to use standard button hints [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Redesign on-screen keyboard with full character grid layout [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.4.0 (2026-01-01)

*  Fix string termination, navigation wrap, and error handling [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Redesign home screen with book card template [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.3.0 (2025-12-31)

*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Optimize memory usage and fix static analysis warnings [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add EPUB 3 nav.xhtml TOC support [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add configurable pages per refresh setting [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix settings freeze and logic errors [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reorganize settings by category [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add paragraph alignment setting [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add battery indicator to home screen [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.2.0 (2025-12-31)

*  Add theming system with custom fonts and dark mode support [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update reader fonts [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Cherry-pick EPUB parsing improvements from master [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix file browser navigation for non-ASCII folder names (#178) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Hide "System Volume Information" folder (#184) [[Dave Allie](mailto:dave@daveallie.com)]
*  Show book title instead of "Select Chapter". (#169) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Remove duplicate USE_UTF8_LONG_NAMES flag from platformio.ini [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Reduce section.bin cache size with memory optimizations [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add book metadata display and theme display names [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.1.2 (2025-12-31)

*  Update Readme [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add command for release [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add information about xteink-epub-optimizer in README [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update README [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix critical bugs: version parsing crash, modulo by zero, buffer overflow [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix buffer overflow vulnerability in ZipFile [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix high priority bugs: render failure handling, string truncation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Prevent memory crash on large EPUBs with entry count check [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.1.1 (2025-12-30)

*  Add UTF-8 filename support and papyrix-flasher docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]


## v0.1.0 (2025-12-30)

*  Public release [[Dave Allie](mailto:dave@daveallie.com)]
*  Add web flashing instructions [[Dave Allie](mailto:dave@daveallie.com)]
*  Remove debug lines [[Dave Allie](mailto:dave@daveallie.com)]
*  Handle nested navpoint elements in nxc TOC [[Dave Allie](mailto:dave@daveallie.com)]
*  Use correct current page on reader screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Add file selection screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Adjust input button thresholds to support more devices [[Dave Allie](mailto:dave@daveallie.com)]
*  Support left and right buttons in reader and file picker [[Dave Allie](mailto:dave@daveallie.com)]
*  Update README.md features [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix hold to wake logic [[Dave Allie](mailto:dave@daveallie.com)]
*  Speedup boot by not waiting for Serial [[Dave Allie](mailto:dave@daveallie.com)]
*  Avoid ghosting on sleep screen by doing a full screen update [[Dave Allie](mailto:dave@daveallie.com)]
*  Show indexing text when indexing [[Dave Allie](mailto:dave@daveallie.com)]
*  Upgrade open-x4-sdk [[Dave Allie](mailto:dave@daveallie.com)]
*  Add directory picking to home screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Full screen refresh of EpubReaderScreen every 10 renders [[Dave Allie](mailto:dave@daveallie.com)]
*  Wrap up multiple font styles into EpdFontFamily [[Dave Allie](mailto:dave@daveallie.com)]
*  Add new boot logo screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Add UI font [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix memory leak with Epub object getting orphaned [[Dave Allie](mailto:dave@daveallie.com)]
*  Avoid leaving screens mid-display update [[Dave Allie](mailto:dave@daveallie.com)]
*  Update sleep screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Update README.md with checkout instructions [[Dave Allie](mailto:dave@daveallie.com)]
*  Use InputManager from community-sdk [[Dave Allie](mailto:dave@daveallie.com)]
*  Use reference passing for EpdRenderer [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix bug with selectin epubs inside of folders [[Dave Allie](mailto:dave@daveallie.com)]
*  Sort items on FileSelectionScreen [[Dave Allie](mailto:dave@daveallie.com)]
*  Add image to README [[Dave Allie](mailto:dave@daveallie.com)]
*  More pass by reference changes [[Dave Allie](mailto:dave@daveallie.com)]
*  Small cleanup [[Dave Allie](mailto:dave@daveallie.com)]
*  Add expat and swap out EPUB HTML parser (#2) [[Dave Allie](mailto:dave@daveallie.com)]
*  Version section bin files [[Dave Allie](mailto:dave@daveallie.com)]
*  Reduce number of full screen refreshes to once every 20 pages [[Dave Allie](mailto:dave@daveallie.com)]
*  Build and use 1-bit font, saves a good amount of space [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix 1-bit font pixel alignment of last byte [[Dave Allie](mailto:dave@daveallie.com)]
*  Sort directories first in file picker [[Dave Allie](mailto:dave@daveallie.com)]
*  Stream inflated EPUB HTMLs down to disk instead of inflating in memory (#4) [[Dave Allie](mailto:dave@daveallie.com)]
*  Move to SDK EInkDisplay and enable anti-aliased 2-bit text (#5) [[Dave Allie](mailto:dave@daveallie.com)]
*  Remove EpdRenderer and create new GfxRenderer [[Dave Allie](mailto:dave@daveallie.com)]
*  Cleanup serial output [[Dave Allie](mailto:dave@daveallie.com)]
*  Add drawCenteredText to GfxRenderer [[Dave Allie](mailto:dave@daveallie.com)]
*  Add version string to boot screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.3.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Add section in readme on flashing via xteink.dve.al [[Dave Allie](mailto:dave@daveallie.com)]
*  Restructure readme [[Dave Allie](mailto:dave@daveallie.com)]
*  Move to smart pointers and split out ParsedText class (#6) [[Dave Allie](mailto:dave@daveallie.com)]
*  Swap out babyblue font for pixelarial14 [[Dave Allie](mailto:dave@daveallie.com)]
*  Bump page file version [[Dave Allie](mailto:dave@daveallie.com)]
*  Show clearer indexing string [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.4.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Remove tinyxml2 dependency replace with expat parsers (#9) [[Dave Allie](mailto:dave@daveallie.com)]
*  Process lines into pages as they are built [[Dave Allie](mailto:dave@daveallie.com)]
*  Show end of book screen when navigating past last page [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.5.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Return -1 from getTocIndexForSpineIndex if TOC item does not exist [[Dave Allie](mailto:dave@daveallie.com)]
*  Add chapter selection screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Add user guide [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.5.1 [[Dave Allie](mailto:dave@daveallie.com)]
*  Update contribution instructions [[Dave Allie](mailto:dave@daveallie.com)]
*  Add comparison images [[Dave Allie](mailto:dave@daveallie.com)]
*  Add Github templates [[Dave Allie](mailto:dave@daveallie.com)]
*  Add cppcheck and formatter to CI (#19) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add Github Action to build release firmware on tag (#20) [[Dave Allie](mailto:dave@daveallie.com)]
*  Upgrade open-x4-sdk to fix white streaks on sleep screen (#21) [[Dave Allie](mailto:dave@daveallie.com)]
*  Settings Screen and first 2 settings (#18) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Fix formatting [[Dave Allie](mailto:dave@daveallie.com)]
*  Run CI action on PR as well as push [[Dave Allie](mailto:dave@daveallie.com)]
*  Fixed light gray text rendering [[Dave Allie](mailto:dave@daveallie.com)]
*  Parse cover image path from content.opf file (#24) [[Dave Allie](mailto:dave@daveallie.com)]
*  Feature/auto poweroff (#32) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Add Cyrillic range to fonts (#27) [[Arthur Tazhitdinov](mailto:lisnake@gmail.com)]
*  Wrap-around navigation in Settings. (#31) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Improve indent (#28) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Cut release 0.6.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Use single buffer mode for EInkDisplay (#34) [[Dave Allie](mailto:dave@daveallie.com)]
*  Use 6x8kB chunks instead of 1x48kB chunk for secondary display buffer (#36) [[Dave Allie](mailto:dave@daveallie.com)]
*  TOC location fix (#25) [[Arthur Tazhitdinov](mailto:lisnake@gmail.com)]
*  Add home screen (#42) [[Dave Allie](mailto:dave@daveallie.com)]
*  Calculate the progress in the book by file sizes of each chapter. (#38) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Rename Screens to Activities and restructure files (#44) [[Dave Allie](mailto:dave@daveallie.com)]
*  Bugfix for #46: don't look at previous chapters if in chapter 0. (#48) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Fix font readability by expanding blacks and trimming whites (#55) [[Dave Allie](mailto:dave@daveallie.com)]
*  Rendering "Indexing..." on white screen to avoid partial update [[Dave Allie](mailto:dave@daveallie.com)]
*  Caching of spine item sizes for faster book loading (saves 1-4 seconds). (#54) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Cut release 0.7.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Skip pagebreak blocks when parsing epub file (#58) [[Dave Allie](mailto:dave@daveallie.com)]
*  Custom sleep screen support with BMP reading (#57) [[Dave Allie](mailto:dave@daveallie.com)]
*  Bugfix/word spacing indented (#59) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Add bounds checking for TOC/spine array access (#64) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Fix title truncation crash for short titles (#63) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Check SD card initialization and show error on failure (#65) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Validate file handle when reading progress.bin (#66) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Handle empty spine in getBookSize() and calculateProgress() (#67) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Add NULL checks after fopen() in ZipFile (#68) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Option to short-press power button. (#56) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Randomly load Sleep Screen from /sleep/*bmp (if exists). (#71) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Cleanup indexing layout string [[Dave Allie](mailto:dave@daveallie.com)]
*  Add connect to Wifi and File Manager Webserver (#41) [[Brendan O'Leary](mailto:github@olearycrew.com)]
*  Replace cover.jpg [[Dave Allie](mailto:dave@daveallie.com)]
*  Update user guide [[Dave Allie](mailto:dave@daveallie.com)]
*  fix: add NULL checks for frameBuffer in GfxRenderer (#79) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  fix: add NULL check after malloc in readFileToMemory() (#81) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  fix: add bounds checks to Epub getter functions (#82) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  fix: add NULL checks after malloc in drawBmp() (#80) [[IFAKA](mailto:99131130+IFAKA@users.noreply.github.com)]
*  Build out lines when parsing html and holding >750 words in buffer (#73) [[Dave Allie](mailto:dave@daveallie.com)]
*  Keep ZipFile open to speed up getting file stats. (#76) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Allow any file to be uploaded (#84) [[Dave Allie](mailto:dave@daveallie.com)]
*  Small code cleanup (#83) [[Dave Allie](mailto:dave@daveallie.com)]
*  Extract EPUB TOC into temp file before parsing (#85) [[Dave Allie](mailto:dave@daveallie.com)]
*  Paginate book list and avoid out of bounds rendering (#86) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add JPG image support (#23) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add info about sleep screen customisation to user guide (#88) [[Sam Davis](mailto:sam@sjd.co)]
*  Prevent boot loop if last open epub crashes on load (#87) [[Dave Allie](mailto:dave@daveallie.com)]
*  Book cover sleep screen (#89) [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix incorrect justification of last line in paragraph (#90) [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix: restores cyrillic glyphs to Pixel Arial font (#70) [[Arthur Tazhitdinov](mailto:lisnake@gmail.com)]
*  Cut release 0.8.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Start power off sequence as soon as hold duration for the power button is reached (#93) [[Dave Allie](mailto:dave@daveallie.com)]
*  Give activities names and log when entering and exiting them (#92) [[Dave Allie](mailto:dave@daveallie.com)]
*  Cleanup of activities [[Dave Allie](mailto:dave@daveallie.com)]
*  Improve power button hold measurement for boot (#95) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Stream CrossPointWebServer data over JSON APIs (#97) [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.8.1 [[Dave Allie](mailto:dave@daveallie.com)]
*  Thoroughly deinitialise expat parsers before freeing them (#103) [[Dave Allie](mailto:dave@daveallie.com)]
*  OTA updates (#96) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add AP mode option for file transfers (#98) [[Brendan O'Leary](mailto:brendan@olearycrew.com)]
*  Pin espressif32 platform version [[Dave Allie](mailto:dave@daveallie.com)]
*  Standardize File handling with FsHelpers (#110) [[Dave Allie](mailto:dave@daveallie.com)]
*  Handle 16x16 MCU blocks in JPEG decoding (#120) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add support for blockquote, strong, and em tags (#121) [[Dave Allie](mailto:dave@daveallie.com)]
*  Prevent SD card error causing boot loop (#122) [[Dave Allie](mailto:dave@daveallie.com)]
*  New book.bin spine and table of contents cache (#104) [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.9.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Optimize glyph lookup with binary search (#125) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Normalize button hints (#130) [[Brendan O'Leary](mailto:brendan@olearycrew.com)]
*  Add Continue Reading menu and remember last book folder (#129) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Network details QR code (#113) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Fix QRCode import [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix QRCode import attempt 2 [[Dave Allie](mailto:dave@daveallie.com)]
*  fix(parser): remove MAX_LINES limit that truncates long chapters (#132) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Add 'Open' button hint to File Selection page (#136) [[Brendan O'Leary](mailto:brendan@olearycrew.com)]
*  Improve EPUB cover image quality with pre-scaling and Atkinson dithering (#116) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Add setting to enable status bar display options (#111) [[1991AcuraLegend](mailto:nz8q492bdb@privaterelay.appleid.com)]
*  Add retry logic and progress bar for chapter indexing (#128) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Fix issue where pressing back from chapter select would leave book (#137) [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix underscore on keyboard and standardize activity (#138) [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix exiting WifiSelectionActivity renderer early [[Dave Allie](mailto:dave@daveallie.com)]
*  Rotation Support (#77) [[Tannay](mailto:Tannay.chandhok@gmail.com)]
*  Allow entering into chapter select screen correctly [[Dave Allie](mailto:dave@daveallie.com)]
*  Fix rendering issue with entering keyboard from wifi screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Use font ascender height for baseline offset (#139) [[Dave Allie](mailto:dave@daveallie.com)]
*  Avoid jumping straight into chapter selection screen [[Dave Allie](mailto:dave@daveallie.com)]
*  Add XTC/XTCH ebook format support (#135) [[Eunchurn Park](mailto:eunchurn.park@gmail.com)]
*  Use confirmation release on home screen to detect action [[Dave Allie](mailto:dave@daveallie.com)]
*  Add font size setting (Small/Medium/Large) [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Use a JSON filter to avoid crashes when checking for updates (#141) [[Dave Allie](mailto:dave@daveallie.com)]
*  Cut release 0.10.0 [[Dave Allie](mailto:dave@daveallie.com)]
*  Redesign home screen with 2x2 grid layout [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update UI font from Ubuntu 10pt to 12pt [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Consolidate chapter page data into single file (#144) [[Dave Allie](mailto:dave@daveallie.com)]
*  Support swapping the functionality of the front buttons (#133) [[dangson](mailto:dangson@gmail.com)]
*  Change default font size to 16pt Normal [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add Makefile [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add reqs config for python scripts [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Change project name [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update User documentation [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix button layout text overlap in Settings UI [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Remove usused module [[Dave Allie](mailto:dave@daveallie.com)]
*  Add side button layout configuration while on reader (#147) [[Yona](mailto:yonatanrodriguezmartin7@gmail.com)]
*  Custom zip parsing (#140) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add sleep screen image converter script [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Replace boot/sleep logo with Papyrix papyrus scroll [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Recalibrated power button duration, decreased long setting slightly. (#149) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Improvement/settings selection by inversion (#152) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Shorten continueLabel to actual screen width. (#151) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Add book cover display when reading EPUBs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update docs [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add configurable sleep timeout setting [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update Readme [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Add chapter select support to XTC files (#145) [[Sam Davis](mailto:sam@sjd.co)]
*  Accept big endian version in XTC files (#159) [[Dave Allie](mailto:dave@daveallie.com)]
*  Update readme [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Split XTC file version into major minor bytes (#161) [[Dave Allie](mailto:dave@daveallie.com)]
*  Add option to apply format fix only on changed files (much faster) (#153) [[Jonas Diemer](mailto:jonasdiemer@gmail.com)]
*  Add exFAT support (#150) [[Dave Allie](mailto:dave@daveallie.com)]
*  Update Makefile [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Update readme [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]
*  Fix after merge [[Pavel Liashkov](mailto:pavel.liashkov@protonmail.com)]

