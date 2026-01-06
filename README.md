# Papyrix

[![Changelog](https://img.shields.io/badge/changelog-CHANGELOG.md-blue)](CHANGELOG.md)
[![User Guide](https://img.shields.io/badge/docs-User_Guide-green)](docs/user_guide.md)
[![Customization](https://img.shields.io/badge/docs-Customization-green)](docs/customization.md)
[![File Formats](https://img.shields.io/badge/docs-File_Formats-green)](docs/file-formats.md)
[![Webserver](https://img.shields.io/badge/docs-Webserver-green)](docs/webserver.md)
[![Calibre](https://img.shields.io/badge/docs-Calibre_Wireless-green)](docs/calibre.md)

A lightweight, user-friendly firmware fork for the **Xteink X4** e-paper display reader.
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

Papyrix is a fork of [CrossPoint Reader](https://github.com/daveallie/crosspoint-reader), focused on creating a light, streamlined reading experience with improved UI defaults.

![](./docs/images/cover.jpg)

## Motivation

E-paper devices are fantastic for reading, but most commercially available readers are closed systems with limited customisation. The **Xteink X4** is an affordable e-paper device, however the official firmware remains closed.

Papyrix aims to:
* Provide a **lightweight, open-source alternative** to the official firmware.
* Offer a **document reader** capable of handling EPUB content on constrained hardware.
* Support **customisable font, layout, and display** options.
* Run purely on the **Xteink X4 hardware**.

This project is **not affiliated with Xteink**; it's built as a community project.

## Features

### Reading & Format Support
- [x] EPUB 2 and EPUB 3 parsing (nav.xhtml with NCX fallback)
- [x] XTC/XTCH native format support
- [x] Plain text (.txt, .text) file support
- [x] Saved reading position
- [x] Book cover display (configurable)
- [x] Table of contents navigation
- [ ] Image support within EPUB

### Text & Display
- [x] Configurable font sizes (Small/Medium/Large)
- [x] Paragraph alignment (Justified/Left/Center/Right)
- [x] Soft hyphen support for text layout
- [x] Text anti-aliasing toggle (grayscale text rendering)
- [x] Pages per refresh setting (1/5/10/15/30)
- [x] 4 screen orientations

### Customization
- [x] Custom themes from SD card (`/config/themes/`)
- [x] Custom fonts from SD card (`/config/fonts/`, .epdfont format)
- [x] Custom sleep screens (Dark/Light/Custom/Cover modes)
- [x] Button remapping (side and front buttons)

### Network & Connectivity
- [x] WiFi file transfer (web server)
- [x] Net Library (OPDS) - Browse and download from OPDS servers
- [x] Calibre Wireless Device - Send books from Calibre desktop
- [x] OTA firmware updates

### File System
- [x] exFAT and FAT32 SD card support
- [x] UTF-8 filenames (Cyrillic, etc.)
- [x] File explorer with nested folders
- [x] Hidden system folders filtering (LOST.DIR, $RECYCLE.BIN, etc.)

See [the user guide](docs/user_guide.md) for operating instructions, and the [customization guide](docs/customization.md) for themes and fonts.

## Installing

### Using Papyrix Flasher (Recommended)

The easiest way to install Papyrix is using [papyrix-flasher](https://github.com/bigbag/papyrix-flasher) — a cross-platform CLI tool with auto-detection and embedded bootloader. Download the latest release for your platform and run:

```bash
papyrix-flasher flash firmware.bin
```

### Manual Build

See [Development](#development) below.

## Development

### Prerequisites

* **PlatformIO Core** (`pio`) or **VS Code + PlatformIO IDE**
* Python 3.8+
* USB-C cable for flashing the ESP32-C3
* Xteink X4

### Checking out the code

Papyrix uses PlatformIO for building and flashing the firmware. To get started, clone the repository:

```
git clone --recursive https://github.com/pliashkou/papyrix

# Or, if you've already cloned without --recursive:
git submodule update --init --recursive
```

### Building

```sh
# Build firmware
make build

# Build release firmware
make release

# Or using PlatformIO directly
pio run
```

### Flashing your device

Connect your Xteink X4 to your computer via USB-C and run the following command.

```sh
make flash

# Or using PlatformIO directly
pio run --target upload
```

You can also flash using esptool directly (useful if you have a pre-built firmware binary):

```sh
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 460800 \
  write_flash -z 0x0 firmware.bin
```

Replace `/dev/ttyACM0` with your device port (e.g., `COM3` on Windows, `/dev/tty.usbmodem*` on macOS).

### Creating sleep screen images

Convert any image to a sleep screen format compatible with Papyrix:

```sh
make sleep-screen INPUT=photo.jpg OUTPUT=sleep.bmp

# With options
make sleep-screen INPUT=photo.jpg OUTPUT=sleep.bmp ARGS='--dither --bits 8'
```

Options:
- `--orientation portrait|landscape` - Screen orientation (default: portrait)
- `--bits 2|4|8` - Output bit depth (default: 4)
- `--dither` - Enable Floyd-Steinberg dithering
- `--fit contain|cover|stretch` - Resize mode (default: contain)

Copy the output BMP to `/sleep/` directory or as `/sleep.bmp` on the SD card.

### Creating a GitHub release

```sh
# With auto-generated notes from commits
make gh-release VERSION=0.1.1

# With custom notes
make gh-release VERSION=0.1.1 NOTES="Release notes here"
```

### Generating changelog

Generate `CHANGELOG.md` from git tags and commit history:

```sh
make changelog
```

This creates a changelog grouped by version tags, with commit messages and author information.

## Internals

Papyrix is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based on this constraint.

### WiFi and memory

The ESP32 WiFi stack allocates ~100KB and fragments heap memory in a way that cannot be recovered at runtime. After using WiFi features (File Transfer or OTA updates), XTC books require ~96KB of contiguous memory for page rendering. To ensure reliable operation, the device automatically restarts after exiting WiFi mode to reclaim memory.

### Performance optimizations

Papyrix includes several performance optimizations for the constrained ESP32-C3 environment:

**EPUB indexing**: TOC-to-spine mapping uses in-memory href caching to avoid repeated SD card reads during book indexing. This reduces indexing time from O(n²) disk operations to O(n) memory lookups.

**XTC rendering**: 1-bit monochrome pages use byte-level processing instead of pixel-by-pixel iteration. All-white bytes (common in margins) are skipped entirely, and all-black bytes are processed in bulk.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the cache. This cache directory exists at `.papyrix` on the SD card. The structure is as follows:


```
.papyrix/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
├── txt_98765432/        # Each TXT file is cached to a subdirectory named `txt_<hash>`
│   ├── progress.bin     # Stores current page number (4-byte uint32)
│   ├── index.bin        # Page index (byte offsets for each page start)
│   └── cover.bmp        # Cover image (if found in same directory as TXT file)
│
└── epub_189013891/
```

Deleting the `.papyrix` directory will clear the entire cache.

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Related Tools

### EPUB to XTC Converter (Web)

[epub-to-xtc-converter](https://github.com/bigbag/epub-to-xtc-converter) — browser-based converter from EPUB to Xteink's native XTC/XTCH format. Uses CREngine WASM for accurate rendering.

- Device presets for Xteink X4/X3 (480x800)
- Font selection from Google Fonts or custom TTF/OTF
- Configurable margins, line height, hyphenation (42 languages)
- Dark mode and dithering options
- Batch processing and ZIP export

**Live version:** [liashkov.site/epub-to-xtc-converter](https://liashkov.site/epub-to-xtc-converter/)

### EPUB Optimizer (CLI)

[xteink-epub-optimizer](https://github.com/bigbag/xteink-epub-optimizer) — command-line tool to optimize EPUB files for the Xteink X4's constraints (480×800 display, limited RAM):

- **CSS Sanitization** - Removes complex layouts (floats, flexbox, grid)
- **Font Removal** - Strips embedded fonts to reduce file size
- **Image Optimization** - Grayscale conversion, resizing to 480px max width
- **XTC/XTCH Conversion** - Convert EPUBs to Xteink's native format

```bash
# Optimize EPUB
python src/optimizer.py ./ebooks ./optimized

# Convert to XTCH format
python src/converter.py book.epub book.xtch --font fonts/MyFont.ttf
```

## Contributing

Contributions are very welcome!

### To submit a contribution:

1. Fork the repo
2. Create a branch (`feature/your-feature`)
3. Make changes
4. Submit a PR

---

Papyrix is a fork of [CrossPoint Reader](https://github.com/daveallie/crosspoint-reader) by Dave Allie.

**Not affiliated with Xteink or any manufacturer of the X4 hardware**.
