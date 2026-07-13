#!/usr/bin/env python3
"""Generate a minimal EPUB with a very long title + author.

Purpose: stress-test how the Books (Recent) screen truncates long metadata on a
small e-ink display. The reader truncates each row to one line, so this book
exists to confirm that truncation and the title/author row layout hold up.

Usage:
    python3 scripts/generate_long_title_book.py [OUTPUT_DIR]

Outputs OUTPUT_DIR/long_title_test.epub (default: /tmp).
"""
from __future__ import annotations

import sys
import zipfile
from pathlib import Path


def _xml_escape(s: str) -> str:
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def _slug(s: str) -> str:
    return "".join(c.lower() if c.isalnum() else "-" for c in s).strip("-")[:48] or "book"


def make_epub(path: Path, title: str, author: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    uid = "urn:uuid:" + _slug(title)

    chapter = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>{_xml_escape(title)}</title></head>
<body>
<p>This book exists only to test how a very long title and author render on the
Books (Recent) screen of the reader.</p>
</body>
</html>
"""

    content_opf = f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>{_xml_escape(title)}</dc:title>
    <dc:creator>{_xml_escape(author)}</dc:creator>
    <dc:language>en</dc:language>
    <dc:identifier id="BookId">{_xml_escape(uid)}</dc:identifier>
  </metadata>
  <manifest>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="styles.css" media-type="text/css"/>
    <item id="chap1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx"><itemref idref="chap1"/></spine>
</package>
"""

    toc_ncx = f"""<?xml version="1.0" encoding="UTF-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="{_xml_escape(uid)}"/></head>
  <docTitle><text>{_xml_escape(title)}</text></docTitle>
  <navMap>
    <navPoint id="nav1" playOrder="1">
      <navLabel><text>{_xml_escape(title)}</text></navLabel>
      <content src="chapter1.xhtml"/>
    </navPoint>
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

    styles_css = b"body { margin: 0; padding: 0; }\n"

    with zipfile.ZipFile(path, "w") as epub:
        # mimetype must be first and stored uncompressed (EPUB spec).
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        epub.writestr("META-INF/container.xml", container_xml)
        epub.writestr("OEBPS/content.opf", content_opf)
        epub.writestr("OEBPS/toc.ncx", toc_ncx)
        epub.writestr("OEBPS/styles.css", styles_css)
        epub.writestr("OEBPS/chapter1.xhtml", chapter)

    print(f"Wrote {path} ({path.stat().st_size} bytes)")


def main() -> int:
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("/tmp")
    out_dir.mkdir(parents=True, exist_ok=True)

    title = (
        "The Extraordinarily and Quite Intentionally Long-Winded Title of a Book "
        "That Keeps Going Well Beyond a Single Line on a Small E-Ink Screen"
    )
    author = (
        "First Middle Last the Third and Also Junior With a Very Lengthy Surname-Appendix"
    )

    path = out_dir / "long_title_test.epub"
    make_epub(path, title, author)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
