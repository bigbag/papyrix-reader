# Customization Guide

This guide explains how to create custom themes and add custom fonts to Papyrix.

---

## Custom Themes

Papyrix supports user-customizable themes stored on the SD card. Themes control colors, layout options, and button mappings.

### Theme File Location

Theme files are stored in the `/config/themes/` directory on the SD card:

```
/config/themes/
├── light.theme      # Default light theme
├── dark.theme       # Default dark theme
└── my-custom.theme  # Your custom theme
```

When you first use the device, default `light.theme` and `dark.theme` files are created automatically.

### Creating a Custom Theme

1. Copy [example.theme](example.theme) or an existing theme file from your device
2. Rename it (e.g., `my-custom.theme`)
3. Edit the file with any text editor
4. Place it in `/config/themes/` on your SD card
5. Restart the device and select your theme in **Settings > Reader > Theme**

### Theme Limits

- **Maximum themes**: 16 themes can be displayed in the Settings UI
- **Theme name length**: Maximum 31 characters
- Themes beyond the limit are ignored with a log warning (alphabetical order by filename)
- If a theme file is invalid or fails to parse, the device skips it and logs a warning

### Theme File Format

Theme files use a simple INI format:

```ini
# Papyrix Theme Configuration
# Edit values and restart device to apply

[theme]
name = My Custom Theme    # Display name shown in Settings UI (optional)

[colors]
inverted_mode = false     # true = dark mode, false = light mode
background = white        # white or black

[selection]
fill_color = black        # Selection highlight color
text_color = white        # Text on selection

[text]
primary_color = black     # Normal text color
secondary_color = black   # Secondary/dimmed text color

[layout]
margin_top = 9            # Top margin in pixels
margin_side = 3           # Side margin in pixels
item_height = 30          # Menu item height
item_spacing = 0          # Space between menu items
front_buttons = bclr      # bclr or lrbc (see below)

[fonts]
ui_font =                 # UI font family (empty = builtin)
reader_font_small =       # Reader font for small size (empty = builtin)
reader_font_medium =      # Reader font for medium size (empty = builtin)
reader_font_large =       # Reader font for large size (empty = builtin)
```

### Configuration Options

#### Theme Section

Optional metadata for the theme:

- **name** - Display name shown in the Settings UI
  - If not specified, the filename (without extension) is used
  - Example: `name = Dark Noto Serif`

#### Colors Section

- **inverted_mode** - Enable dark mode (inverted colors)
  - Values: `true` or `false`
- **background** - Screen background color
  - Values: `white` or `black`

#### Selection Section

- **fill_color** - Highlight color for selected items
  - Values: `white` or `black`
- **text_color** - Text color on selected items
  - Values: `white` or `black`

#### Text Section

- **primary_color** - Main text color
  - Values: `white` or `black`
- **secondary_color** - Secondary/dimmed text color
  - Values: `white` or `black`

#### Layout Section

- **margin_top** - Top screen margin in pixels
  - Default: `9`
- **margin_side** - Side screen margins in pixels
  - Default: `3`
- **item_height** - Height of menu items in pixels
  - Default: `30`
  - Minimum: `1` (values of 0 will cause errors)
  - Affects file browser and menu navigation (including long-press page skip)
  - Note: Chapter selection screens use automatic 2-line item heights based on font size
- **item_spacing** - Vertical space between items in pixels
  - Default: `0`
- **front_buttons** - Front button mapping
  - Values: `bclr` (Back, Confirm, Left, Right) or `lrbc` (Left, Right, Back, Confirm)

#### Fonts Section

- **ui_font** - Custom UI font family name
  - Leave empty to use builtin font
- **reader_font_small** - Custom reader font for small size (14pt)
  - Leave empty to use builtin font
- **reader_font_medium** - Custom reader font for medium size (16pt)
  - Leave empty to use builtin font
- **reader_font_large** - Custom reader font for large size (18pt)
  - Leave empty to use builtin font

### Example: Dark Theme

```ini
[colors]
inverted_mode = true
background = black

[selection]
fill_color = white
text_color = black

[text]
primary_color = white
secondary_color = white

[layout]
margin_top = 9
margin_side = 3
item_height = 30
item_spacing = 0
front_buttons = bclr

[fonts]
ui_font =
reader_font_small =
reader_font_medium =
reader_font_large =
```

### Example: Compact Theme

```ini
[colors]
inverted_mode = false
background = white

[selection]
fill_color = black
text_color = white

[text]
primary_color = black
secondary_color = black

[layout]
margin_top = 5
margin_side = 5
item_height = 25
item_spacing = 2
front_buttons = bclr

[fonts]
ui_font =
reader_font_small =
reader_font_medium =
reader_font_large =
```

