# Papyrix

A lightweight, user-friendly firmware fork for the **Xteink X4** e-paper display reader.
Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

Papyrix is a fork of [CrossPoint Reader](https://github.com/daveallie/crosspoint-reader), focused on creating a light, streamlined reading experience with improved UI defaults.

![](./docs/images/cover.jpg)

## Fork Information

**Papyrix** (from "papyrus" - ancient scrolls) is a community fork with the goal of providing a lighter, more user-friendly firmware version while maintaining full compatibility with the original CrossPoint Reader.

### Changes from Original

- **Font size setting** - Added configurable font sizes (Small/Medium/Large)
- **Redesigned home screen** - New 2x2 grid layout (or classic list view, configurable)
- **Improved UI font** - Updated from Ubuntu 10pt to 12pt for better readability
- **Better default font size** - Changed to 16pt Normal for comfortable reading
- **Book cover display** - Show book cover as first page when reading (configurable)
- **Sleep timeout setting** - Configurable auto-sleep timeout (5/10/15/30 min)
- **UTF-8 filename support** - Cyrillic and other non-ASCII characters in file names

## Motivation

E-paper devices are fantastic for reading, but most commercially available readers are closed systems with limited customisation. The **Xteink X4** is an affordable e-paper device, however the official firmware remains closed.

Papyrix aims to:
* Provide a **lightweight, open-source alternative** to the official firmware.
* Offer a **document reader** capable of handling EPUB content on constrained hardware.
* Support **customisable font, layout, and display** options.
* Run purely on the **Xteink X4 hardware**.

This project is **not affiliated with Xteink**; it's built as a community project.

## Features & Usage

- [x] EPUB parsing and rendering
- [ ] Image support within EPUB
- [x] Saved reading position
- [x] exFAT SD card support (in addition to FAT32)
- [x] UTF-8 filenames (Cyrillic, etc.)
- [x] File explorer with file picker
  - [x] Basic EPUB picker from root directory
  - [x] Support nested folders
  - [ ] EPUB picker with cover art
- [x] Custom sleep screen
  - [x] Cover sleep screen
- [x] Book cover display when reading
- [x] Wifi book upload
- [ ] Wifi OTA updates
- [x] Configurable font size options
- [ ] Screen rotation

See [the user guide](./USER_GUIDE.md) for instructions on operating Papyrix.

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

## Internals

Papyrix is pretty aggressive about caching data down to the SD card to minimise RAM usage. The ESP32-C3 only has ~380KB of usable RAM, so we have to be careful. A lot of the decisions made in the design of the firmware were based on this constraint.

### Data caching

The first time chapters of a book are loaded, they are cached to the SD card. Subsequent loads are served from the cache. This cache directory exists at `.crosspoint` on the SD card. The structure is as follows:


```
.crosspoint/
├── epub_12471232/       # Each EPUB is cached to a subdirectory named `epub_<hash>`
│   ├── progress.bin     # Stores reading progress (chapter, page, etc.)
│   ├── cover.bmp        # Book cover image (once generated)
│   ├── book.bin         # Book metadata (title, author, spine, table of contents, etc.)
│   └── sections/        # All chapter data is stored in the sections subdirectory
│       ├── 0.bin        # Chapter data (screen count, all text layout info, etc.)
│       ├── 1.bin        #     files are named by their index in the spine
│       └── ...
│
└── epub_189013891/
```

Deleting the `.crosspoint` directory will clear the entire cache.

Due the way it's currently implemented, the cache is not automatically cleared when a book is deleted and moving a book file will use a new cache directory, resetting the reading progress.

For more details on the internal file structures, see the [file formats document](./docs/file-formats.md).

## Related Tools

### EPUB Optimizer

[xteink-epub-optimizer](https://github.com/bigbag/xteink-epub-optimizer) optimizes EPUB files for the Xteink X4's constraints (480×800 display, limited RAM):

- **CSS Sanitization** - Removes complex layouts (floats, flexbox, grid)
- **Font Removal** - Strips embedded fonts to reduce file size
- **Image Optimization** - Grayscale conversion, resizing to 480px max width
- **XTC/XTCH Conversion** - Convert EPUBs to Xteink's native format

```bash
# Optimize EPUB
python src/optimizer.py ./ebooks ./optimized

# Convert to XTCH format
python src/converter.py book.epub book.xtch --font fonts/Bookerly.ttf
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
