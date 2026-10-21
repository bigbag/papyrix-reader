#!/usr/bin/env python3
"""Keep a wholly buffered first word out of the next block.

Drives the production HTML and EPUB chapter parsers through reader-test and
checks the word, style, and alignment stream stored in the page cache.
"""

import re
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path

READER = Path(__file__).resolve().parent / "build/reader-test"

FILLER = "Filler text so the chapter spans several pages and batch suspend and resume run. " * 8

# Every case opens a new block or inline scope while the first word is still
# wholly buffered (no whitespace before the opening tag).
BODY = (
    '<div style="margin-top:40px">tail<h3>HEADER</h3></div>'
    "<div>tailp<p>PARAGRAPH</p></div>"
    "<div>tailpre<pre>PREFORMAT</pre></div>"
    "<div>committed <h3>HEADER2</h3></div>"
    '<div>some<b hidden="">HIDDEN</b>thing</div>'
    '<div dir="rtl" style="text-align:left">tailrtl<h3 dir="ltr">LTRHEAD</h3></div>'
    "<div>tailtable<table><tr><td>CELL</td></tr></table></div>"
    "<p>" + FILLER + "</p>"
    "<div>tailbatch<h3>BATCHHEAD</h3></div>"
    "<p>" + FILLER + "</p>"
)
EXPECTED_LINES = [
    r"^\[j\] tail $",
    r"^\[c\] HEADER\[b\] $",
    r"^\[j\] tailp $",
    r"^\[j\] PARAGRAPH $",
    r"^\[j\] tailpre $",
    r"^\[l\] PREFORMAT $",
    r"^\[j\] committed $",
    r"^\[c\] HEADER2\[b\] $",
    r"^\[j\] something $",
    r"^\[r\] tailrtl $",
    r"^\[c\] LTRHEAD\[b\] $",
    r"^\[j\] tailtable $",
    r"^\[c\] \[Table omitted\]\[i\] $",
    r"^\[j\] tailbatch $",
    r"^\[c\] BATCHHEAD\[b\] $",
]

FORBIDDEN_WORDS = ["tailHEADER", "tailpPARAGRAPH", "tailprePREFORMAT", "tailrtlLTRHEAD", "someHIDDEN"]


def pages(cache_dir):
    section_dir = cache_dir / "sections"
    files = sorted(section_dir.glob("*.bin") if section_dir.exists() else cache_dir.glob("pages_*.bin"))
    result = {}
    for file in files:
        data = file.read_bytes()
        count = struct.unpack_from("<I", data, 18)[0]
        lut = struct.unpack_from("<I", data, 23)[0]
        for page in range(count):
            offset = struct.unpack_from("<I", data, lut + page * 4)[0]
            elements = struct.unpack_from("<H", data, offset)[0]
            offset += 2
            lines = []
            positions = []
            for _ in range(elements):
                if data[offset] != 1:
                    raise AssertionError("Text fixture contains a non-text page element")
                position = struct.unpack_from("<hh", data, offset + 1)
                offset += 5
                words = struct.unpack_from("<H", data, offset)[0]
                offset += 2
                text = []
                for _ in range(words):
                    size = struct.unpack_from("<I", data, offset)[0]
                    offset += 4
                    text.append(data[offset:offset + size].decode("utf-8"))
                    offset += size
                word_positions = struct.unpack_from("<" + "H" * words, data, offset)
                positions.append((position, word_positions))
                offset += words * 2
                styles = data[offset:offset + words]
                offset += words
                alignment = ("[j]", "[l]", "[c]", "[r]")[data[offset]]
                offset += 1
                styled = [word + ("", "[b]", "[i]", "[bi]")[style] for word, style in zip(text, styles)]
                lines.append(alignment + " " + " ".join(styled) + " \n")
            result[len(result)] = ("".join(lines), positions)
    return result


def check_lines(name, text, failures):
    for pattern in EXPECTED_LINES:
        if not re.search(pattern, text, flags=re.M):
            failures.append(f"{name}: missing line {pattern}")
    for word in FORBIDDEN_WORDS:
        if word in text:
            failures.append(f"{name}: fused word {word}")


def write_epub(path, chapter):
    container = (
        '<?xml version="1.0"?>'
        '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
        '<rootfiles><rootfile full-path="OEBPS/content.opf" '
        'media-type="application/oebps-package+xml"/></rootfiles></container>'
    )
    opf = (
        '<?xml version="1.0"?>'
        '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="bookid">'
        '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
        "<dc:identifier id=\"bookid\">urn:uuid:papyrix-block-boundary</dc:identifier>"
        "<dc:title>Boundary</dc:title><dc:creator>Test Writer</dc:creator>"
        "<dc:language>en</dc:language></metadata>"
        '<manifest><item id="ch1" href="ch1.xhtml" media-type="application/xhtml+xml"/></manifest>'
        "<spine><itemref idref=\"ch1\"/></spine></package>"
    )
    chapter_doc = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Boundary</title></head>'
        "<body>" + chapter + "</body></html>"
    )
    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        epub.writestr("META-INF/container.xml", container)
        epub.writestr("OEBPS/content.opf", opf)
        epub.writestr("OEBPS/ch1.xhtml", chapter_doc)


def check_book(name, book, cache_root, batches, failures):
    outputs = {}
    for batch in batches:
        out = cache_root / f"{name}-{batch}"
        subprocess.run([str(READER), "--batch", str(batch), str(book), str(out)], check=True, capture_output=True)
        outputs[batch] = pages(next(out.glob("epub_*")) if book.suffix == ".epub" else out)
    check_lines(f"{name} batch {batches[0]}", "".join(page[0] for page in outputs[batches[0]].values()), failures)
    for batch in batches[1:]:
        if outputs[batch] != outputs[batches[0]]:
            failures.append(f"{name}: batch {batch} output differs from unbatched")
    return outputs[batches[0]]


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix="block-boundary-") as temporary:
        root = Path(temporary)
        html_book = root / "boundary.html"
        html_book.write_text("<html><head><title>Boundary</title></head><body>" + BODY + "</body></html>")
        html_pages = check_book("html", html_book, root / "html-out", (0, 1, 5), failures)

        epub_book = root / "boundary.epub"
        write_epub(epub_book, BODY)
        epub_pages = check_book("epub", epub_book, root / "epub-out", (0, 1, 5), failures)

        committed = re.sub(r"(tail(?:p|pre|rtl|table|batch)?)(?=<)", r"\1 ", BODY)
        html_book.write_text("<html><head><title>Boundary</title></head><body>" + committed + "</body></html>")
        committed_html = check_book("html-committed", html_book, root / "committed-html-out", (0,), failures)
        write_epub(epub_book, committed)
        committed_epub = check_book("epub-committed", epub_book, root / "committed-epub-out", (0,), failures)
        if html_pages != committed_html or epub_pages != committed_epub:
            failures.append("Pending and space-terminated words differ in style, position, or pagination")

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        raise SystemExit(1)
    print("HTML/EPUB block boundaries: words, styles, positions, and batches match")


if __name__ == "__main__":
    main()
