#!/usr/bin/env python3
"""
Convert TTF/OTF fonts to Papyrix binary format (.epdfont)

Creates a font family directory with all style variants for use with Papyrix themes.

Usage:
    python3 convert_theme_fonts.py my-font -r Regular.ttf -b Bold.ttf -i Italic.ttf
    python3 convert_theme_fonts.py my-font -r Regular.ttf --size 16
    python3 convert_theme_fonts.py my-font -r Regular.ttf -o /path/to/sdcard/fonts/

Requirements:
    pip install freetype-py
"""

import argparse
import struct
import math
import sys
from pathlib import Path
from collections import namedtuple

try:
    import freetype
except ImportError:
    print("Error: freetype-py is required. Install with: pip install freetype-py", file=sys.stderr)
    sys.exit(1)

# Binary format constants
MAGIC = 0x46445045  # "EPDF" in little-endian
VERSION = 1

# Unicode intervals - base set (Latin, Cyrillic, punctuation)
INTERVALS_BASE = [
    # Basic Latin (ASCII)
    (0x0000, 0x007F),
    # Latin-1 Supplement
    (0x0080, 0x00FF),
    # Latin Extended-A
    (0x0100, 0x017F),
    # General Punctuation
    (0x2000, 0x206F),
    # Dashes, quotes, prime marks
    (0x2010, 0x203A),
    # Misc punctuation
    (0x2040, 0x205F),
    # Currency symbols
    (0x20A0, 0x20CF),
    # Combining Diacritical Marks
    (0x0300, 0x036F),
    # Cyrillic
    (0x0400, 0x04FF),
    # Math operators
    (0x2200, 0x22FF),
    # Arrows
    (0x2190, 0x21FF),
]

# CJK intervals - full set (~32,000 chars, results in large files ~4MB+)
INTERVALS_CJK_FULL = [
    # CJK Symbols and Punctuation
    (0x3000, 0x303F),
    # Hiragana (Japanese)
    (0x3040, 0x309F),
    # Katakana (Japanese)
    (0x30A0, 0x30FF),
    # CJK Unified Ideographs - FULL (20,992 chars)
    (0x4E00, 0x9FFF),
    # Hangul Jamo (Korean)
    (0x1100, 0x11FF),
    # Hangul Compatibility Jamo (Korean)
    (0x3130, 0x318F),
    # Hangul Syllables - FULL (11,172 chars)
    (0xAC00, 0xD7AF),
    # Halfwidth and Fullwidth Forms
    (0xFF00, 0xFFEF),
]

# CJK intervals - common subset (~3,500 chars, fits in 2MB limit)
# Covers ~99% of typical Chinese/Korean text
INTERVALS_CJK_COMMON = [
    # CJK Symbols and Punctuation
    (0x3000, 0x303F),
    # Hiragana (Japanese)
    (0x3040, 0x309F),
    # Katakana (Japanese)
    (0x30A0, 0x30FF),
    # CJK Unified Ideographs - Common subset (~3,500 most frequent chars)
    # This range contains the most commonly used Chinese characters
    (0x4E00, 0x5DFF),
    # Hangul Jamo (Korean)
    (0x1100, 0x11FF),
    # Hangul Compatibility Jamo (Korean)
    (0x3130, 0x318F),
    # Hangul Syllables - Common subset (~2,350 most frequent)
    (0xAC00, 0xB5FF),
    # Fullwidth ASCII and punctuation
    (0xFF00, 0xFF5F),
]

# Default: base + full CJK
INTERVALS = INTERVALS_BASE + INTERVALS_CJK_FULL

GlyphProps = namedtuple("GlyphProps", [
    "width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"
])


def norm_floor(val):
    return int(math.floor(val / 64))


def norm_ceil(val):
    return int(math.ceil(val / 64))


def _build_4bit_grayscale(bitmap):
    """Convert 8-bit bitmap buffer to packed 4-bit grayscale pixels."""
    pixels4g = []
    for y in range(bitmap.rows):
        for x in range(0, bitmap.width, 2):
            i = y * bitmap.width + x
            low = bitmap.buffer[i] >> 4
            high = (bitmap.buffer[i + 1] >> 4) << 4 if x + 1 < bitmap.width else 0
            pixels4g.append(low | high)
    return pixels4g


