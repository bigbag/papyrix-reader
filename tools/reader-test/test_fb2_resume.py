#!/usr/bin/env python3
"""Compare FB2 page text after a batch boundary with an uninterrupted parse."""

import re
import subprocess
import tempfile
from pathlib import Path

READER = Path(__file__).resolve().parent / "build/reader-test"


def pages(cache):
    result = subprocess.run([READER, "--cache-dump", cache], check=True, capture_output=True, text=True)
    parts = re.split(r"^    --- Page (\d+) ---\n", result.stdout, flags=re.M)
    return {int(parts[i]): parts[i + 1] for i in range(1, len(parts), 2)}


def main():
    with tempfile.TemporaryDirectory(prefix="fb2-resume-") as temporary:
        root = Path(temporary)
        book = root / "sections.fb2"
        paragraphs = "".join(
            f"<p>Section {i}. " + "A fresh scratch allocation must not erase the previous page. " * 6 + "</p>"
            for i in range(20)
        )
        book.write_text(
            '<?xml version="1.0" encoding="UTF-8"?><FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0">'
            "<description><title-info><genre>sf</genre><author><first-name>Test</first-name>"
            "<last-name>Writer</last-name></author><book-title>Resume</book-title><lang>en</lang>"
            "</title-info></description><body><section><title><p>Scratch</p></title>"
            + paragraphs
            + "</section></body></FictionBook>"
        )
        for name, batch in (("batched", "5"), ("single", "0")):
            subprocess.run([READER, "--batch", batch, book, root / name], check=True, capture_output=True)
        batched, single = pages(root / "batched"), pages(root / "single")
        assert batched and batched.keys() == single.keys(), "FB2 page count differs after resume"
        for index in batched:
            assert batched[index] == single[index], f"FB2 text differs on page {index} after resume"
        assert "Section 0." in "".join(batched.values()) and "Section 19." in "".join(batched.values())
        print(f"FB2 batch resume: {len(batched)} pages match")


if __name__ == "__main__":
    main()
