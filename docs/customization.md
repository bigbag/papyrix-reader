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
5. Restart the device and select your theme in **Settings > Theme**

### Theme File Format

Theme files use a simple INI format:

```ini
# Papyrix Theme Configuration
# Edit values and restart device to apply

[colors]
inverted_mode = false     # true = dark mode, false = light mode
background = white        # white or black

[selection]
fill_color = black        # Selection highlight color
text_color = white        # Text on selection

[text]
primary_color = black     # Normal text color
secondary_color = black   # Secondary/dimmed text

[layout]
margin_top = 9            # Top margin in pixels
margin_side = 3           # Side margin in pixels
item_height = 30          # Menu item height
item_spacing = 0          # Space between menu items
front_buttons = bclr      # bclr or lrbc (see below)

[fonts]
ui_font =                 # UI font family (empty = builtin)
reader_font =             # Reader font family (empty = builtin)
```

### Configuration Options

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
  - Affects file browser, TOC lists, and chapter selection navigation (including long-press page skip)
- **item_spacing** - Vertical space between items in pixels
  - Default: `0`
- **front_buttons** - Front button mapping
  - Values: `bclr` (Back, Confirm, Left, Right) or `lrbc` (Left, Right, Back, Confirm)

#### Fonts Section

- **ui_font** - Custom UI font family name
  - Leave empty to use builtin font
- **reader_font** - Custom reader font family name
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
reader_font =
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
reader_font =
```

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
│   ├── italic.epdfont
│   └── bold_italic.epdfont
└── another-font/
    └── regular.epdfont
```

Each font family is a subdirectory containing one or more style variants.

### Converting Fonts

To create `.epdfont` files from TTF/OTF fonts, use the `convert_theme_fonts.py` script included in the firmware source code (`scripts/convert_theme_fonts.py`).

#### Requirements

- Python 3
- freetype-py library: `pip install freetype-py`

#### Basic Usage

Convert a complete font family:

```bash
python3 scripts/convert_theme_fonts.py my-font \
    -r MyFont-Regular.ttf \
    -b MyFont-Bold.ttf \
    -i MyFont-Italic.ttf \
    -bi MyFont-BoldItalic.ttf
```

Convert only the regular style:

```bash
python3 scripts/convert_theme_fonts.py my-font -r MyFont-Regular.ttf
```

#### Options

- **-r, --regular** - Path to regular style font (required)
- **-b, --bold** - Path to bold style font
- **-i, --italic** - Path to italic style font
- **-bi, --bold-italic** - Path to bold-italic style font
- **-o, --output** - Output directory (default: current directory)
- **-s, --size** - Font size in points (default: 16)
- **--2bit** - Generate 2-bit grayscale (smoother but larger)
- **--all-sizes** - Generate all reader sizes (14, 16, 18pt)

#### Examples

```bash
# Convert with custom size
python3 scripts/convert_theme_fonts.py my-font -r Font.ttf --size 14

# Output directly to SD card
python3 scripts/convert_theme_fonts.py my-font -r Font.ttf -o /Volumes/SDCARD/fonts/

# Generate all sizes for reader font
python3 scripts/convert_theme_fonts.py my-font -r Font.ttf --all-sizes

# Use 2-bit grayscale for smoother rendering
python3 scripts/convert_theme_fonts.py my-font -r Font.ttf --2bit
```

The script creates a font family directory structure:

```
my-font/
├── regular.epdfont
├── bold.epdfont
├── italic.epdfont
└── bold_italic.epdfont
```

Copy the entire folder to `/config/fonts/` on your SD card.

#### Recommended Font Sizes

- **Reader font (Small setting):** 14pt
- **Reader font (Normal setting):** 16pt
- **Reader font (Large setting):** 18pt
- **UI font:** 14-16pt

#### Advanced: Using fontconvert.py Directly

For more control, use the low-level `fontconvert.py` script:

```bash
python3 lib/EpdFont/scripts/fontconvert.py FontName 16 font.ttf --binary -o output.epdfont
```

Additional options:
- **--additional-intervals min,max** - Add extra Unicode code point ranges (e.g., `--additional-intervals 0x0400,0x04FF` for Cyrillic)

### Using Custom Fonts in Themes

Once you've created your font files, reference them in your theme configuration:

```ini
[fonts]
ui_font = my-font
reader_font = my-font
```

The font family name must match the directory name under `/config/fonts/`.

### Supported Characters

By default, the font converter includes:

- Basic Latin (ASCII) - letters, digits, punctuation
- Latin-1 Supplement - Western European accented characters
- Latin Extended-A - Eastern European languages
- General punctuation - smart quotes, dashes, ellipsis
- Common currency symbols

Use `--additional-intervals` to add more Unicode ranges if needed. For example, to add Cyrillic:

```bash
python3 fontconvert.py MyFont 16 font.ttf --binary --additional-intervals 0x0400,0x04FF -o output.epdfont
```

### Fallback Behavior

If a custom font file is missing or corrupted:

- The device automatically falls back to built-in fonts
- Built-in fonts are always available:
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
│   ├── opds.ini
│   ├── themes/
│   │   ├── light.theme
│   │   ├── dark.theme
│   │   └── custom.theme
│   └── fonts/
│       ├── my-reader-font/
│       │   ├── regular.epdfont
│       │   ├── bold.epdfont
│       │   ├── italic.epdfont
│       │   └── bold_italic.epdfont
│       └── my-ui-font/
│           └── regular.epdfont
├── sleep.bmp              # Custom sleep image (optional)
└── sleep/                 # Multiple sleep images (optional)
    ├── image1.bmp
    └── image2.bmp
```
