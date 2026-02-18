# Papyrix

[![Changelog](https://img.shields.io/badge/changelog-CHANGELOG.md-blue)](CHANGELOG.md)
[![User Guide](https://img.shields.io/badge/docs-User_Guide-green)](docs/user_guide.md)
[![Customization](https://img.shields.io/badge/docs-Customization-green)](docs/customization.md)
[![Fonts](https://img.shields.io/badge/docs-Fonts-green)](docs/fonts.md)
[![Architecture](https://img.shields.io/badge/docs-Architecture-green)](docs/architecture.md)
[![Device Specs](https://img.shields.io/badge/docs-Device_Specs-green)](docs/device-specifications.md)
[![File Formats](https://img.shields.io/badge/docs-File_Formats-green)](docs/file-formats.md)
[![Images](https://img.shields.io/badge/docs-Images-green)](docs/images.md)
[![SSD1677 Driver](https://img.shields.io/badge/docs-SSD1677_Driver-green)](docs/ssd1677-driver.md)
[![Webserver](https://img.shields.io/badge/docs-Webserver-green)](docs/webserver.md)
[![Calibre](https://img.shields.io/badge/docs-Calibre_Wireless-green)](docs/calibre.md)


A lightweight, user-friendly firmware fork for the **Xteink X4** e-paper display reader.
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

Papyrix is a fork of [CrossPoint Reader](https://github.com/daveallie/crosspoint-reader), focused on creating a light, streamlined reading experience with improved UI defaults.

![Home screen](./docs/images/device.jpg)

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
- [x] CSS stylesheet parsing (text-align, font-style, font-weight, text-indent, margins, direction)
- [x] FB2 (FictionBook 2.0) support with metadata, TOC navigation, and metadata caching (no inline images)
- [x] XTC/XTCH native format support
- [x] Markdown (.md, .markdown) file support with formatting
- [x] Plain text (.txt, .text) file support
- [x] Saved reading position
- [x] Book cover display (JPG/JPEG/PNG/BMP, case-insensitive)
- [x] Table of contents navigation
- [x] Image support within EPUB (JPEG/PNG/BMP, baseline JPEG only, max 2048×3072)

### Text & Display
- [x] Configurable font sizes (XSmall/Small/Normal/Large)
- [x] Paragraph alignment (Justified/Left/Center/Right)
- [x] Text layout presets (Compact/Standard/Large) for indentation and spacing
- [x] Soft hyphen support for text layout
- [x] Native Vietnamese, Thai, and Greek support in builtin fonts
- [x] CJK (Chinese/Japanese/Korean) text layout
- [x] Thai text rendering with proper mark positioning
- [x] Arabic text shaping - contextual forms, Lam-Alef ligatures with RTL layout (book text only, not UI or File manager)
- [x] Knuth-Plass line breaking algorithm (TeX-quality justified text)
- [x] Text anti-aliasing toggle (grayscale text rendering for builtin fonts)
- [x] Pages per refresh setting (1/5/10/15/30)
- [x] Sunlight fading fix (powers down display after refresh to prevent UV fading)
- [x] 4 screen orientations

### Customization
- [x] Custom themes from SD card (`/config/themes/`)
- [x] Custom fonts from SD card (`/config/fonts/`, .epdfont format)
- [x] Custom sleep screens (Dark/Light/Custom/Cover modes)
- [x] Button remapping (side and front buttons)
- [x] Power button page turn (one-handed reading)

### Network & Connectivity
- [x] WiFi file transfer (web server)
- [x] Calibre Wireless Device - Send books from Calibre desktop

### Maintenance
- [x] Cleanup menu (clear caches, fonts, factory reset)
- [x] System info (version, uptime, memory, storage)

### File System
- [x] exFAT and FAT32 SD card support
- [x] UTF-8 filenames (Cyrillic, etc.)
- [x] File explorer with nested folders
- [x] Hidden system folders filtering (LOST.DIR, $RECYCLE.BIN, etc.)

See [the user guide](docs/user_guide.md) for operating instructions, and the [customization guide](docs/customization.md) for themes and fonts. Example theme and font files are available in [`docs/examples/`](docs/examples/).

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
* Python 3.12+ with [uv](https://docs.astral.sh/uv/) (for font conversion)
* Node.js 18+ (for sleep screen and logo scripts)
* USB-C cable for flashing the ESP32-C3
* Xteink X4

Install Node.js dependencies (for sleep screen and logo scripts):
```bash
cd scripts && npm install
```

### Using Nix (Recommended)

If you have [Nix](https://nixos.org/) installed, all dependencies are provided via `shell.nix`:

```bash
# Enter development environment
nix-shell

# Or run commands directly
nix-shell --run "make build"
nix-shell --run "make check"
```

First-time Nix setup:
```bash
# Install Nix (if not installed)
sh <(curl -L https://nixos.org/nix/install) --daemon

# Add nixpkgs channel
nix-channel --add https://nixos.org/channels/nixos-unstable nixpkgs
nix-channel --update
```

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

### Build Scripts

Build scripts are in the `scripts/` directory.

#### Converting fonts

Convert TTF/OTF fonts to Papyrix `.epdfont` format using Python (requires [uv](https://docs.astral.sh/uv/)):

```bash
# Basic conversion (outputs to current directory)
uv run scripts/fontconvert.py my-font -r MyFont-Regular.ttf --2bit

# Full font family with all reader sizes (14, 16, 18pt)
uv run scripts/fontconvert.py my-font -r Regular.ttf -b Bold.ttf --2bit --all-sizes -o /tmp/fonts/

# With Thai script support
uv run scripts/fontconvert.py my-font -r Regular.ttf --2bit --thai -o /tmp/fonts/

# With Arabic script support
uv run scripts/fontconvert.py my-font -r Regular.ttf --2bit --arabic -o /tmp/fonts/

# Generate C header instead of binary (for builtin fonts)
uv run scripts/fontconvert.py my_font 16 Regular.ttf --2bit > my_font_16_2b.h
```

Options: `-r/--regular`, `-b/--bold`, `-i/--italic`, `-o/--output`, `-s/--size`, `--2bit`, `--all-sizes`, `--header`, `--thai`, `--arabic`

See [customization guide](docs/customization.md) for detailed font conversion instructions.

#### Creating sleep screen images

Convert any image to sleep screen BMP format (requires `cd scripts && npm install`):

```bash
# Via Makefile
make sleep-screen INPUT=photo.jpg OUTPUT=sleep.bmp
make sleep-screen INPUT=photo.jpg OUTPUT=sleep.bmp ARGS='--dither --bits 8'

# Or directly
cd scripts && node create-sleep-screen.mjs photo.jpg sleep.bmp --dither --bits 8
```

Options:
- `--orientation portrait|landscape` - Screen orientation (default: portrait)
- `--bits 2|4|8` - Output bit depth (default: 4)
- `--dither` - Enable Floyd-Steinberg dithering
- `--fit contain|cover|stretch` - Resize mode (default: contain)

Copy the output BMP to `/sleep/` directory or as `/sleep.bmp` on the SD card.

#### Converting logo

Convert image to C header for firmware logo (128x128 monochrome):

```bash
cd scripts && node convert-logo.mjs logo.png ../src/images/PapyrixLogo.h
```

Options: `--invert`, `--threshold <0-255>`, `--rotate <0|90|180|270>`

#### Calibre simulators (development/testing)

Two simulators are provided for testing the Calibre Wireless Device feature without real hardware:

```bash
cd scripts

# Simulate a Papyrix device (for testing Calibre desktop connection)
node device-simulator.mjs

# Simulate Calibre desktop (for testing device firmware)
node calibre-simulator.mjs
```

The device simulator listens for Calibre broadcasts and can receive books (saved to `scripts/received_books/`). The Calibre simulator broadcasts discovery packets and sends test books to connected devices.

#### Serial monitor

A standalone Go binary for reading device logs without PlatformIO. Pre-built binaries are available on the [releases page](https://github.com/pliashkou/papyrix/releases), or build from source:

```bash
cd tools/monitor && go build -o monitor .
```

Usage:
```bash
./monitor                                  # Auto-detect port
./monitor -port /dev/ttyACM0               # Explicit port
./monitor -port /dev/ttyACM0 -log out.txt  # Also save to file
./monitor -speed 921600                    # Custom baud rate (default: 115200)
```

#### Reader test (desktop)

A desktop tool for testing the content parsing pipeline (EPUB, TXT, Markdown) without flashing to hardware. Useful for catching parsing bugs, layout issues, or crashes.

```bash
# Build only
make reader-test

# Build and process a book
make reader-test FILE=book.epub OUTPUT=/tmp/cache

# Dump parsed text content of each page
tools/reader-test/build/reader-test --dump book.epub /tmp/cache
```

Options:
- `--dump` — Print the parsed text content of each page (useful for verifying entity resolution, text extraction, and layout)

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

Papyrix is designed for the ESP32-C3's ~380KB RAM constraint. See [docs/architecture.md](docs/architecture.md) for detailed architecture documentation.

### Core Architecture

- **State Machine**: 10 pre-allocated states (Home, Reader, Settings, etc.) with lifecycle hooks
- **Dual-Boot System**: UI mode (full features) vs Reader mode (minimal, maximum RAM) - device restarts between modes
- **Content Providers**: Unified `ContentHandle` interface for EPUB, XTC, TXT, Markdown, and FB2 formats
- **PageCache**: Partial page caching with background pre-rendering

### WiFi and Memory

The ESP32 WiFi stack allocates ~100KB and fragments heap memory in a way that cannot be recovered at runtime. After using WiFi features (File Transfer or Calibre Wireless), the device automatically restarts to reclaim memory.

### Performance Optimizations

**Hash-based lookups**: EPUB spine/TOC and glyph caches use FNV-1a hashing for O(1) lookups.

**EPUB indexing**: Manifest item lookup uses an in-memory hash map for O(1) resolution of spine itemrefs. TOC-to-spine mapping also uses a hash map for O(1) href-to-index resolution.

**XTC rendering**: 1-bit monochrome pages use byte-level processing. All-white bytes (common in margins) are skipped entirely.

**Group5 compression**: 1-bit image data uses CCITT Group5 compression for fast decompression and reduced SD card I/O.

**Word width caching**: 256-entry cache in GfxRenderer avoids repeated font measurements.

**Image caching**: EPUB images are converted to BMP once and cached to `/.papyrix/epub_<hash>/images/`. Failed conversions are marked to avoid re-processing.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the cache. This cache directory exists at `.papyrix` on the SD card. The structure is as follows:


```
.papyrix/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   ├── sections/        # All chapter data is stored in the sections subdirectory
│   │   ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│   │   ├── 1.bin        #     files are named by their index in the spine
│   │   └── ...
│   └── images/          # Cached inline images (converted to 2-bit BMP)
│       ├── 123456.bmp   # Images named by hash of source path
│       └── ...
│
├── fb2_55667788/        # Each FB2 file is cached to a subdirectory named `fb2_<hash>`
│   ├── meta.bin         # Cached metadata (title, author, TOC) for faster reloads
│   ├── progress.bin     # Stores reading progress
│   ├── cover.bmp        # Cover image (converted from adjacent image file)
│   ├── sections/        # Cached chapter pages (same format as EPUB sections)
│   │   ├── 0.bin
│   │   └── ...
│
│
├── txt_98765432/        # Each TXT file is cached to a subdirectory named `txt_<hash>`
│   ├── progress.bin     # Stores current page number (4-byte uint32)
│   ├── index.bin        # Page index (byte offsets for each page start)
│   └── cover.bmp        # Cover image (converted from book.jpg/png/bmp or cover.jpg/png/bmp)
│
├── md_12345678/         # Each Markdown file is cached to a subdirectory named `md_<hash>`
│   ├── progress.bin     # Stores current page number (2-byte uint16)
│   ├── section.bin      # Parsed pages (same format as EPUB sections)
│   └── cover.bmp        # Cover image (converted from README.jpg/png/bmp or cover.jpg/png/bmp)
│
└── epub_189013891/
```

To clear cached data, use **Settings > Cleanup**:
- **Clear Book Cache** — Delete all cached book data and reading progress
- **Clear Device Storage** — Erase internal flash storage (requires restart)
- **Factory Reset** — Erase all data (caches, settings, WiFi, fonts) and restart

Alternatively, deleting the `.papyrix` directory manually will clear the book cache.

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

X4 hardware insights from [bb_epaper](https://github.com/bitbank2/bb_epaper) by Larry Bank.

Markdown parsing using [MD4C](https://github.com/mity/md4c) by Martin Mitáš.

CSS parser adapted from [microreader](https://github.com/CidVonHighwind/microreader) by CidVonHighwind.

**Not affiliated with Xteink or any manufacturer of the X4 hardware**.
