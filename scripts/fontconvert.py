#!/usr/bin/env python3
# /// script
# dependencies = ["freetype-py"]
# ///
"""
Font converter for Papyrix e-reader.

Converts TTF/OTF fonts to:
- C header files (.h) for builtin fonts
- Binary .epdfont files for SD card fonts

Based on epdiy fontconvert: https://github.com/vroland/epdiy
"""
import argparse
import math
import struct
import sys
from collections import namedtuple
from pathlib import Path

import freetype

# Unicode intervals for multi-language support
# Must not overlap and should be in ascending order for merging

INTERVALS_BASE = [
    (0x0000, 0x007F),  # Basic Latin (ASCII)
    (0x0080, 0x00FF),  # Latin-1 Supplement
    (0x0100, 0x017F),  # Latin Extended-A
    (0x0180, 0x024F),  # Latin Extended-B (includes Vietnamese Ơ, Ư)
    (0x0250, 0x02AF),  # IPA Extensions
    (0x0300, 0x036F),  # Combining Diacritical Marks
    (0x0400, 0x04FF),  # Cyrillic
    (0x1E00, 0x1EFF),  # Latin Extended Additional (Vietnamese tones)
    (0x2000, 0x206F),  # General Punctuation
    (0x2070, 0x209F),  # Superscripts and Subscripts
    (0x20A0, 0x20CF),  # Currency Symbols
    (0x2190, 0x21FF),  # Arrows
    (0x2200, 0x22FF),  # Mathematical Operators
    (0xFFFD, 0xFFFD),  # Replacement Character
]

INTERVALS_THAI = [
    (0x0E00, 0x0E7F),  # Thai
]

GlyphProps = namedtuple(
    "GlyphProps",
    ["width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"],
)


def norm_floor(val):
    return int(math.floor(val / (1 << 6)))


def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))


def chunks(lst, n):
    for i in range(0, len(lst), n):
        yield lst[i : i + n]


def merge_intervals(intervals):
    """Merge overlapping or adjacent intervals."""
    if not intervals:
        return []
    sorted_intervals = sorted(intervals)
    merged = [list(sorted_intervals[0])]
    for i_start, i_end in sorted_intervals[1:]:
        if i_start <= merged[-1][1] + 1:
            merged[-1][1] = max(merged[-1][1], i_end)
        else:
            merged.append([i_start, i_end])
    return [tuple(x) for x in merged]


def load_glyph(font_stack, code_point):
    """Load a glyph from the font stack, returning the face that has it."""
    for face in font_stack:
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
    return None


def validate_intervals(font_stack, intervals):
    """Filter intervals to only include code points present in the font."""
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


