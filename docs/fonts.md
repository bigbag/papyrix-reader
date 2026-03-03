# Fonts

Papyrix Reader supports custom fonts for reading. Fonts are converted to a proprietary `.epdfont` format optimized for e-paper displays.

## How Fonts Work

### Streaming Font System

Custom fonts use a memory-efficient **streaming** system that loads glyph bitmaps on-demand from the SD card rather than keeping the entire font in RAM. This saves approximately **50KB of RAM per font**.

- **Glyph metadata** (character metrics, positions) is loaded into RAM
- **Glyph bitmaps** (the actual pixels) are streamed from SD as needed
- An **LRU cache** keeps recently-used glyphs in memory for fast access
- Typical RAM usage: ~25KB per font (vs ~70KB for fully-loaded fonts)

This is transparent to users - fonts work the same way, just more efficiently.

### Supported Styles

Custom `.epdfont` fonts support **regular** and **bold** styles:

- **Regular** (`regular.epdfont`) - Required. Loaded when the book is opened.
- **Bold** (`bold.epdfont`) - Optional. Loaded on demand when bold text is first encountered, saving ~42KB of RAM for books that don't use bold.
- **Italic** text renders using the regular variant. When using built-in fonts, the native italic is used.

### Fallback Behavior

Papyrix ensures you can always read your books, even if a custom font fails:

1. **Font load failure** → Built-in font is used automatically
2. **Individual glyph failure** → Character is skipped gracefully (no crash)
3. **SD card read error** → Affected characters skipped, reading continues

If you notice missing characters, try switching to a different font in Settings. The built-in font is always available as a reliable fallback.

### Built-in Font Coverage

The built-in fonts include native support for:

- **Latin** - Western and Eastern European languages, including Vietnamese diacritics
- **Cyrillic** - Russian, Ukrainian, and other Cyrillic-script languages
- **Thai** - Full Thai script with proper mark positioning
- **Greek** - Modern Greek alphabet
- **Arabic** - Arabic script with contextual shaping and RTL layout

No custom fonts are needed for these scripts - they work out of the box.

## Font Samples

### PT Serif

A versatile serif typeface with a contemporary feel. Excellent for body text with good readability on e-paper displays.

- **Styles**: Regular, Bold
- **License**: OFL (Open Font License)

![PT Serif Sample](examples/images/pt-serif-sample.png)

### Bookerly

Amazon's custom font designed specifically for e-readers. Optimized for readability on low-resolution displays.

- **Styles**: Regular, Bold, Italic
- **License**: Proprietary (Amazon)

![Bookerly Sample](examples/images/bookerly-sample.png)

### Literata

A contemporary serif typeface designed for long-form reading. Features excellent legibility and a warm, inviting character.

- **Styles**: Regular, Bold, Italic
- **License**: OFL (Open Font License)

![Literata Sample](examples/images/literata-sample.png)

### Noto Serif

A classic serif font from Google's Noto family. Excellent readability with extensive language support.

- **Styles**: Regular
- **License**: OFL (Open Font License)

![Noto Serif Sample](examples/images/noto-serif-sample.png)

### Noto Sans

A clean sans-serif font from Google's Noto family. Modern appearance with wide language coverage.

- **Styles**: Regular, Italic (Variable font)
- **License**: OFL (Open Font License)

![Noto Sans Sample](examples/images/noto-sans-sample.png)

### Roboto

Google's signature font family. Clean, modern design ideal for UI and reading.

- **Styles**: Regular, Italic (Variable font)
- **License**: Apache 2.0

![Roboto Sample](examples/images/roboto-sample.png)

### Ubuntu

The Ubuntu font family has a contemporary style and is designed for screen reading. Warm and friendly character.

- **Styles**: Regular, Bold, Italic
- **License**: Ubuntu Font License

![Ubuntu Sample](examples/images/ubuntu-sample.png)

### OpenDyslexic

A typeface designed to increase readability for readers with dyslexia. Features weighted bottoms to prevent letter rotation.

- **Styles**: Regular, Bold, Italic
- **License**: OFL (Open Font License)

![OpenDyslexic Sample](examples/images/opendyslexic-sample.png)


