#!/usr/bin/env python3
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KEY_MAP = ROOT / "lib/I18n/src/I18n.cpp"
LOCALE_DIR = ROOT / "docs/examples/locale"
BUFFER_SIZE = 4096


def i18n_keys() -> set[str]:
    return set(re.findall(r'\{"([A-Z0-9_]+)", StrId::STR_', KEY_MAP.read_text()))


def locale_data(path: Path) -> tuple[set[str], int]:
    keys = set()
    used = 0
    for line in path.read_text().splitlines():
        if not line or line.startswith(("#", ";")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if not key.startswith("_"):
            keys.add(key)
            used += len(value.encode("utf-8")) + 1
    return keys, used


def main() -> int:
    expected = i18n_keys()
    failed = False
    for locale in sorted(LOCALE_DIR.glob("*.txt")):
        actual, used = locale_data(locale)
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        capacity_exceeded = used > BUFFER_SIZE
        if missing or unexpected or capacity_exceeded:
            failed = True
            print(f"FAIL: {locale.relative_to(ROOT)}")
            if missing:
                print(f"  missing: {', '.join(missing)}")
            if unexpected:
                print(f"  unexpected: {', '.join(unexpected)}")
            if capacity_exceeded:
                print(f"  override bytes: {used}/{BUFFER_SIZE}")
        else:
            print(f"PASS: {locale.relative_to(ROOT)} ({len(actual)} keys, {used}/{BUFFER_SIZE} bytes)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
