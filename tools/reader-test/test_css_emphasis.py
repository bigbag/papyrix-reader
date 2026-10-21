#!/usr/bin/env python3
"""Checks CSS emphasis inheritance and cascade through the production HTML and EPUB chapter parsers."""

import re
import subprocess
import tempfile
import zipfile
from pathlib import Path

from test_html_block_boundary import pages

READER = Path(__file__).resolve().parent / "build/reader-test"

FILLER = "Filler text so the chapter spans several pages and batch suspend and resume run. " * 8

INLINE_BODY = (
    '<div style="font-weight:bold;font-style:italic">biPARENT '
    '<span style="font-weight:normal;font-style:normal">biNORM </span>'
    '<span style="font-weight:inherit;font-style:inherit">biKEEP </span>'
    '<span style="font-weight:initial;font-style:initial">biRST </span>'
    "biTAIL</div>"
    "<p>biAFTER</p>"
    '<div style="font-style:italic">igP '
    '<span style="font-style:normal">igN <span style="font-style:inherit">igGIN</span> igT</span> igA</div>'
    '<div style="font-weight:bold">xP <span style="font-weight:normal">xDIS <b>xREEN</b> xTAIL</span> xAFT</div>'
    "<p>semA <b>semB</b> semC</p>"
    '<p>semNO <b style="font-weight:normal">semOFF</b> semIN <b style="font-weight:inherit">semKEEP</b></p>'
    "<p>itA <i>itB <em>itC</em> itD</i> itE</p>"
    "<h3>hdHEAD</h3>"
    '<h3 style="font-weight:normal">hdNO</h3>'
    "<p>hdAFTER</p>"
    '<p style="font-weight:bold">some<span style="font-weight:normal"/>thing</p>'
    '<p style="font-style:italic">ini<span style="font-style:initial"></span>tial</p>'
    '<p style="font-weight:bold;font-style:italic">bo<span style="font-weight:normal;font-style:initial"></span>th</p>'
    '<p style="font-weight:bold">n1<span style="font-weight:normal"><span></span></span>n2 '
    '<span style="font-weight:normal">mid</span>n3</p>'
    '<p>some<b hidden="">HIDDEN</b>thing</p>'
    "<pre><b>preB</b>preN<i>preI</i>preA</pre>"
    "<pre>  <b>indentB</b><i>indentI</i></pre>"
)

CSS_BODY = (
    '<div class="xbi">cssP '
    '<span class="xoff">cssN </span>'
    '<span class="xkeep">cssK </span>'
    '<span class="xrst">cssR </span>'
    "cssT</div>"
    '<div class="xbi">ccP <span class="xoff" style="font-weight:inherit;font-style:inherit">ccKEEP</span> ccT</div>'
    '<p class="xbi">ovP <span style="font-weight:normal">ovN</span></p>'
    "<p>cssAFTER</p>"
)

STYLESHEET = (
    ".xbi { font-weight: bold; font-style: italic; }\n"
    ".xoff { font-weight: normal; font-style: normal; }\n"
    ".xkeep { font-weight: inherit; font-style: inherit; }\n"
    ".xrst { font-weight: initial; font-style: initial; }\n"
)

# Ordered styled words per block. The trailing space keeps a single word from
# matching inside a styled variant of itself.
EXPECTED_WORDS = [
    "biPARENT[bi] biNORM biKEEP[bi] biRST biTAIL[bi] ",
    "biAFTER ",
    "igP[i] igN igGIN igT igA[i] ",
    "xP[b] xDIS xREEN[b] xTAIL xAFT[b] ",
    "semA semB[b] semC ",
    "semNO semOFF semIN semKEEP ",
    "itA itB[i] itC[i] itD[i] itE ",
    "hdAFTER ",
    "something[b] ",
    "initial[i] ",
    "both[bi] ",
    "n1n2[b] mid n3[b] ",
    "something ",
    "preB[b] preN preI[i] preA ",
    "indentB[b] indentI[i] ",
]

CSS_EXPECTED_WORDS = [
    "cssP[bi] cssN cssK[bi] cssR cssT[bi] ",
    "ccP[bi] ccKEEP[bi] ccT[bi] ",
    "ovP[bi] ovN[i] ",
    "cssAFTER ",
]

HEADING_LINES = [
    r"^\[c\] hdHEAD\[b\] $",
    r"^\[c\] hdNO $",
]

FORBIDDEN_WORDS = ["biPARENTbiNORM", "semAsemB", "someHIDDEN", "xPxDIS", "cssPcssN", "some thing", "n1 n2"]


def flat_words(text):
    stream = re.sub(r"^\[[a-z]\] ", "", text, flags=re.M)
    return re.sub(r"\s+", " ", stream)


def check_book(name, book, cache_root, batches, expected_words, failures):
    outputs = {}
    for batch in batches:
        out = cache_root / f"{name}-{batch}"
        subprocess.run([str(READER), "--batch", str(batch), str(book), str(out)], check=True, capture_output=True)
        outputs[batch] = pages(next(out.glob("epub_*")) if book.suffix == ".epub" else out)
    text = "".join(page[0] for page in outputs[batches[0]].values())
    flat = flat_words(text)
    for words in expected_words:
        if words not in flat:
            failures.append(f"{name} batch {batches[0]}: missing words {words!r}")
    for pattern in HEADING_LINES:
        if not re.search(pattern, text, flags=re.M):
            failures.append(f"{name} batch {batches[0]}: missing heading line {pattern}")
    for word in FORBIDDEN_WORDS:
        if word in flat:
            failures.append(f"{name}: fused word {word}")
    for batch in batches[1:]:
        if outputs[batch] != outputs[batches[0]]:
            failures.append(f"{name}: batch {batch} output differs from unbatched")


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
        "<dc:identifier id=\"bookid\">urn:uuid:papyrix-css-emphasis</dc:identifier>"
        "<dc:title>Emphasis</dc:title><dc:creator>Test Writer</dc:creator>"
        "<dc:language>en</dc:language></metadata>"
        '<manifest><item id="ch1" href="ch1.xhtml" media-type="application/xhtml+xml"/>'
        '<item id="css" href="styles.css" media-type="text/css"/></manifest>'
        "<spine><itemref idref=\"ch1\"/></spine></package>"
    )
    chapter_doc = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Emphasis</title></head>'
        "<body>" + chapter + "</body></html>"
    )
    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        epub.writestr("META-INF/container.xml", container)
        epub.writestr("OEBPS/content.opf", opf)
        epub.writestr("OEBPS/ch1.xhtml", chapter_doc)
        epub.writestr("OEBPS/styles.css", STYLESHEET)


def main():
    failures = []
    with tempfile.TemporaryDirectory(prefix="css-emphasis-") as temporary:
        root = Path(temporary)
        html_book = root / "emphasis.html"
        html_book.write_text(
            "<html><head><title>Emphasis</title></head><body><p>"
            + FILLER + "</p>" + INLINE_BODY + "<p>" + FILLER + "</p></body></html>"
        )
        check_book("html", html_book, root / "html-out", (0, 1, 5), EXPECTED_WORDS, failures)

        epub_book = root / "emphasis.epub"
        write_epub(epub_book, "<p>" + FILLER + "</p>" + INLINE_BODY + CSS_BODY + "<p>" + FILLER + "</p>")
        check_book("epub", epub_book, root / "epub-out", (0, 1, 5), EXPECTED_WORDS + CSS_EXPECTED_WORDS, failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        raise SystemExit(1)
    print("CSS emphasis: inheritance, normal, inherit, initial, and batches match")


if __name__ == "__main__":
    main()