### Noto Sans Arabic

A sans-serif font with complete Arabic script support including contextual shaping and ligatures.

- **Styles**: Regular, Bold
- **Theme**: `light-arabic.theme`
- **License**: OFL (Open Font License)

### CJK Fonts (Chinese/Japanese/Korean)

For CJK texts, Papyrix uses external `.bin` format fonts that are streamed from the SD card due to their large size. The `.bin` format uses direct codepoint indexing (1-bit bitmap, MSB first) for the full BMP range (U+0000-U+FFEF).

#### Quick Start with gen_cjk_theme.sh

The easiest way to create CJK fonts is with `gen_cjk_theme.sh`, which generates a `.bin` font and matching `.theme` file:

```bash
# CJK font renders everything (Latin + CJK)
./scripts/gen_cjk_theme.sh --cjk MyCJKFont.otf --latin-mode cjk --name my-cjk-font

# Separate Latin font for ANSI, CJK font for ideographs
./scripts/gen_cjk_theme.sh --cjk MyCJKFont.ttf --latin MySerifFont.ttf --name my-mixed-font

# CJK only, builtin system font handles Latin
./scripts/gen_cjk_theme.sh --cjk MyCJKFont.otf --latin-mode system --name my-cjk-font
```

#### Manual Conversion with fontconvert-bin

For single-size conversion or custom options, use the Go converter (fast, no native dependencies):

```bash
# Build the Go converter (one-time)
make fontconvert-bin

# Basic CJK conversion
tools/fontconvert-bin/build/fontconvert-bin MyCJKFont.ttf --pixel-height 34 --name my-cjk -o /tmp/

# With separate Latin font
tools/fontconvert-bin/build/fontconvert-bin MyCJKFont.ttf --pixel-height 34 --latin-font Latin.ttf --name mixed -o /tmp/

# CJK only (builtin handles Latin)
tools/fontconvert-bin/build/fontconvert-bin MyCJKFont.ttf --pixel-height 30 --cjk-only --name cjk -o /tmp/
```

Alternative: Python converter (slower, no build step):

```bash
uv run scripts/fontconvert_bin.py MyCJKFont.ttf --pixel-height 34 --name my-cjk -o /tmp/
```

Options:
- `--size N` — Font size in points (default: 20)
- `--pixel-height N` — Pixel height (overrides --size/--dpi)
- `--name NAME` — Font name for output
- `--latin-font FILE` — Separate font for Latin (U+0000-U+024F)
- `--latin-size N` — Pixel height for Latin font
- `--cjk-only` — Zero Latin range, empty slots fall through to builtin
- `-o DIR` — Output directory
- `--dpi N` — Rendering DPI (default: 150)
- `--max-codepoint N` — Highest codepoint (default: 0xFFEF)

#### CJK Font Format Details

- **Direct codepoint indexing**: `offset = codepoint * bytesPerChar`
- **1-bit bitmap**, MSB first, `bytesPerRow = (W+7)//8`
- **Cell dimensions**: auto-calculated from sample CJK chars, capped at 64x64
- **Cell size constraint**: max 512 bytes/glyph (64x64 at 1-bit)
- **Filename encodes metadata**: `{name}_{size}_{W}x{H}.bin` or `{name}_px{height}_{W}x{H}.bin`
- **Glyph filtering**: detects Latin glyph reuse (font mapping bugs), skips narrow ideographs (<20% cell width)

#### Latin Handling Modes

| Mode | Description | When to use |
|------|-------------|-------------|
| `cjk` | CJK font renders Latin + CJK | Font has good Latin glyphs |
| `include` | Separate Latin font for U+0000-U+024F | Want different Latin/CJK fonts |
| `system` | Builtin font handles Latin, `.bin` for CJK only | Prefer builtin Latin rendering |

## Converting and Installing Fonts

See the [Customization Guide](customization.md#custom-fonts) for detailed instructions on converting TTF/OTF fonts to `.epdfont` format and installing them on your device.

## Font Sources

- [Google Fonts](https://fonts.google.com/) - Free, open-source fonts
- [Noto Fonts](https://fonts.google.com/noto) - Extensive language coverage
