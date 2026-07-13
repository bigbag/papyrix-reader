#!/usr/bin/env python3
"""
Generate device/host test EPUBs for:

  08db788  Stability: Atomic SD writes and OOM-safe image paths
  c092d94  Epub: Apply CSS block-level margins and padding to page layout. Issue #139

Outputs under tmp/books/ by default.
"""

from __future__ import annotations

import io
import sys
import zipfile
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Please install Pillow: pip install Pillow", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "tmp" / "books"


def get_font(size: int = 18):
    for path in (
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    ):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def jpeg_bytes(img: Image.Image, quality: int = 85) -> bytes:
    buf = io.BytesIO()
    img.convert("RGB").save(buf, format="JPEG", quality=quality, optimize=True)
    return buf.getvalue()


def png_bytes(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def labeled_image(width: int, height: int, title: str, subtitle: str = "", fill: int = 200) -> Image.Image:
    img = Image.new("L", (width, height), fill)
    draw = ImageDraw.Draw(img)
    font = get_font(22)
    small = get_font(14)
    draw.rectangle([2, 2, width - 3, height - 3], outline=0, width=3)
    # checkerboard corners so scaling/dither is obvious
    for y in range(0, height, 24):
        for x in range(0, width, 24):
            if ((x // 24) + (y // 24)) % 2 == 0:
                draw.rectangle([x, y, min(x + 23, width - 1), min(y + 23, height - 1)], fill=max(0, fill - 40))
    draw.rectangle([20, 20, width - 21, 90], fill=255)
    draw.text((30, 30), title, font=font, fill=0)
    if subtitle:
        draw.text((30, 58), subtitle, font=small, fill=64)
    return img


def gradient_image(width: int, height: int) -> Image.Image:
    img = Image.new("L", (width, height), 255)
    px = img.load()
    for y in range(height):
        for x in range(width):
            px[x, y] = int(255 * x / max(1, width - 1))
    draw = ImageDraw.Draw(img)
    draw.rectangle([0, 0, width - 1, height - 1], outline=0, width=2)
    draw.text((10, 10), "GRADIENT (dither test)", font=get_font(16), fill=0)
    return img


def make_epub(path: Path, title: str, author: str, chapters: list[tuple[str, str]], images: dict[str, bytes]) -> None:
    """
    chapters: list of (chapter_title, xhtml_body_inner_html)
    images: filename -> bytes (stored under OEBPS/images/)
            special key __styles.css__ holds stylesheet bytes (not written as an image)
    """
    path.parent.mkdir(parents=True, exist_ok=True)

    assets = dict(images)
    styles_css = assets.pop("__styles.css__", None)
    if styles_css is None:
        styles_css = b"body { margin: 0; padding: 0; }\nimg { max-width: 100%; }\n"
    if isinstance(styles_css, str):
        styles_css = styles_css.encode("utf-8")

    manifest_items = [
        '    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>',
        '    <item id="css" href="styles.css" media-type="text/css"/>',
    ]
    spine_items = []
    ncx_points = []
    chapter_files: list[tuple[str, str]] = []

    for i, (ch_title, body) in enumerate(chapters, start=1):
        cid = f"chap{i:02d}"
        href = f"chapter{i:02d}.xhtml"
        manifest_items.append(f'    <item id="{cid}" href="{href}" media-type="application/xhtml+xml"/>')
        spine_items.append(f'    <itemref idref="{cid}"/>')
        ncx_points.append(
            f"""    <navPoint id="nav{i}" playOrder="{i}">
      <navLabel><text>{_xml_escape(ch_title)}</text></navLabel>
      <content src="{href}"/>
    </navPoint>"""
        )
        xhtml = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
  <title>{_xml_escape(ch_title)}</title>
  <link rel="stylesheet" type="text/css" href="styles.css"/>
</head>
<body>
{body}
</body>
</html>
"""
        chapter_files.append((href, xhtml))

    for name in assets:
        ext = name.rsplit(".", 1)[-1].lower()
        mt = "image/jpeg" if ext in ("jpg", "jpeg") else "image/png"
        safe_id = "img_" + "".join(c if c.isalnum() else "_" for c in name)
        manifest_items.append(f'    <item id="{safe_id}" href="images/{name}" media-type="{mt}"/>')

    content_opf = f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:title>{_xml_escape(title)}</dc:title>
    <dc:creator>{_xml_escape(author)}</dc:creator>
    <dc:language>en</dc:language>
    <dc:identifier id="BookId">urn:uuid:{_slug(title)}</dc:identifier>
  </metadata>
  <manifest>
{chr(10).join(manifest_items)}
  </manifest>
  <spine toc="ncx">
{chr(10).join(spine_items)}
  </spine>
</package>
"""

    toc_ncx = f"""<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head>
    <meta name="dtb:uid" content="urn:uuid:{_slug(title)}"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>{_xml_escape(title)}</text></docTitle>
  <navMap>
{chr(10).join(ncx_points)}
  </navMap>
</ncx>
"""

    container_xml = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        epub.writestr("META-INF/container.xml", container_xml)
        epub.writestr("OEBPS/content.opf", content_opf)
        epub.writestr("OEBPS/toc.ncx", toc_ncx)
        epub.writestr("OEBPS/styles.css", styles_css)
        for href, xhtml in chapter_files:
            epub.writestr(f"OEBPS/{href}", xhtml)
        for name, data in assets.items():
            epub.writestr(f"OEBPS/images/{name}", data)

    print(f"Wrote {path} ({path.stat().st_size} bytes)")


def _xml_escape(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def _slug(s: str) -> str:
    return "".join(c.lower() if c.isalnum() else "-" for c in s).strip("-")[:48]


def build_image_stability_book(out_dir: Path) -> Path:
    """
    For commit 08db788 — Atomic SD writes and OOM-safe image paths.

    Exercises:
    - JPEG + PNG conversion paths (.part → rename publish)
    - Large image (forces bigger dither buffers / OOM degrade path)
    - Multiple images across pages (cache churn)
    - Gradient image (dither quality visible under degrade)
    - Corrupt/tiny decorative image edge cases are avoided (parser min 20px)
    """
    images: dict[str, bytes] = {}

    images["small_jpeg.jpg"] = jpeg_bytes(labeled_image(320, 240, "SMALL JPEG", "fast convert", 180), quality=80)
    images["small_png.png"] = png_bytes(labeled_image(320, 240, "SMALL PNG", "png path", 200))
    images["medium_jpeg.jpg"] = jpeg_bytes(labeled_image(800, 1000, "MEDIUM JPEG", "typical inline", 190), quality=85)
    images["large_jpeg.jpg"] = jpeg_bytes(labeled_image(1600, 2200, "LARGE JPEG", "OOM/dither stress", 170), quality=90)
    images["wide_jpeg.jpg"] = jpeg_bytes(labeled_image(1800, 600, "WIDE JPEG", "landscape scale", 185), quality=85)
    images["gradient.jpg"] = jpeg_bytes(gradient_image(640, 400), quality=92)
    images["gradient.png"] = png_bytes(gradient_image(640, 400))
    images["cover.jpg"] = jpeg_bytes(labeled_image(600, 800, "COVER", "08db788 image stability", 160), quality=88)

    # Second set of unique images so re-open / flip churn converts more than one hash
    for i in range(1, 6):
        images[f"seq_{i}.jpg"] = jpeg_bytes(
            labeled_image(500, 650, f"SEQ IMAGE {i}", "interrupt convert / re-enter", 150 + i * 10),
            quality=82,
        )

    styles = """
body { font-family: serif; margin: 1em; }
h1 { text-align: center; }
p { margin: 0.6em 0; }
img { display: block; margin: 0.8em auto; max-width: 100%; }
.caption { text-align: center; font-size: 0.9em; }
""".strip()
    images["__styles.css__"] = styles.encode("utf-8")

    chapters = [
        (
            "Overview",
            """
<h1>Image Stability Test Book</h1>
<p>Target commit: <b>08db788</b> — Atomic SD writes and OOM-safe image paths.</p>
<p>Use this book on device (or with reader-test) to verify:</p>
<p>1. First view of each image is clean (no half-written BMP noise).</p>
<p>2. Leaving mid-convert and returning never shows garbage; only full image or placeholder.</p>
<p>3. Large images do not reboot the device under memory pressure (may look less dithered).</p>
<p>4. After a successful convert, no <code>.bmp.part</code> remains for that hash under the book cache.</p>
""",
        ),
        (
            "Small JPEG + PNG",
            """
<h1>Small Formats</h1>
<p>Both formats should convert and display on first open.</p>
<p><img src="images/small_jpeg.jpg" alt="small jpeg"/></p>
<p class="caption">JPEG 320x240</p>
<p><img src="images/small_png.png" alt="small png"/></p>
<p class="caption">PNG 320x240</p>
""",
        ),
        (
            "Medium JPEG",
            """
<h1>Medium JPEG</h1>
<p>Typical chapter illustration size. Flip away during first convert, then return.</p>
<p><img src="images/medium_jpeg.jpg" alt="medium jpeg"/></p>
<p>Text after image — should remain readable and not inherit image centering bugs.</p>
""",
        ),
        (
            "Large JPEG (OOM stress)",
            """
<h1>Large JPEG</h1>
<p>1600x2200 — stresses dither row buffers. Under OOM, decode should degrade (no dither / simple quantize) instead of rebooting.</p>
<p><img src="images/large_jpeg.jpg" alt="large jpeg"/></p>
<p>If the device reboots here under heap pressure, the OOM-safe path failed.</p>
""",
        ),
        (
            "Wide + Gradient",
            """
<h1>Wide Landscape + Gradient</h1>
<p>Wide image tests scale path; gradient makes dither vs quantize visible.</p>
<p><img src="images/wide_jpeg.jpg" alt="wide jpeg"/></p>
<p><img src="images/gradient.jpg" alt="gradient jpeg"/></p>
<p><img src="images/gradient.png" alt="gradient png"/></p>
""",
        ),
        (
            "Sequence churn 1-3",
            """
<h1>Sequence 1–3</h1>
<p>Open each image, flip quickly, re-open book. Watch for atomic publish (.part → rename).</p>
<p><img src="images/seq_1.jpg" alt="seq 1"/></p>
<p><img src="images/seq_2.jpg" alt="seq 2"/></p>
<p><img src="images/seq_3.jpg" alt="seq 3"/></p>
""",
        ),
        (
            "Sequence churn 4-5",
            """
<h1>Sequence 4–5</h1>
<p>Continue convert churn. Second visit of earlier chapters should hit final BMP only.</p>
<p><img src="images/seq_4.jpg" alt="seq 4"/></p>
<p><img src="images/seq_5.jpg" alt="seq 5"/></p>
""",
        ),
        (
            "Mixed page",
            """
<h1>Mixed On One Page</h1>
<p>Multiple converts in one chapter.</p>
<p><img src="images/small_jpeg.jpg" alt="again jpeg"/></p>
<p><img src="images/small_png.png" alt="again png"/></p>
<p><img src="images/gradient.jpg" alt="again gradient"/></p>
<p>End of image stability book.</p>
""",
        ),
    ]

    out = out_dir / "test_image_stability_08db788.epub"
    make_epub(out, "Image Stability (08db788)", "Papyrix Test Suite", chapters, images)
    return out


def build_css_margins_book(out_dir: Path) -> Path:
    """
    For commit c092d94 / Issue #139 — CSS block-level margins and padding.

    Exercises:
    - margin / padding longhand + shorthand
    - blockquote indent (horizontal + vertical)
    - nested block horizontal accumulation
    - text-align from CSS
    - display:none skip
    - header center default
    - pre + br continuation insets
    - long content for batch/suspend (--batch 5) margin continuity
    - units: em, px, %
    """
    styles = """
body {
  margin: 0;
  padding: 0;
  font-family: serif;
}

h1 {
  margin-top: 1.2em;
  margin-bottom: 0.6em;
  /* headers default to center when no text-align; this one is explicit */
  text-align: center;
}

h2 {
  margin-top: 1em;
  margin-bottom: 0.4em;
  text-align: left;
}

p {
  margin-top: 0.4em;
  margin-bottom: 0.4em;
}

p.indented {
  margin-left: 2em;
  margin-right: 1em;
}

p.padded {
  padding-left: 1.5em;
  padding-right: 1.5em;
  padding-top: 0.5em;
  padding-bottom: 0.5em;
}

p.shorthand {
  margin: 1em 2em;
  padding: 0.5em 1em;
}

p.percent {
  margin-left: 10%;
  margin-right: 10%;
}

p.px {
  margin-left: 40px;
  margin-right: 20px;
  margin-top: 12px;
  margin-bottom: 12px;
}

p.center {
  text-align: center;
  margin-left: 1em;
  margin-right: 1em;
}

p.right {
  text-align: right;
  margin-right: 1em;
}

p.justify {
  text-align: justify;
  margin-left: 0.5em;
  margin-right: 0.5em;
}

blockquote {
  margin: 1em 1.5em;
  padding: 0.5em 1em;
}

blockquote.deep {
  margin-left: 2em;
  margin-right: 1em;
  padding-left: 1em;
  border: none;
}

blockquote p {
  margin-top: 0.3em;
  margin-bottom: 0.3em;
}

div.box {
  margin: 1em;
  padding: 0.8em;
}

div.nested-outer {
  margin-left: 1em;
  padding-left: 0.5em;
}

div.nested-inner {
  margin-left: 1em;
  padding-left: 0.5em;
}

.hidden, .display-none {
  display: none;
}

pre.code {
  margin: 1em 1.5em;
  padding: 0.5em;
  text-align: left;
}

li {
  margin-left: 1em;
  margin-bottom: 0.3em;
}
""".strip()

    images: dict[str, bytes] = {"__styles.css__": styles.encode("utf-8")}

    # Enough text to cross batch boundaries with --batch 5
    filler = (
        "The quick brown fox jumps over the lazy dog. "
        "Pack my box with five dozen liquor jugs. "
        "How vexingly quick daft zebras jump. "
        "Sphinx of black quartz, judge my vow. "
    )
    long_para = " ".join([filler] * 12)
    long_chapter_body = "\n".join(
        f'<p class="indented">Paragraph {i + 1}. {long_para}</p>' for i in range(18)
    )

    chapters = [
        (
            "Overview",
            """
<h1>CSS Margins &amp; Padding Test</h1>
<p>Target commit: <b>c092d94</b> / Issue #139 — Apply CSS block-level margins and padding to page layout.</p>
<p>Compare with CrossPoint screenshots on the issue: blockquotes and indented paragraphs should show visible left/right inset and vertical spacing, not flush full-width text.</p>
<p class="display-none">THIS MUST NOT APPEAR — display:none on class.</p>
<p style="display:none">THIS MUST NOT APPEAR — inline display:none.</p>
<p>Visible paragraph after hidden ones. If you see the hidden text above, display:none failed.</p>
""",
        ),
        (
            "Margins longhand",
            """
<h1>Margin Longhand</h1>
<p>Normal paragraph with stylesheet default margins (0.4em top/bottom).</p>
<p class="indented">This paragraph has <b>margin-left: 2em</b> and <b>margin-right: 1em</b>. It should be visibly narrower and shifted right compared to the normal paragraph above.</p>
<p class="px">This paragraph uses <b>pixel margins</b> (left 40px, right 20px, top/bottom 12px).</p>
<p class="percent">This paragraph uses <b>10% left/right margins</b> (relative to viewport width).</p>
<p>Back to normal width — confirms previous margins do not leak into this block.</p>
""",
        ),
        (
            "Padding &amp; shorthand",
            """
<h1>Padding and Shorthand</h1>
<p class="padded">This paragraph has <b>padding: 0.5em 1.5em</b> (via longhand classes). Content should sit inset from the block edges; on e-ink the effect is reduced line width + vertical gap.</p>
<p class="shorthand">Shorthand: <b>margin: 1em 2em; padding: 0.5em 1em</b>. Strong horizontal inset expected.</p>
<p>Normal again.</p>
""",
        ),
        (
            "Blockquotes",
            """
<h1>Blockquotes</h1>
<p>Lead-in text at full content width.</p>
<blockquote>
  <p>Quoted text inside blockquote with <b>margin: 1em 1.5em</b> and <b>padding: 0.5em 1em</b>. The entire quote block should be indented from both sides, with space above and below the quote group.</p>
  <p>Second paragraph in the same blockquote — should keep the same horizontal inset (nested p inside blockquote).</p>
</blockquote>
<p>After first quote — full width again.</p>
<blockquote class="deep">
  <p>Deep blockquote: margin-left 2em + padding-left 1em. Stronger left indent than the previous quote.</p>
</blockquote>
<p>End of blockquote chapter.</p>
""",
        ),
        (
            "Nested divs",
            """
<h1>Nested Horizontal Insets</h1>
<div class="nested-outer">
  <p>Outer div: margin-left 1em + padding-left 0.5em.</p>
  <div class="nested-inner">
    <p>Inner div adds another margin-left 1em + padding-left 0.5em. Horizontal insets should accumulate; this line should be further right than the outer paragraph.</p>
  </div>
  <p>Back in outer div only.</p>
</div>
<p>Full-width body paragraph after nested divs.</p>
<div class="box">
  <p>Box div with margin 1em and padding 0.8em wrapping this paragraph.</p>
</div>
""",
        ),
        (
            "Text align",
            """
<h1>Text Align + Margins</h1>
<p class="center">Centered paragraph with side margins.</p>
<p class="right">Right-aligned paragraph with margin-right.</p>
<p class="justify">Justified paragraph with small side margins. """
            + long_para
            + """</p>
<h2>Left heading (explicit)</h2>
<p>Body under left heading.</p>
""",
        ),
        (
            "BR and PRE",
            """
<h1>Line Breaks and Pre</h1>
<p class="indented">Indented paragraph with a hard break here:<br/>Second line after br — should keep the same left inset as the first line (continuation insets).</p>
<pre class="code">def hello():
    print("pre block")
    print("left aligned, monospaced intent")
    # margin 1em 1.5em should inset the whole pre block
</pre>
<p>After pre — full width.</p>
""",
        ),
        (
            "Display none nested",
            """
<h1>display:none Nesting</h1>
<blockquote>
  <div class="display-none"><p>Hidden inside blockquote — must not show and must not break blockquote stack.</p></div>
  <p>Visible quote paragraph after a display:none sibling. Blockquote left/right inset must still apply.</p>
</blockquote>
<p>Body after quote.</p>
""",
        ),
        (
            "Long indented (batch)",
            f"""
<h1>Long Indented Chapter</h1>
<p>Use <code>reader-test --dump --batch 5</code> on this chapter. Every continuation page after a batch boundary should keep the 2em left indent — if text jumps to full width mid-paragraph, suspend/resume lost BlockStyle.</p>
{long_chapter_body}
""",
        ),
        (
            "Clamp extremes",
            """
<h1>Clamp / Extreme Values</h1>
<p style="margin-left: 20em; margin-right: 20em;">Huge horizontal margins (20em) — should clamp to max horizontal inset (2em each side), not zero-width or overflow.</p>
<p style="margin-top: 10em; margin-bottom: 10em;">Huge vertical margins (10em) — should clamp to max vertical (~4em), not push forever.</p>
<p style="margin-left: -2em;">Negative margin — should clamp to 0 (no shift left off-screen).</p>
<p>Final normal paragraph.</p>
""",
        ),
    ]

    out = out_dir / "test_css_margins_c092d94.epub"
    make_epub(out, "CSS Margins Padding (c092d94 / #139)", "Papyrix Test Suite", chapters, images)
    return out


def main() -> int:
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    out_dir.mkdir(parents=True, exist_ok=True)
    a = build_image_stability_book(out_dir)
    b = build_css_margins_book(out_dir)
    print("\nTest books ready:")
    print(f"  {a}")
    print(f"  {b}")
    print("\nHost smoke (optional):")
    print(f"  make reader-test FILE={a} OUTPUT=/tmp/cache-img")
    print(f"  make reader-test FILE={b} OUTPUT=/tmp/cache-css ARGS='--dump --batch 5'")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