### Example: Theme with Custom Fonts

```ini
[colors]
inverted_mode = false
background = white

[selection]
fill_color = black
text_color = white

[text]
primary_color = black
secondary_color = black

[layout]
margin_top = 9
margin_side = 3
item_height = 30
item_spacing = 0
front_buttons = bclr

[fonts]
ui_font = noto-sans
reader_font_small = noto-serif-14
reader_font_medium = noto-serif-16
reader_font_large = noto-serif-18
```

This theme uses custom fonts:
- UI: `/config/fonts/noto-sans/`
- Reader (small): `/config/fonts/noto-serif-14/`
- Reader (medium): `/config/fonts/noto-serif-16/`
- Reader (large): `/config/fonts/noto-serif-18/`

If any font directory doesn't exist, the device falls back to the builtin font for that size.

---

## Custom Fonts

Papyrix supports loading custom fonts from the SD card. Fonts must be pre-converted to the `.epdfont` binary format.

### Font File Location

Custom fonts are stored in the `/config/fonts/` directory, organized by font family:

```
/config/fonts/
├── my-font/
│   ├── regular.epdfont
│   ├── bold.epdfont
│   └── italic.epdfont
└── another-font/
    └── regular.epdfont
```

Each font family is a subdirectory containing one or more style variants.

### Converting Fonts

To create `.epdfont` files from TTF/OTF fonts, use the `fontconvert.py` script included in the firmware source code (`scripts/fontconvert.py`).

#### Requirements

