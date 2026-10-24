#!/usr/bin/env python3
"""Compile real Power.cpp; rejected try_to_start must restart."""

import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

MOCK = r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using esp_err_t = int;
#ifndef PAPYRIX_TARGET_XTEINK_C3
#define PAPYRIX_TARGET_XTEINK_C3 0
#endif
#ifndef PAPYRIX_TARGET_MURPHY_M4
#define PAPYRIX_TARGET_MURPHY_M4 0
#endif
#ifndef TEST_TOUCH_STOP_FAIL
#define TEST_TOUCH_STOP_FAIL 0
#endif
constexpr int ESP_GPIO_WAKEUP_GPIO_LOW = 0, ESP_EXT1_WAKEUP_ANY_LOW = 2;
inline bool slept = false;
inline bool sdReady = true;
inline bool sdEnded = false, spiEnded = false, pullsOff = false;
inline bool pinsPrepared = false, holdEnabled = false, wakeC3 = false, wakeS3 = false;
inline bool samplingStopped = false;
inline bool touchStopAttempted = false, touchStopped = false;
inline void requireTouchStopped() {
  if (PAPYRIX_TARGET_MURPHY_M4 && !touchStopped) std::_Exit(1);
}
'''

HEADERS = {
    "DisplayController.h": "#pragma once\nnamespace eink { enum class DisplayController { SSD1677 }; }\n",
    "SDCardManager.h":
        '#pragma once\n#include "MockPower.h"\n'
        "struct SdCardManager { bool ready() const { return sdReady; } "
        "void end() { requireTouchStopped(); sdEnded = true; } };\n"
        "inline SdCardManager SdMan;\n",
    "SPI.h":
        '#pragma once\n#include "MockPower.h"\nstruct ArduinoSpi { '
        'void end() { requireTouchStopped(); spiEnded = true; } };\n'
        "inline ArduinoSpi SPI;\n",
    "InputManager.h":
        '#pragma once\n#include "MockPower.h"\n'
        'struct InputManager { void stopSampling() { samplingStopped = true; } };\n'
        'inline InputManager inputManager;\n'
        'inline void disableGpioPullsForSleep() {\n'
        '  if (!samplingStopped) std::_Exit(1);\n'
        '  requireTouchStopped();\n'
        '  pullsOff = true;\n}\n',
    "PowerPolicy.h":
        '#pragma once\n#include "MockPower.h"\n#include "BoardProfile.h"\n'
        "namespace papyrix::board {\n"
        "inline void prepareDeepSleepPins(const BoardProfile&, bool) { requireTouchStopped(); pinsPrepared = true; }\n"
        "inline uint64_t powerButtonWakeMask(const BoardProfile& p) { return 1ULL << p.input.power; }\n"
        "}\n",
    "HardwareIdentity.h":
        '#pragma once\n#include "BoardProfile.h"\n'
        "namespace papyrix::board {\n"
        "struct HardwareIdentity {\n"
        "  static HardwareIdentity& instance() { static HardwareIdentity id; return id; }\n"
        "  const BoardProfile& profile() const {\n"
        "    static BoardProfile p{}; p.input.power = 3; return p;\n}\n};\n}\n",
    "driver/gpio.h":
        '#pragma once\n#include "MockPower.h"\ninline void gpio_deep_sleep_hold_en() { holdEnabled = true; }\n',
    "esp_sleep.h":
        '#pragma once\n#include "MockPower.h"\n'
        "inline void requireReady() {\n"
        "  const bool c3 = PAPYRIX_TARGET_XTEINK_C3 != 0;\n"
        "  if (!sdEnded || !spiEnded || !pullsOff || !pinsPrepared || !holdEnabled ||\n"
        "      (c3 ? !wakeC3 || wakeS3 : !wakeS3 || wakeC3)) {\n"
        "    std::fprintf(stderr, \"FAIL: shutdown/wake incomplete before sleep\\n\");\n"
        "    std::_Exit(1);\n"
        "  }\n"
        "}\n"
        "inline esp_err_t esp_deep_sleep_enable_gpio_wakeup(uint64_t, int) { wakeC3 = true; return 0; }\n"
        "inline esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t, int) { wakeS3 = true; return 0; }\n"
        "inline esp_err_t esp_deep_sleep_try_to_start() { requireReady(); slept = true; return 1; }\n",
    "TouchBackend.h":
        '#pragma once\n#include "MockPower.h"\nnamespace papyrix::board {\n'
        'inline bool stopTouchAcquisition() {\n'
        '  touchStopAttempted = true;\n'
        '  if (!samplingStopped) std::_Exit(1);\n'
        '  if (TEST_TOUCH_STOP_FAIL) return false;\n'
        '  touchStopped = true; return true;\n}\n}\n',
    "Logging.h": '#pragma once\n#define LOG_ERR(...) ((void)0)\n',
    "esp_system.h":
        '#pragma once\n#include "MockPower.h"\n'
        "[[noreturn]] inline void esp_restart() {\n"
        "  if (TEST_TOUCH_STOP_FAIL) {\n"
        "    if (!touchStopAttempted || !samplingStopped || sdEnded || spiEnded || pullsOff || pinsPrepared ||\n"
        "        holdEnabled || wakeC3 || wakeS3 || slept) std::_Exit(1);\n"
        "    std::_Exit(43);\n"
        "  }\n"
        "  if (!slept) { std::fprintf(stderr, \"FAIL: restart without try_to_start\\n\"); std::_Exit(1); }\n"
        "  std::_Exit(42);\n}\n",
}

MAIN = r'''
#include "Power.h"
int main() {
  papyrix::hal::enterDeepSleepWithHardwareShutdown(false);
  return 0;
}
'''

TARGETS = [("C3", ["-DPAPYRIX_TARGET_XTEINK_C3=1"]),
           ("S3", ["-DPAPYRIX_TARGET_X4PRO=1", "-DPAPYRIX_TARGET_XTEINK_C3=0"]),
           ("Classic", ["-DPAPYRIX_TARGET_X4CLASSIC=1", "-DPAPYRIX_TARGET_XTEINK_C3=0"]),
           ("M4", ["-DPAPYRIX_TARGET_MURPHY_M4=1"]),
           ("M4Blocked", ["-DPAPYRIX_TARGET_MURPHY_M4=1", "-DTEST_TOUCH_STOP_FAIL=1"])]


def main():
    for name, defines in TARGETS:
        with tempfile.TemporaryDirectory(prefix=f"papyrix-sleep-abort-{name.lower()}-") as directory:
            path = Path(directory)
            (path / "MockPower.h").write_text(MOCK)
            (path / "main.cpp").write_text(MAIN)
            for header, body in HEADERS.items():
                target = path / header
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(body)
            command = [os.environ.get("CXX", "c++"), "-std=c++17", *defines]
            command += ["-I" + str(path), "-Isrc/hal", "-Ilib/BoardSupport/include"]
            command += ["src/hal/Power.cpp", str(path / "main.cpp"), "-o", str(path / "sleep")]
            subprocess.run(command, cwd=ROOT, check=True, timeout=120)
            try:
                result = subprocess.run([str(path / "sleep")], timeout=10)
            except subprocess.TimeoutExpired:
                raise SystemExit(f"FAIL: {name}: sleep path hung instead of restarting")
            expected = 43 if name == "M4Blocked" else 42
            if result.returncode != expected:
                raise SystemExit(f"{name}: expected esp_restart exit {expected}, got {result.returncode}")
    print("PASS: Power.cpp stops touch before shutdown and restarts after rejected sleep")


if __name__ == "__main__":
    main()
