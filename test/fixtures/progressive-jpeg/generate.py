#!/usr/bin/env python3
"""Generate JPEG fixtures from local patterns and libjpeg reference pixels."""

import subprocess
import struct
import zlib
import tempfile
from pathlib import Path

DESTINATION = Path(__file__).resolve().parent


def encode(source, name, options, reference=True):
    destination = DESTINATION / f"{name}.jpg"
    subprocess.run(["cjpeg", "-quality", "85", *options, "-outfile", str(destination), str(source)], check=True)
    if reference:
        subprocess.run(["djpeg", "-dct", "int", "-grayscale", "-pnm", "-outfile",
                        str(DESTINATION / f"{name}.y.pgm"), str(destination)], check=True)


def pattern(width, height, color):
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            ramp = (x * 191 // max(1, width - 1) + y * 63 // max(1, height - 1))
            checker = 40 if (x // 11 + y // 7) % 2 else 0
            pixels.append(min(255, ramp + checker))
            if color:
                pixels.extend((255 - ramp, (3 * x + 5 * y) % 256))
    return f"{'P6' if color else 'P5'}\n{width} {height}\n255\n".encode() + pixels


def main():
    with tempfile.TemporaryDirectory(prefix="papyrix-jpeg-fixtures-") as directory:
        root = Path(directory)
        color = root / "color.ppm"
        color.write_bytes(pattern(73, 57, True))
        gray = root / "gray.pgm"
        gray.write_bytes(pattern(37, 29, False))
        encode(color, "color-420", ["-progressive", "-sample", "2x2,1x1,1x1"])
        encode(color, "color-444-restart", ["-progressive", "-sample", "1x1,1x1,1x1", "-restart", "2B"])
        encode(color, "baseline", ["-sample", "2x2,1x1,1x1"])
        encode(gray, "gray", ["-progressive"])
        for width, height, level, name in ((20, 20, 0, "black"), (21, 23, 255, "white"),
                                           (2048, 3072, 128, "gray-maximum")):
            gray.write_bytes(f"P5\n{width} {height}\n255\n".encode() + bytes([level]) * width * height)
            encode(gray, name, ["-progressive"], reference=False)
            if name == "black":
                encode(gray, "black-baseline", [], reference=False)
            if name == "gray-maximum":
                encode(gray, "gray-maximum-baseline", [], reference=False)
        color.write_bytes(b"P6\n2048 3072\n255\n" + bytes([128]) * 2048 * 3072 * 3)
        encode(color, "color-maximum", ["-progressive", "-sample", "2x2,1x1,1x1"], reference=False)
        gray.write_bytes(b"P5\n20 20\n255\n" + bytes([128]) * 400)
        for count in (32, 33):
            ac_count = count - 1
            entries = ["0: 0 0 0 0;"]
            entries += [f"0: {k} {k} 0 0;" for k in range(1, ac_count)]
            entries += [f"0: {ac_count} 63 0 0;"]
            scans = root / "scans.txt"
            scans.write_text("\n".join(entries) + "\n")
            encode(gray, f"gray-{count}-scans", ["-scans", str(scans)], reference=False)
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 20, 20, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(61 * 20))) + chunk(b"IEND", b"")
    (DESTINATION / "black.png").write_bytes(png)
    bitmap = struct.pack("<2sIHHI", b"BM", 54 + 1200, 0, 0, 54)
    bitmap += struct.pack("<IiiHHIIiiII", 40, 20, 20, 1, 24, 0, 1200, 0, 0, 0, 0)
    (DESTINATION / "black.bmp").write_bytes(bitmap + bytes(1200))
    for command in ("cjpeg", "djpeg"):
        subprocess.run([command, "-version"], check=True)
    print("Generated local JPEG patterns and independent grayscale references")


if __name__ == "__main__":
    main()
