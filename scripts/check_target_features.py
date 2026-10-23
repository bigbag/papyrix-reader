#!/usr/bin/env python3
"""Reject forbidden firmware symbols for one target."""

import re
import sys
from pathlib import Path

EXIT_PASS = 0
EXIT_FORBIDDEN = 1
EXIT_USAGE = 2

USAGE = "usage: python scripts/check_target_features.py SYMBOL_FILE TARGET"

# This pattern rejects X4 Pro symbols without matching C3 X4 names such as `kX4Profile`.
X4PRO = r"X4Pro(?!Variant)(?![a-z])|::x4pro::"

GROUPS = {
    "X4 Pro board symbols": re.compile(X4PRO),
    "PaperMono board symbols": re.compile(r"PaperMono"),
    "GT911 touch symbols": re.compile(r"GT911", re.IGNORECASE),
    "FT6336 touch symbols": re.compile(r"FT6336", re.IGNORECASE),
    "swipe gesture symbols": re.compile(r"[Ss]wipe"),
    "Bluetooth symbols": re.compile(r"Bluetooth"),
    "X4ProVariant placeholder": re.compile(r"X4ProVariant"),
    "frontlight symbols": re.compile(r"FrontLightBackend::"),
    "Pro touch board symbols": re.compile(r"::x4pro::"),
    "progressive JPEG symbols": re.compile(r"ProgressiveJpeg::|ProgressiveJpegImageConverter"),
}

FORBIDDEN_GROUP_NAMES = {
    "default": (
        "X4 Pro board symbols",
        "PaperMono board symbols",
        "GT911 touch symbols",
        "FT6336 touch symbols",
        "swipe gesture symbols",
        "Bluetooth symbols",
        "X4ProVariant placeholder",
        "progressive JPEG symbols",
    ),
    "x4pro": (
        "PaperMono board symbols",
        "FT6336 touch symbols",
        "swipe gesture symbols",
        "Bluetooth symbols",
        "X4ProVariant placeholder",
    ),
    "x4c": (
        "PaperMono board symbols",
        "GT911 touch symbols",
        "FT6336 touch symbols",
        "swipe gesture symbols",
        "Bluetooth symbols",
        "X4ProVariant placeholder",
        "frontlight symbols",
        "Pro touch board symbols",
    ),
}

SUPPORTED_TARGETS = tuple(FORBIDDEN_GROUP_NAMES)

_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")


def is_address(token):
    return len(token) >= 4 and all(ch in _HEX_DIGITS for ch in token)


def symbol_name(line):
    """Return the symbol name from one `nm -C` output line."""
    stripped = line.strip()
    if not stripped or stripped.startswith("#"):
        return None
    fields = stripped.split(maxsplit=2)
    if len(fields) >= 2 and is_address(fields[0]) and len(fields[1]) == 1 and fields[1].isalpha():
        # nm includes the address in this form. Demangled names can contain spaces.
        return fields[2] if len(fields) == 3 else None
    if len(fields) == 2 and len(fields[0]) == 1 and fields[0].isalpha():
        # nm omits the address in this form.
        return fields[1]
    return stripped


def read_symbols(path):
    names = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        name = symbol_name(line)
        if name:
            names.append(name)
    return names


def violations(names, group_names):
    patterns = [(group, GROUPS[group]) for group in group_names]
    found = set()
    for name in names:
        for group, pattern in patterns:
            if pattern.search(name):
                found.add((name, group))
    return sorted(found)


def main(argv):
    if len(argv) != 2:
        print(USAGE, file=sys.stderr)
        print("error: expected exactly 2 arguments: SYMBOL_FILE TARGET", file=sys.stderr)
        return EXIT_USAGE
    symbol_file = Path(argv[0])
    target = argv[1]
    if target not in SUPPORTED_TARGETS:
        print(USAGE, file=sys.stderr)
        print(
            "error: unknown target '%s' (supported: %s)" % (target, ", ".join(SUPPORTED_TARGETS)),
            file=sys.stderr,
        )
        return EXIT_USAGE
    if not symbol_file.is_file():
        print(USAGE, file=sys.stderr)
        print("error: cannot read symbol file '%s'" % symbol_file, file=sys.stderr)
        return EXIT_USAGE
    try:
        names = read_symbols(symbol_file)
    except OSError as exc:
        print(USAGE, file=sys.stderr)
        print("error: cannot read symbol file '%s': %s" % (symbol_file, exc), file=sys.stderr)
        return EXIT_USAGE

    found = violations(names, FORBIDDEN_GROUP_NAMES[target])
    if found:
        for name, group in found:
            print("FAIL target=%s symbol='%s' group='%s'" % (target, name, group))
        print(
            "FAIL target=%s: %d forbidden symbol%s in %d scanned"
            % (target, len(found), "s" if len(found) != 1 else "", len(names))
        )
        return EXIT_FORBIDDEN
    print("PASS target=%s: %d symbols scanned, 0 forbidden" % (target, len(names)))
    return EXIT_PASS


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
