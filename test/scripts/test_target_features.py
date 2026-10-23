#!/usr/bin/env python3
"""Test scripts/check_target_features.py against good and bad symbol listings."""

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts" / "check_target_features.py"

EXIT_PASS = 0
EXIT_FORBIDDEN = 1
EXIT_USAGE = 2

GOOD_DEFAULT = """\
# nm -C .pio/build/default/firmware.elf

0805a1b0 T papyrix::Device::runFullProbe_()
0806c2f4 T papyrix::X3ControllerPolicy::select(unsigned char)
1000abcd D kX4Profile
1000ff00 D kX3Profile
         U free
         U malloc
20001234 R logTags
operator delete(void*)
"""

GOOD_X4PRO = """\
0805a1b0 T papyrix::board::x4pro::enableTouch(papyrix::board::TouchConfig const&)
0805c001 T Gt911Touch::init()
0805c100 T papyrix::Sdmmc1Bit::mount()
"""

GOOD_X4C = """\
0805a1b0 T papyrix::eink::Uc8279X4ProDriver::display()
0805c001 T papyrix::eink::Uc8179X4ProDriver::display()
0805c100 D kX4ClassicProfile
"""

GOOD_STICKY = """\
0805a1b0 T papyrix::eink::Ssd2677Driver::display()
0805c001 T papyrix::board::TouchBackend::poll()
0805c100 T BatteryMonitor::readBq27220Mv_()
0805c200 T papyrix::board::RtcBackend::read()
0805c300 T ProgressiveJpeg::decode(FsFile&)
"""


BAD_DEFAULT = {
    "X4 Pro board symbols": "1000aaaa T papyrix::board::x4pro::enableTouch(papyrix::board::TouchConfig const&)",
    "PaperMono board symbols": "1000bbbb D PaperMonoProfile",
    "GT911 touch symbols": "1000cccc T Gt911Touch::init()",
    "FT6336 touch symbols": "1000dddd T Ft6336gTouch::readSample()",
    "swipe gesture symbols": "1000eeee T Input::handleSwipeGesture()",
    "Bluetooth symbols": "1000ffff T BluetoothSerial::begin()",
    "X4ProVariant placeholder": "1000abcd D kPanelNameX4ProVariant",
}
BAD_X4PRO = {
    "PaperMono board symbols": "1000bbbb D PaperMonoProfile",
    "FT6336 touch symbols": "1000dddd T Ft6336gTouch::readSample()",
    "swipe gesture symbols": "1000eeee T Input::handleSwipeGesture()",
    "Bluetooth symbols": "1000ffff T BluetoothSerial::begin()",
    "X4ProVariant placeholder": "1000abcd D kPanelNameX4ProVariant",
}
BAD_X4C = {
    **BAD_X4PRO,
    "GT911 touch symbols": "1000cccc T Gt911Touch::init()",
    "frontlight symbols": "1000abcd T papyrix::board::FrontLightBackend::write()",
    "Pro touch board symbols": "1000aaaa T papyrix::board::x4pro::enableTouch()",
}
BAD_STICKY = {
    **BAD_X4PRO,
    "X4 Pro board symbols": "1000aaaa T papyrix::board::x4pro::enableTouch()",
    "frontlight symbols": "1000aabb T papyrix::board::FrontLightBackend::write()",
    "C3 panel symbols": "1000ccdd T papyrix::eink::Uc8279X3Driver::begin()",
    "CW2017 symbols": "1000ddee T BatteryMonitor::readCw2017Soc_()",
    "SDMMC symbols": "1000eeff T papyrix::Sdmmc1Bit::mount()",
    "foreign board profiles": "1000ff00 D kX4ClassicProfile",
}


def run_checker(symbol_file, target):
    return subprocess.run(
        [sys.executable, str(CHECKER), str(symbol_file), target],
        capture_output=True,
        text=True,
    )


def write_listing(directory, name, content):
    path = directory / name
    path.write_text(content)
    return path