def _get_pixel_4bit(pixels4g, pitch, x, y):
    """Extract a single 4-bit pixel value from packed buffer."""
    if pitch == 0 or not pixels4g:
        return 0
    idx = y * pitch + (x // 2)
    if idx >= len(pixels4g):
        return 0
    byte = pixels4g[idx]
    return (byte >> ((x % 2) * 4)) & 0xF


def _quantize_pixel(value, is_2bit):
    """Quantize a 4-bit pixel to 2-bit or 1-bit."""
    if is_2bit:
        if value >= 12:
            return 3
        if value >= 8:
            return 2
        if value >= 4:
            return 1
        return 0
    return 1 if value >= 2 else 0


def _render_downsampled(bitmap, pixels4g, is_2bit):
    """Downsample 4-bit grayscale to 2-bit or 1-bit packed bitmap."""
    bits_per_pixel = 2 if is_2bit else 1
    pixels_per_byte = 8 // bits_per_pixel
    pitch = (bitmap.width + 1) // 2
    total_pixels = bitmap.width * bitmap.rows

    pixels = []
    px = 0
    for y in range(bitmap.rows):
        for x in range(bitmap.width):
            px = (px << bits_per_pixel) | _quantize_pixel(
                _get_pixel_4bit(pixels4g, pitch, x, y), is_2bit
            )
            pixel_idx = y * bitmap.width + x
            if pixel_idx % pixels_per_byte == pixels_per_byte - 1:
                pixels.append(px)
                px = 0

    remainder = total_pixels % pixels_per_byte
    if remainder != 0:
        px <<= (pixels_per_byte - remainder) * bits_per_pixel
        pixels.append(px)

    return bytes(pixels)


def load_glyph(font_stack, code_point):
    """Load a glyph from the font stack, trying each font in order."""
    for face in font_stack:
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
    return None


def validate_intervals(font_stack, intervals):
    """Filter intervals to only include code points present in font."""
    valid_intervals = []

    for i_start, i_end in intervals:
        start = i_start
        for code_point in range(i_start, i_end + 1):
            face = load_glyph(font_stack, code_point)
            if face is None:
                if start < code_point:
                    valid_intervals.append((start, code_point - 1))
                start = code_point + 1
        if start <= i_end:
            valid_intervals.append((start, i_end))

    return valid_intervals


def render_glyph(face, is_2bit):
    """Render a glyph to bitmap data."""
    bitmap = face.glyph.bitmap
    pixels4g = _build_4bit_grayscale(bitmap)
    return _render_downsampled(bitmap, pixels4g, is_2bit)


def convert_font(font_path: Path, output_path: Path, size: int, is_2bit: bool) -> bool:
    """Convert a single font file to .epdfont binary format."""
    if not font_path.exists():
        print(f"  Warning: Font file not found: {font_path}", file=sys.stderr)
        return False

    print(f"  Converting: {font_path.name} -> {output_path.name}")

    try:
        face = freetype.Face(str(font_path))
        font_stack = [face]
        face.set_char_size(size << 6, size << 6, 150, 150)

        # Validate intervals
        intervals = validate_intervals(font_stack, sorted(INTERVALS))
        if not intervals:
            print(f"  Error: No valid glyphs found", file=sys.stderr)
            return False

        # Render all glyphs
        all_glyphs = []
        total_bitmap_size = 0

        for i_start, i_end in intervals:
            for code_point in range(i_start, i_end + 1):
                loaded_face = load_glyph(font_stack, code_point)
                if loaded_face is None:
                    continue

                bitmap = loaded_face.glyph.bitmap
                pixel_data = render_glyph(loaded_face, is_2bit)

                glyph = GlyphProps(
                    width=bitmap.width,
                    height=bitmap.rows,
                    advance_x=norm_floor(loaded_face.glyph.advance.x),
                    left=loaded_face.glyph.bitmap_left,
                    top=loaded_face.glyph.bitmap_top,
                    data_length=len(pixel_data),
                    data_offset=total_bitmap_size,
                    code_point=code_point,
                )
                total_bitmap_size += len(pixel_data)
                all_glyphs.append((glyph, pixel_data))

        # Get font metrics from '|' character
        load_glyph(font_stack, ord('|'))
        advance_y = norm_ceil(face.size.height)
        ascender = norm_ceil(face.size.ascender)
        descender = norm_floor(face.size.descender)

        # Build binary file
        output_path.parent.mkdir(parents=True, exist_ok=True)

        with open(output_path, 'wb') as f:
            # Header: magic(4) + version(2) + flags(2) + reserved(8) = 16 bytes
            flags = 0x01 if is_2bit else 0x00
            f.write(struct.pack('<IHH8x', MAGIC, VERSION, flags))

            # Metrics: advanceY(1) + pad(1) + ascender(2) + descender(2) + counts(12) = 18 bytes
            f.write(struct.pack('<BBhhIII', advance_y & 0xFF, 0, ascender, descender,
                                len(intervals), len(all_glyphs), total_bitmap_size))

            # Intervals: first(4) + last(4) + offset(4) = 12 bytes each
            glyph_offset = 0
            for i_start, i_end in intervals:
                f.write(struct.pack('<III', i_start, i_end, glyph_offset))
                glyph_offset += i_end - i_start + 1

            # Glyphs: w(1) + h(1) + adv(1) + pad(1) + left(2) + top(2) + len(2) + off(4) = 14 bytes
            for glyph, _ in all_glyphs:
                f.write(struct.pack('<4BhhHI', glyph.width, glyph.height,
                                    glyph.advance_x & 0xFF, 0,
                                    glyph.left, glyph.top,
                                    glyph.data_length, glyph.data_offset))

            # Bitmap data
            for _, pixel_data in all_glyphs:
                f.write(pixel_data)

        print(f"  Created: {output_path} ({output_path.stat().st_size} bytes)")
        return True

    except Exception as e:
        print(f"  Error: {e}", file=sys.stderr)
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Convert TTF/OTF fonts to Papyrix binary format (.epdfont)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert a complete font family
  %(prog)s my-font -r MyFont-Regular.ttf -b MyFont-Bold.ttf -i MyFont-Italic.ttf

  # Convert only regular style
  %(prog)s my-font -r MyFont-Regular.ttf

  # Specify font size (default: 16)
  %(prog)s my-font -r MyFont-Regular.ttf --size 14

  # Output to SD card
  %(prog)s my-font -r MyFont-Regular.ttf -o /Volumes/SDCARD/config/fonts/

  # Use 2-bit grayscale for smoother rendering
  %(prog)s my-font -r MyFont-Regular.ttf --2bit

Output structure:
  <output>/
  └── <family-name>/
      ├── regular.epdfont
      ├── bold.epdfont
      └── italic.epdfont
        """,
    )

    parser.add_argument("family", help="Font family name (used as directory name)")
    parser.add_argument("-r", "--regular", required=True, help="Path to regular style TTF/OTF file")
    parser.add_argument("-b", "--bold", help="Path to bold style TTF/OTF file")
    parser.add_argument("-i", "--italic", help="Path to italic style TTF/OTF file")
    parser.add_argument("-o", "--output", default=".", help="Output directory (default: current directory)")
    parser.add_argument("-s", "--size", type=int, default=16, help="Font size in points (default: 16)")
    parser.add_argument("--2bit", dest="is_2bit", action="store_true", help="Generate 2-bit grayscale (smoother but larger)")
    parser.add_argument("--all-sizes", action="store_true", help="Generate all reader font sizes (14, 16, 18pt)")
    parser.add_argument("--cjk-common", action="store_true", help="Use reduced CJK character set (~3,500 chars) to fit in 2MB limit")

    args = parser.parse_args()

    # Select interval set based on --cjk-common flag
    global INTERVALS
    if args.cjk_common:
        INTERVALS = INTERVALS_BASE + INTERVALS_CJK_COMMON
        print("Using reduced CJK character set (~3,500 chars)")

    output_base = Path(args.output)

    print(f"Converting font family: {args.family}")
    print(f"Output directory: {output_base}")
    print(f"Font size: {args.size}pt")
    if args.is_2bit:
        print("Mode: 2-bit grayscale")
    print()

    styles = [
        ("regular", args.regular),
        ("bold", args.bold),
        ("italic", args.italic),
    ]

    sizes = [14, 16, 18] if args.all_sizes else [args.size]
    success_count = 0
    total_count = 0

    for size in sizes:
        family_dir = output_base / (f"{args.family}-{size}" if args.all_sizes else args.family)
        if args.all_sizes:
            print(f"Size: {size}pt -> {family_dir.name}/")

        for style_name, font_path in styles:
            if font_path is None:
                continue

            total_count += 1
            output_file = family_dir / f"{style_name}.epdfont"

            if convert_font(Path(font_path), output_file, size, args.is_2bit):
                success_count += 1

    print()
    print(f"Converted {success_count}/{total_count} fonts")

    if success_count > 0:
        print()
        print("To use this font in your theme, add to your .theme file:")
        print()
        print("[fonts]")
        for size_name, size in [("small", 14), ("medium", 16), ("large", 18)]:
            font_name = f"{args.family}-{size}" if args.all_sizes else args.family
            print(f"reader_font_{size_name} = {font_name}")
        print()
        print(f"Then copy the font folder(s) to /config/fonts/ on your SD card.")

    return 0 if success_count == total_count else 1


if __name__ == "__main__":
    sys.exit(main())