def render_glyph_2bit(bitmap):
    """Render a FreeType bitmap to 2-bit grayscale packed format."""
    # Build 4-bit grayscale from 8-bit FreeType buffer
    pixels4g = []
    px = 0
    for i, v in enumerate(bitmap.buffer):
        x = i % bitmap.width
        if x % 2 == 0:
            px = v >> 4
        else:
            px = px | (v & 0xF0)
            pixels4g.append(px)
            px = 0
        if x == bitmap.width - 1 and bitmap.width % 2 > 0:
            pixels4g.append(px)
            px = 0

    # Downsample to 2-bit: 0-3 white, 4-7 light, 8-11 dark, 12-15 black
    pixels2b = []
    px = 0
    pitch = (bitmap.width // 2) + (bitmap.width % 2)
    for y in range(bitmap.rows):
        for x in range(bitmap.width):
            px = px << 2
            bm = pixels4g[y * pitch + (x // 2)]
            bm = (bm >> ((x % 2) * 4)) & 0xF
            if bm >= 12:
                px += 3
            elif bm >= 8:
                px += 2
            elif bm >= 4:
                px += 1
            if (y * bitmap.width + x) % 4 == 3:
                pixels2b.append(px)
                px = 0
    if (bitmap.width * bitmap.rows) % 4 != 0:
        px = px << (4 - (bitmap.width * bitmap.rows) % 4) * 2
        pixels2b.append(px)

    return bytes(pixels2b)


def render_glyph_1bit(bitmap):
    """Render a FreeType bitmap to 1-bit packed format."""
    # Build 4-bit grayscale from 8-bit FreeType buffer
    pixels4g = []
    px = 0
    for i, v in enumerate(bitmap.buffer):
        x = i % bitmap.width
        if x % 2 == 0:
            px = v >> 4
        else:
            px = px | (v & 0xF0)
            pixels4g.append(px)
            px = 0
        if x == bitmap.width - 1 and bitmap.width % 2 > 0:
            pixels4g.append(px)
            px = 0

    # Downsample to 1-bit - treat any 2+ as black
    pixelsbw = []
    px = 0
    pitch = (bitmap.width // 2) + (bitmap.width % 2)
    for y in range(bitmap.rows):
        for x in range(bitmap.width):
            px = px << 1
            bm = pixels4g[y * pitch + (x // 2)]
            px += 1 if ((x & 1) == 0 and bm & 0xE > 0) or ((x & 1) == 1 and bm & 0xE0 > 0) else 0
            if (y * bitmap.width + x) % 8 == 7:
                pixelsbw.append(px)
                px = 0
    if (bitmap.width * bitmap.rows) % 8 != 0:
        px = px << (8 - (bitmap.width * bitmap.rows) % 8)
        pixelsbw.append(px)

    return bytes(pixelsbw)


def convert_font(font_paths, size, intervals, is_2bit):
    """Convert font files to glyph data."""
    font_stack = [freetype.Face(str(p)) for p in font_paths]
    for face in font_stack:
        face.set_char_size(size << 6, size << 6, 150, 150)

    merged_intervals = merge_intervals(intervals)
    valid_intervals = validate_intervals(font_stack, merged_intervals)

    if not valid_intervals:
        print("Error: No valid glyphs found in font", file=sys.stderr)
        return None

    total_size = 0
    all_glyphs = []

    for i_start, i_end in valid_intervals:
        for code_point in range(i_start, i_end + 1):
            face = load_glyph(font_stack, code_point)
            if face is None:
                continue
            bitmap = face.glyph.bitmap

            if is_2bit:
                packed = render_glyph_2bit(bitmap)
            else:
                packed = render_glyph_1bit(bitmap)

            glyph = GlyphProps(
                width=bitmap.width,
                height=bitmap.rows,
                advance_x=norm_floor(face.glyph.advance.x),
                left=face.glyph.bitmap_left,
                top=face.glyph.bitmap_top,
                data_length=len(packed),
                data_offset=total_size,
                code_point=code_point,
            )
            total_size += len(packed)
            all_glyphs.append((glyph, packed))

    # Get metrics from pipe character (good heuristic for descender)
    face = load_glyph(font_stack, ord("|"))
    metrics = {
        "advance_y": norm_ceil(face.size.height),
        "ascender": norm_ceil(face.size.ascender),
        "descender": norm_floor(face.size.descender),
    }

    return {
        "glyphs": all_glyphs,
        "intervals": valid_intervals,
        "metrics": metrics,
        "is_2bit": is_2bit,
    }


def write_header(output_path, font_name, data, cmd_line):
    """Write font data as C header file."""
    glyphs = data["glyphs"]
    intervals = data["intervals"]
    metrics = data["metrics"]
    is_2bit = data["is_2bit"]

    glyph_data = []
    glyph_props = []
    for props, packed in glyphs:
        glyph_data.extend(packed)
        glyph_props.append(props)

    lines = [
        "/**",
        " * generated by fontconvert.py",
        f" * name: {font_name}",
        f" * mode: {'2-bit' if is_2bit else '1-bit'}",
        " */",
        "#pragma once",
        '#include "EpdFontData.h"',
        "",
        f"static const uint8_t PROGMEM {font_name}Bitmaps[{len(glyph_data)}] = {{",
    ]

    for chunk in chunks(glyph_data, 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in chunk))
    lines.append("};")
    lines.append("")

    lines.append(f"static const EpdGlyph PROGMEM {font_name}Glyphs[] = {{")
    for g in glyph_props:
        char_comment = ""
        if g.code_point >= 0x20 and g.code_point != 92:
            try:
                char_comment = f" // {chr(g.code_point)}"
            except (ValueError, UnicodeEncodeError):
                pass
        elif g.code_point == 92:
            char_comment = " // <backslash>"
        lines.append(
            "    { "
            + ", ".join(str(a) for a in list(g[:-1]))
            + " },"
            + char_comment
        )
    lines.append("};")
    lines.append("")

    lines.append(f"static const EpdUnicodeInterval PROGMEM {font_name}Intervals[] = {{")
    offset = 0
    for i_start, i_end in intervals:
        lines.append(f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
        offset += i_end - i_start + 1
    lines.append("};")
    lines.append("")

    lines.append(f"static const EpdFontData {font_name} = {{")
    lines.append(f"    {font_name}Bitmaps,")
    lines.append(f"    {font_name}Glyphs,")
    lines.append(f"    {font_name}Intervals,")
    lines.append(f"    {len(intervals)},")
    lines.append(f"    {metrics['advance_y']},")
    lines.append(f"    {metrics['ascender']},")
    lines.append(f"    {metrics['descender']},")
    lines.append(f"    {'true' if is_2bit else 'false'},")
    lines.append("};")

    output_path.write_text("\n".join(lines) + "\n")
    print(f"Created: {output_path} ({len(glyph_data)} bytes bitmap, {len(glyph_props)} glyphs)")


def write_epdfont(output_path, data):
    """
    Write font data as binary .epdfont file.

    Binary format (see EpdFontLoader.h):
      Header (16 bytes):
        - Magic: "EPDF" (4 bytes, little-endian 0x46445045)
        - Version: uint16_t (2 bytes)
        - Flags: uint16_t (2 bytes, bit 0 = is2Bit)
        - Reserved: 8 bytes

      Metrics (18 bytes):
        - advanceY: uint8_t
        - padding: uint8_t
        - ascender: int16_t
        - descender: int16_t
        - intervalCount: uint32_t
        - glyphCount: uint32_t
        - bitmapSize: uint32_t

      Intervals: intervalCount * 12 bytes each
        - first: uint32_t
        - last: uint32_t
        - offset: uint32_t

      Glyphs: glyphCount * 14 bytes each
        - width: uint8_t
        - height: uint8_t
        - advanceX: uint8_t
        - padding: uint8_t
        - left: int16_t
        - top: int16_t
        - dataLength: uint16_t
        - dataOffset: uint32_t

      Bitmap data: concatenated glyph bitmaps
    """
    MAGIC = 0x46445045
    VERSION = 1

    glyphs = data["glyphs"]
    intervals = data["intervals"]
    metrics = data["metrics"]
    is_2bit = data["is_2bit"]

    glyph_props = [g[0] for g in glyphs]
    bitmap_data = b"".join(g[1] for g in glyphs)
    bitmap_size = len(bitmap_data)

    # Calculate sizes
    header_size = 16
    metrics_size = 18
    intervals_size = len(intervals) * 12
    glyphs_size = len(glyph_props) * 14
    total_size = header_size + metrics_size + intervals_size + glyphs_size + bitmap_size

    buf = bytearray(total_size)
    offset = 0

    # Header
    struct.pack_into("<I", buf, offset, MAGIC)
    offset += 4
    struct.pack_into("<H", buf, offset, VERSION)
    offset += 2
    struct.pack_into("<H", buf, offset, 0x01 if is_2bit else 0x00)
    offset += 2
    offset += 8  # reserved

    # Metrics
    buf[offset] = metrics["advance_y"] & 0xFF
    offset += 1
    buf[offset] = 0  # padding
    offset += 1
    struct.pack_into("<h", buf, offset, metrics["ascender"])
    offset += 2
    struct.pack_into("<h", buf, offset, metrics["descender"])
    offset += 2
    struct.pack_into("<I", buf, offset, len(intervals))
    offset += 4
    struct.pack_into("<I", buf, offset, len(glyph_props))
    offset += 4
    struct.pack_into("<I", buf, offset, bitmap_size)
    offset += 4

    # Intervals
    glyph_offset = 0
    for i_start, i_end in intervals:
        struct.pack_into("<I", buf, offset, i_start)
        offset += 4
        struct.pack_into("<I", buf, offset, i_end)
        offset += 4
        struct.pack_into("<I", buf, offset, glyph_offset)
        offset += 4
        glyph_offset += i_end - i_start + 1

    # Glyphs
    for g in glyph_props:
        buf[offset] = g.width & 0xFF
        offset += 1
        buf[offset] = g.height & 0xFF
        offset += 1
        buf[offset] = g.advance_x & 0xFF
        offset += 1
        buf[offset] = 0  # padding
        offset += 1
        struct.pack_into("<h", buf, offset, g.left)
        offset += 2
        struct.pack_into("<h", buf, offset, g.top)
        offset += 2
        struct.pack_into("<H", buf, offset, g.data_length)
        offset += 2
        struct.pack_into("<I", buf, offset, g.data_offset)
        offset += 4

    # Bitmap data
    buf[offset : offset + bitmap_size] = bitmap_data

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(buf)
    print(f"Created: {output_path} ({total_size} bytes, {len(glyph_props)} glyphs)")


def main():
    parser = argparse.ArgumentParser(
        description="Convert TTF/OTF fonts to Papyrix format (.epdfont or C header)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Generate C header (original behavior)
  %(prog)s my_font 16 font.ttf --2bit

  # Generate binary .epdfont file
  %(prog)s my-font -r Regular.ttf -b Bold.ttf --size 16 --2bit -o /tmp/fonts/

  # Generate all sizes with Thai support
  %(prog)s my-font -r Regular.ttf --all-sizes --thai -o /tmp/fonts/
""",
    )

    # Original positional arguments for header mode
    parser.add_argument("name", help="Font name (for header) or family name (for binary)")
    parser.add_argument(
        "size",
        nargs="?",
        type=int,
        help="Font size (required for header mode, use --size for binary mode)",
    )
    parser.add_argument(
        "fontstack",
        nargs="*",
        help="Font files for header mode (fallback order)",
    )

    # New-style arguments for binary mode
    parser.add_argument("-r", "--regular", help="Regular style font file")
    parser.add_argument("-b", "--bold", help="Bold style font file")
    parser.add_argument("-i", "--italic", help="Italic style font file")
    parser.add_argument("-o", "--output", default=".", help="Output directory (default: current)")
    parser.add_argument("-s", "--size-opt", type=int, help="Font size in points")
    parser.add_argument(
        "--2bit",
        dest="is_2bit",
        action="store_true",
        help="Generate 2-bit grayscale (smoother but larger)",
    )
    parser.add_argument(
        "--header",
        action="store_true",
        help="Output C header instead of binary .epdfont",
    )
    parser.add_argument(
        "--thai",
        action="store_true",
        help="Include Thai script (U+0E00-0E7F)",
    )
    parser.add_argument(
        "--all-sizes",
        action="store_true",
        help="Generate 14, 16, 18pt sizes",
    )
    parser.add_argument(
        "--additional-intervals",
        dest="additional_intervals",
        action="append",
        help="Additional code point intervals as min,max (can be repeated)",
    )

    args = parser.parse_args()

    # Determine mode: header (original) or binary (new)
    if args.regular:
        # New binary mode with -r flag
        intervals = list(INTERVALS_BASE)
        if args.thai:
            intervals.extend(INTERVALS_THAI)
        if args.additional_intervals:
            for interval_str in args.additional_intervals:
                parts = [int(n, base=0) for n in interval_str.split(",")]
                intervals.append(tuple(parts))

        styles = []
        if args.regular:
            styles.append(("regular", args.regular))
        if args.bold:
            styles.append(("bold", args.bold))
        if args.italic:
            styles.append(("italic", args.italic))

        base_size = args.size_opt or args.size or 16
        sizes = [12, 14, 16, 18] if args.all_sizes else [base_size]
        output_base = Path(args.output)
        family_name = args.name

        print(f"Converting font family: {family_name}")
        print(f"Output directory: {output_base}")
        print(f"Mode: {'2-bit' if args.is_2bit else '1-bit'}")
        if args.thai:
            print("Including Thai script")
        print()

        for size in sizes:
            if args.all_sizes:
                family_dir = output_base / f"{family_name}-{size}"
            else:
                family_dir = output_base / family_name

            for style_name, font_path in styles:
                font_path = Path(font_path)
                if not font_path.exists():
                    print(f"Warning: Font file not found: {font_path}", file=sys.stderr)
                    continue

                print(f"Converting: {font_path.name} ({size}pt {style_name})...")
                data = convert_font([font_path], size, intervals, args.is_2bit)
                if data is None:
                    continue

                if args.header:
                    header_name = f"{family_name.replace('-', '_')}_{style_name}"
                    if args.all_sizes:
                        header_name += f"_{size}"
                    output_file = output_base / f"{header_name}_2b.h"
                    write_header(output_file, header_name, data, " ".join(sys.argv))
                else:
                    output_file = family_dir / f"{style_name}.epdfont"
                    write_epdfont(output_file, data)

        print()
        print("Done! Copy font folder(s) to /config/fonts/ on your SD card.")

    elif args.fontstack:
        # Original header-only mode (backward compatible)
        if not args.size:
            parser.error("Font size is required in header mode")

        intervals = list(INTERVALS_BASE)
        if args.thai:
            intervals.extend(INTERVALS_THAI)
        if args.additional_intervals:
            for interval_str in args.additional_intervals:
                parts = [int(n, base=0) for n in interval_str.split(",")]
                intervals.append(tuple(parts))

        font_paths = [Path(f) for f in args.fontstack]
        for p in font_paths:
            if not p.exists():
                print(f"Error: Font file not found: {p}", file=sys.stderr)
                sys.exit(1)

        data = convert_font(font_paths, args.size, intervals, args.is_2bit)
        if data is None:
            sys.exit(1)

        # Output to stdout (original behavior)
        glyphs = data["glyphs"]
        intervals = data["intervals"]
        metrics = data["metrics"]
        is_2bit = data["is_2bit"]
        font_name = args.name

        glyph_data = []
        glyph_props = []
        for props, packed in glyphs:
            glyph_data.extend(packed)
            glyph_props.append(props)

        print(f"""/**
 * generated by fontconvert.py
 * name: {font_name}
 * size: {args.size}
 * mode: {'2-bit' if is_2bit else '1-bit'}
 */
#pragma once
#include "EpdFontData.h"
""")

        print(f"static const uint8_t PROGMEM {font_name}Bitmaps[{len(glyph_data)}] = {{")
        for chunk in chunks(glyph_data, 16):
            print("    " + " ".join(f"0x{b:02X}," for b in chunk))
        print("};")
        print()

        print(f"static const EpdGlyph PROGMEM {font_name}Glyphs[] = {{")
        for g in glyph_props:
            char_comment = ""
            if g.code_point >= 0x20 and g.code_point != 92:
                try:
                    char_comment = f" // {chr(g.code_point)}"
                except (ValueError, UnicodeEncodeError):
                    pass
            elif g.code_point == 92:
                char_comment = " // <backslash>"
            print(
                "    { "
                + ", ".join(str(a) for a in list(g[:-1]))
                + " },"
                + char_comment
            )
        print("};")
        print()

        print(f"static const EpdUnicodeInterval PROGMEM {font_name}Intervals[] = {{")
        offset = 0
        for i_start, i_end in intervals:
            print(f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
            offset += i_end - i_start + 1
        print("};")
        print()

        print(f"static const EpdFontData {font_name} = {{")
        print(f"    {font_name}Bitmaps,")
        print(f"    {font_name}Glyphs,")
        print(f"    {font_name}Intervals,")
        print(f"    {len(intervals)},")
        print(f"    {metrics['advance_y']},")
        print(f"    {metrics['ascender']},")
        print(f"    {metrics['descender']},")
        print(f"    {'true' if is_2bit else 'false'},")
        print("};")

    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