def check(name, condition, detail=""):
    if condition:
        print(f"PASS: {name}")
        return True
    print(f"FAIL: {name}")
    if detail:
        print(f"  {detail}")
    return False


def expect_pass(name, listing, target):
    result = run_checker(listing, target)
    return check(
        name,
        result.returncode == EXIT_PASS and "FAIL" not in result.stdout,
        f"exit={result.returncode} stdout={result.stdout.strip()!r} stderr={result.stderr.strip()!r}",
    )


def expect_forbidden(name, listing, target, group):
    result = run_checker(listing, target)
    return check(
        name,
        result.returncode == EXIT_FORBIDDEN
        and group in result.stdout
        and result.stdout.count("group=") == 1
        and "PASS" not in result.stdout,
        f"exit={result.returncode} stdout={result.stdout.strip()!r}",
    )


def expect_usage_error(name, args, target_in_args):
    result = subprocess.run(
        [sys.executable, str(CHECKER)] + args, capture_output=True, text=True
    )
    output = result.stdout + result.stderr
    detail = f"exit={result.returncode} output={output.strip()!r}"
    ok = result.returncode == EXIT_USAGE and "error:" in output
    if target_in_args:
        ok = ok and "supported:" in output
    return check(name, ok, detail)


def main():
    if not CHECKER.exists():
        print(f"FAIL: checker not found: {CHECKER.relative_to(ROOT)}")
        return 1
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)

        listing = write_listing(tmp, "good_default.txt", GOOD_DEFAULT)
        failed |= not expect_pass("good default listing passes", listing, "default")

        listing = write_listing(tmp, "good_x4pro.txt", GOOD_X4PRO)
        failed |= not expect_pass("good x4pro listing passes", listing, "x4pro")
        listing = write_listing(tmp, "good_x4c.txt", GOOD_X4C)
        failed |= not expect_pass("Classic permits shared S3 panel drivers", listing, "x4c")
        listing = write_listing(tmp, "good_sticky.txt", GOOD_STICKY)
        failed |= not expect_pass("Sticky permits its display, input, gauge, RTC, and image paths", listing, "sticky")
        for target in ("default", "x4pro", "x4c"):
            listing = write_listing(tmp, "sticky_driver.txt", "1000abcd T papyrix::eink::Ssd2677Driver::display()\n")
            failed |= not expect_forbidden("current targets reject Sticky drivers", listing, target,
                                           "Sticky panel symbols")

        for symbol in ("ProgressiveJpeg::decode(FsFile&)", "vtable for ProgressiveJpegImageConverter"):
            listing = write_listing(tmp, "progressive.txt", f"1000abcd T {symbol}\n")
            failed |= not expect_forbidden("C3 rejects progressive decoding", listing, "default", "progressive JPEG symbols")
            failed |= not expect_pass("Pro permits progressive decoding", listing, "x4pro")
            failed |= not expect_pass("Classic permits progressive decoding", listing, "x4c")

        for target, bad in (
            ("default", BAD_DEFAULT),
            ("x4pro", BAD_X4PRO),
            ("x4c", BAD_X4C),
            ("sticky", BAD_STICKY),
        ):
            for index, (group, line) in enumerate(bad.items()):
                listing = write_listing(tmp, f"bad_{target}_{index}.txt", line + "\n")
                failed |= not expect_forbidden(f"{target} rejects {group}", listing, target, group)

        listing = write_listing(tmp, "unknown.txt", "1000aaaa D GT911Touch::init()\n")
        failed |= not expect_usage_error(
            "unknown target exits 2", [str(listing), "x3"], True
        )
        failed |= not expect_usage_error("no arguments exits 2", [], False)
        failed |= not expect_usage_error(
            "too many arguments exits 2", ["a", "default", "b"], False
        )
        failed |= not expect_usage_error(
            "missing symbol file exits 2", [str(tmp / "nope.txt"), "default"], False
        )

    print()
    print("All target feature checker tests passed." if not failed else "Checker tests failed.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
