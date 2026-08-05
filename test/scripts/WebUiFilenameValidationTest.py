#!/usr/bin/env python3
import gzip
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/network/html/AppPage.html"
HEADER = ROOT / "src/network/html/AppPageHtml.generated.h"

subprocess.run([sys.executable, "scripts/build_html.py"], cwd=ROOT, check=True, capture_output=True, text=True)
source = SOURCE.read_text(encoding="utf-8")
header = HEADER.read_text(encoding="utf-8")
compressed = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", header))
served = gzip.decompress(compressed).decode("utf-8")

old_whitelist = r"/^[a-zA-Z0-9_\-. ]+$/"
required = (
    "function isUnsupportedCjk",
    "function validateFileName",
    "validateFileName(name)",
    "validateFileName(newName)",
    "validateFileName(file.name)",
    "CJK filenames are not supported",
)

assert old_whitelist not in source, "ASCII-only folder whitelist remains in source HTML"
assert old_whitelist not in served, "ASCII-only folder whitelist remains in served HTML"
for token in required:
    assert token in source, f"missing source wiring: {token}"
    assert token in served, f"missing generated asset wiring: {token}"

print("WebUiFilenameValidationTest: PASS")