- Python 3.12+
- [uv](https://docs.astral.sh/uv/) package manager (dependencies are handled automatically via inline script metadata)

#### Basic Usage

Convert a complete font family:

```bash
uv run scripts/fontconvert.py my-font \
    -r MyFont-Regular.ttf \
    -b MyFont-Bold.ttf \
    -i MyFont-Italic.ttf \
    --2bit \
    -o /path/to/output/
```

Convert only the regular style:

```bash
uv run scripts/fontconvert.py my-font -r MyFont-Regular.ttf --2bit -o /tmp/fonts/
```

#### Options

- **-r, --regular** - Path to regular style font (required for binary mode)
- **-b, --bold** - Path to bold style font
- **-i, --italic** - Path to italic style font
- **-o, --output** - Output directory (default: current directory)
- **-s, --size-opt** - Font size in points (default: 16)
- **--2bit** - Generate 2-bit grayscale (smoother but larger)
- **--all-sizes** - Generate all reader sizes (14, 16, 18pt)
- **--header** - Output C header instead of binary .epdfont
- **--thai** - Include Thai script (U+0E00-0E7F)
- **--additional-intervals** - Additional Unicode intervals as min,max (can be repeated)

#### Examples

```bash
# Convert with custom size
uv run scripts/fontconvert.py my-font -r Font.ttf --2bit -s 14 -o /tmp/fonts/

# Output directly to SD card
uv run scripts/fontconvert.py my-font -r Font.ttf --2bit -o /Volumes/SDCARD/config/fonts/

# Generate all sizes for reader font (14, 16, 18pt)
uv run scripts/fontconvert.py my-font -r Font.ttf --2bit --all-sizes -o /tmp/fonts/

# Include Thai script support
uv run scripts/fontconvert.py my-font -r NotoSansThai-Regular.ttf --2bit --thai -o /tmp/fonts/

# Generate C header for builtin fonts (original mode, outputs to stdout)
uv run scripts/fontconvert.py my_font 16 Font.ttf --2bit > my_font_16_2b.h
```

The script creates a font family directory structure:

```
my-font/
├── regular.epdfont
├── bold.epdfont
└── italic.epdfont
```

With `--all-sizes`, separate directories are created for each size:

```
my-font-14/
├── regular.epdfont
├── bold.epdfont
└── italic.epdfont
my-font-16/
├── ...
my-font-18/
├── ...
```

Copy the entire folder(s) to `/config/fonts/` on your SD card.

### Recommended Font Sizes

- **Reader font (Small setting):** 14pt
- **Reader font (Normal setting):** 16pt
- **Reader font (Large setting):** 18pt
- **UI font:** 14-16pt

### Using Custom Fonts in Themes

Once you've created your font files, reference them in your theme configuration:

```ini
[fonts]
ui_font = my-font
reader_font_small = my-font-14
reader_font_medium = my-font-16
reader_font_large = my-font-18
```

Each font family name must match a directory name under `/config/fonts/`. You can use the same font for all sizes, or different fonts for each size.

### Supported Characters

By default, the font converter includes:

- Basic Latin (ASCII) - letters, digits, punctuation
- Latin-1 Supplement - Western European accented characters
- Latin Extended-A/B - Eastern European languages
- Latin Extended Additional - Vietnamese characters
- General punctuation - smart quotes, dashes, ellipsis
- Common currency symbols
- Cyrillic characters
- Combining diacritical marks
- Math operators and arrows

Vietnamese fonts work with standard `.epdfont` format since they use Latin script with additional diacritics.

### Thai Fonts

Thai fonts can be generated using the `--thai` flag:

```bash
# Thai font with Thai script support
uv run scripts/fontconvert.py noto-sans-thai -r NotoSansThai-Regular.ttf --2bit --thai -o /tmp/fonts/
```

### CJK Fonts

The ESP32-C3 has limited RAM (~380KB), so CJK fonts require external `.bin` format which streams glyphs from SD card. Pre-converted CJK fonts are available in the `docs/examples/fonts/` directory.

### Fallback Behavior

If a custom font file is missing, corrupted, or exceeds size limits:

- The device automatically falls back to built-in fonts
- Console shows which font failed and why

**Size limits:**
- `.epdfont` files: max 512KB bitmap data
- `.bin` external fonts: max 32MB file size, max 64x64 pixel glyphs

Built-in fonts are always available:
- **Reader** - Reader font (3 sizes)
- **UI** - UI font
- **Small** - Small text

> **Note:** Custom font loading is optional. The device works perfectly with built-in fonts if no custom fonts are configured.

---

## SD Card Structure

Here's the complete SD card structure for customization:

```
/
├── config/
│   ├── calibre.ini
│   ├── themes/
│   │   ├── light.theme
│   │   ├── dark.theme
│   │   └── custom.theme
│   └── fonts/
│       ├── my-reader-font/
│       │   ├── regular.epdfont
│       │   ├── bold.epdfont
│       │   └── italic.epdfont
│       └── my-ui-font/
│           └── regular.epdfont
├── sleep.bmp              # Custom sleep image (optional)
└── sleep/                 # Multiple sleep images (optional)
    ├── image1.bmp
    └── image2.bmp
```

---

## Example Files

The repository includes example theme and font files in [`docs/examples/`](examples/):

**Themes:**
- **`light-noto-serif.theme`** - Light theme with Noto Serif reader fonts (Latin script)
- **`light-noto-sans.theme`** - Light theme with Noto Sans reader fonts
- **`light-pt-serif.theme`** - Light theme with PT Serif reader fonts
- **`light-literata.theme`** - Light theme with Literata reader fonts
- **`light-roboto.theme`** - Light theme with Roboto reader fonts
- **`light-opendyslexic.theme`** - Light theme with OpenDyslexic reader fonts
- **`light-thai.theme`** - Light theme with Noto Sans Thai fonts
- **`light-vietnamese.theme`** - Light theme with Noto Serif Vietnamese fonts
- **`light-cjk-external.theme`** - Light theme with CJK external (.bin) fonts

**Fonts:**
- **`fonts/noto-serif-*/`** - Noto Serif at 14pt, 16pt, 18pt (Latin script)
- **`fonts/noto-sans-*/`** - Noto Sans at 14pt, 16pt, 18pt
- **`fonts/pt-serif-*/`** - PT Serif at 14pt, 16pt, 18pt
- **`fonts/literata-*/`** - Literata at 14pt, 16pt, 18pt
- **`fonts/roboto-*/`** - Roboto at 14pt, 16pt, 18pt
- **`fonts/opendyslexic-*/`** - OpenDyslexic at 14pt, 16pt, 18pt
- **`fonts/noto-sans-thai-*/`** - Noto Sans Thai at 14pt, 16pt, 18pt
- **`fonts/noto-serif-vn-*/`** - Noto Serif Vietnamese at 14pt, 16pt, 18pt
- **`fonts/*.bin`** - CJK external fonts (Source Han Sans CN, KingHwaOldSong)

To use a theme:
1. Copy the `.theme` file to `/config/themes/` on your SD card
2. Copy the corresponding font folders to `/config/fonts/` on your SD card
3. Select the theme in **Settings > Reader > Theme**

### Font Attribution

The example fonts use:
- [Noto Serif](https://fonts.google.com/noto/specimen/Noto+Serif) from Google Fonts (SIL OFL)
- [Noto Sans Thai](https://fonts.google.com/noto/specimen/Noto+Sans+Thai) from Google Fonts (SIL OFL)
- [Source Han Sans CN](https://github.com/adobe-fonts/source-han-sans) from Adobe (SIL OFL)
- KingHwaOldSong (traditional Chinese font)

All fonts are licensed under the [SIL Open Font License (OFL)](https://openfontlicense.org/).
