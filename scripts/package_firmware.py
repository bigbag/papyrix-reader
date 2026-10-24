#!/usr/bin/env python3
import argparse
import configparser
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

ARTIFACTS = (
    {
        "environment": "release_xteink_c3",
        "checker_target": "default",
        "filename": "papyrix-xteink-c3.bin",
        "mcu": "ESP32-C3",
        "boards": ["X3", "X4"],
        "panels": ["SSD1677", "UC8253", "UC8279_X3"],
    },
    {
        "environment": "release_x4pro",
        "checker_target": "x4pro",
        "filename": "papyrix-x4pro.bin",
        "mcu": "ESP32-S3",
        "boards": ["X4Pro"],
        "panels": ["UC8279_X4PRO", "UC8179_X4PRO"],
    },
    {
        "environment": "release_x4c",
        "checker_target": "x4c",
        "filename": "papyrix-x4c.bin",
        "mcu": "ESP32-S3",
        "boards": ["X4Classic"],
        "panels": ["SSD1677", "UC8179_X4PRO", "UC8279_X4PRO"],
    },
    {
        "environment": "release_sticky",
        "checker_target": "sticky",
        "filename": "papyrix-sticky.bin",
        "mcu": "ESP32-S3",
        "boards": ["Sticky"],
        "panels": ["SSD1677", "SSD2677"],
        "install": {"board_key": "sticky", "display_name": "PapyriX Sticky",
                    "expected_tag": b"PAPYRIX-BOARD-V1:sticky;", "flash_bytes": 0x2000000, "flash_size": "32MB"},
    },
    {
        "environment": "release_murphy_m4",
        "checker_target": "murphy_m4",
        "filename": "papyrix-murphy-m4.bin",
        "mcu": "ESP32-S3",
        "boards": ["MurphyM4"],
        "panels": ["SSD1677"],
        "install": {"board_key": "murphy-m4", "display_name": "PapyriX Murphy M4",
                    "expected_tag": b"PAPYRIX-BOARD-V1:murphy_m4;", "flash_bytes": 0x1000000, "flash_size": "16MB"},
    },
)

INSTALL_LICENSES = {
    "LICENSE": "LICENSE",
    "FREEINK_LICENSE": "lib/EInkDisplay/FREEINK_LICENSE",
    "CROSSMUX_LICENSE": "src/core/CROSSMUX_LICENSE",
    "BOARDSUPPORT_FREEINK_LICENSE": "lib/BoardSupport/FREEINK_LICENSE",
}


def load_release_contract(root: Path):
    config = configparser.ConfigParser(interpolation=None)
    config.read(root / "platformio.ini", encoding="utf-8")
    version = config["papyrix"]["version"]
    flash_offset = config["base"]["board_upload.offset_address"]
    identity = (root / "lib/BoardSupport/include/HardwareIdentity.h").read_text(encoding="utf-8")
    match = re.search(r"kProfileSchemaVersion\s*=\s*(\d+)", identity)
    if match is None:
        raise RuntimeError("profile schema version is missing")
    return version, flash_offset, int(match.group(1))


def run_builds(root: Path):
    project_pio = root / ".venv/bin/pio"
    pio = str(project_pio) if project_pio.is_file() else shutil.which("pio")
    if pio is None:
        raise RuntimeError("PlatformIO executable is missing")
    command = [pio, "run"]
    for artifact in ARTIFACTS:
        command.extend(["-e", artifact["environment"]])
    subprocess.run(command, cwd=root, check=True)


def run_feature_checks(root: Path):
    toolchains = {
        "ESP32-C3": root / ".pio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm",
        "ESP32-S3": root / ".pio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm",
    }
    for artifact in ARTIFACTS:
        environment = artifact["environment"]
        elf = root / ".pio/build" / environment / "firmware.elf"
        symbols = root / ".pio/build" / environment / "firmware.symbols.txt"
        with symbols.open("wb") as output:
            subprocess.run([str(toolchains[artifact["mcu"]]), "-C", str(elf)], cwd=root, stdout=output, check=True)
        subprocess.run(
            [sys.executable, str(root / "scripts/check_target_features.py"), str(symbols), artifact["checker_target"]],
            cwd=root,
            check=True,
        )


def write_install_package(root: Path, output: Path, artifact: dict) -> Path:
    config = artifact["install"]
    filename = artifact["filename"]
    build = root / ".pio/build" / artifact["environment"]
    inputs = json.loads((build / "flash-inputs.json").read_text(encoding="utf-8"))
    if inputs.get("flash_size") != config["flash_size"] or not inputs.get("flash_mode") or not inputs.get("flash_frequency"):
        raise RuntimeError(f"{config['display_name']} flash parameters are missing or incompatible")
    parts = []
    payloads = {}
    for item in inputs["parts"]:
        source = Path(item["path"])
        offset = item["offset"]
        size = source.stat().st_size
        if (not source.is_absolute() or type(offset) is not int or offset < 0 or size <= 0
                or offset + size > config["flash_bytes"]):
            raise RuntimeError(f"invalid {config['display_name']} flash part: {source}")
        name = filename if source.name == "firmware.bin" else source.name
        if (name in payloads or name == "manifest.json" or name in INSTALL_LICENSES
                or (name == filename and size > 0x640000)):
            raise RuntimeError(f"reserved, duplicate, or oversized {config['display_name']} flash part: {name}")
        data = source.read_bytes()
        payloads[name] = data
        parts.append({"path": name, "offset": offset, "size": size, "sha256": hashlib.sha256(data).hexdigest()})
    parts.sort(key=lambda part: part["offset"])
    by_name = {part["path"]: part for part in parts}
    required = {"bootloader.bin", "partitions.bin", "boot_app0.bin", filename}
    if not required.issubset(by_name):
        raise RuntimeError(f"{config['display_name']} install requires bootloader, partitions, OTA data, and app")
    for first, second in zip(parts, parts[1:]):
        if first["offset"] + first["size"] > second["offset"]:
            raise RuntimeError(f"{config['display_name']} flash parts overlap")

    table = payloads["partitions.bin"]
    partitions = {}
    has_table_checksum = False
    for pos in range(0, len(table), 32):
        entry = table[pos:pos + 32]
        if len(entry) != 32:
            raise RuntimeError(f"truncated {config['display_name']} partition table")
        magic = struct.unpack_from("<H", entry)[0]
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            if (entry[2:16] != b"\xFF" * 14 or entry[16:] != hashlib.md5(table[:pos]).digest()
                    or any(byte != 0xFF for byte in table[pos + 32:])):
                raise RuntimeError(f"{config['display_name']} partition table checksum mismatch")
            has_table_checksum = True
            break
        if magic != 0x50AA:
            raise RuntimeError(f"invalid {config['display_name']} partition table")
        _, kind, subtype, offset, size, label, _ = struct.unpack("<HBBII16sI", entry)
        label = label.split(b"\0", 1)[0].decode("ascii")
        if label in partitions or size == 0 or offset < 0x9000 or offset + size > 0x1000000:
            raise RuntimeError(f"invalid {config['display_name']} partition bounds")
        partitions[label] = (kind, subtype, offset, size)
    if not has_table_checksum:
        raise RuntimeError("Install partition table requires its generated checksum")
    for label, expected in {
        "nvs": (1, 2, 0x9000, 0x5000),
        "app0": (0, 0x10, 0x10000, 0x640000),
        "app1": (0, 0x11, 0x650000, 0x640000),
        "otadata": (1, 0, 0xE000, 0x2000),
        "spiffs": (1, 0x82, 0xC90000, 0x360000),
        "coredump": (1, 3, 0xFF0000, 0x10000),
    }.items():
        if partitions.get(label) != expected:
            raise RuntimeError(f"{config['display_name']} partition layout disagrees with {label}")
    ordered = sorted(partitions.values(), key=lambda part: part[2])
    for first, second in zip(ordered, ordered[1:]):
        if first[2] + first[3] > second[2]:
            raise RuntimeError(f"{config['display_name']} partitions overlap")
    if (by_name["bootloader.bin"]["offset"] != 0 or by_name["partitions.bin"]["offset"] != 0x8000
            or by_name["partitions.bin"]["offset"] + len(table) > ordered[0][2]
            or by_name["boot_app0.bin"]["offset"] != partitions["otadata"][2]
            or by_name["boot_app0.bin"]["size"] > partitions["otadata"][3]
            or by_name[filename]["offset"] != partitions["app0"][2]):
        raise RuntimeError(f"{config['display_name']} upload offsets disagree with generated partitions")

    app = payloads[filename]
    if len(app) < 24 or app[0] != 0xE9 or int.from_bytes(app[12:14], "little") != 9:
        raise RuntimeError(f"{config['display_name']} app is not an ESP32-S3 image")
    pos = 24
    matches = 0
    checksum = 0xEF
    magic = b"PAPYRIX-BOARD-V1:"
    tag = config["expected_tag"]
    for _ in range(app[1]):
        if pos + 8 > len(app):
            raise RuntimeError(f"truncated {config['display_name']} app segment header")
        length = struct.unpack_from("<I", app, pos + 4)[0]
        pos += 8
        end = pos + length
        if end > len(app):
            raise RuntimeError(f"truncated {config['display_name']} app segment")
        found = app.find(magic, pos, end)
        while found != -1:
            if not app.startswith(tag, found) or app.find(b";", found + len(magic), end) != found + len(tag) - 1:
                raise RuntimeError(f"{config['display_name']} app has a malformed or incompatible board tag")
            matches += 1
            found = app.find(magic, found + len(magic), end)
        for byte in memoryview(app)[pos:end]:
            checksum ^= byte
        pos = end
    pad_end = (pos + 16) & ~15
    has_hash = app[23] != 0
    if len(app) != pad_end + (32 if has_hash else 0) or app[pad_end - 1] != checksum:
        raise RuntimeError(f"{config['display_name']} app size or checksum mismatch")
    if has_hash and hashlib.sha256(memoryview(app)[:pad_end]).digest() != app[pad_end:]:
        raise RuntimeError(f"{config['display_name']} app hash mismatch")
    if matches != 1:
        raise RuntimeError(f"{config['display_name']} app requires exactly one matching board tag")

    version, _, _ = load_release_contract(root)
    manifest = {
        "name": config["display_name"],
        "version": version,
        "flashSize": inputs["flash_size"],
        "flashMode": inputs["flash_mode"],
        "flashFrequency": inputs["flash_frequency"],
        "new_install_prompt_erase": True,
        "warning": "A complete installation erases stock firmware and device data. Save a full flash backup first. "
                   "Stock recovery is not guaranteed.",
        "builds": [{"chipFamily": "ESP32-S3", "parts": parts}],
    }
    payloads["manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    for name, relative in INSTALL_LICENSES.items():
        payloads[name] = (root / relative).read_bytes()
    install = output / f"{config['board_key']}-install"
    install.mkdir(parents=True, exist_ok=True)
    for name, data in payloads.items():
        (install / name).write_bytes(data)
    with zipfile.ZipFile(output / f"papyrix-{config['board_key']}-install.zip", "w") as archive:
        for name in sorted(payloads):
            info = zipfile.ZipInfo(name)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, payloads[name])
    return install / "manifest.json"


def package(root: Path, output: Path, build: bool = True, check_features: bool = True):
    root = root.resolve()
    output = output.resolve()
    if build:
        run_builds(root)
    if check_features:
        run_feature_checks(root)

    version, flash_offset, profile_schema = load_release_contract(root)
    output.mkdir(parents=True, exist_ok=True)
    manifest_artifacts = []
    for artifact in ARTIFACTS:
        source = root / ".pio/build" / artifact["environment"] / "firmware.bin"
        if not source.is_file():
            raise FileNotFoundError(f"missing firmware build: {source}")
        destination = output / artifact["filename"]
        shutil.copyfile(source, destination)
        digest = hashlib.sha256(destination.read_bytes()).hexdigest()
        manifest_artifacts.append(
            {
                "filename": artifact["filename"],
                "environment": artifact["environment"],
                "checker_target": artifact["checker_target"],
                "mcu": artifact["mcu"],
                "boards": artifact["boards"],
                "panels": artifact["panels"],
                "sha256": digest,
            }
        )

    manifest = {
        "version": version,
        "flash_offset": flash_offset,
        "profile_schema": profile_schema,
        "artifacts": manifest_artifacts,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for artifact in ARTIFACTS:
        if "install" in artifact:
            write_install_package(root, output, artifact)
    return manifest


def main():
    parser = argparse.ArgumentParser(description="Build and package deterministic PapyriX firmware artifacts")
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--no-feature-check", action="store_true")
    args = parser.parse_args()
    root = args.project_root.resolve()
    output = args.output.resolve() if args.output else root / "dist"
    package(root, output, build=not args.no_build, check_features=not args.no_feature_check)
    print(f"packaged {len(ARTIFACTS)} artifacts in {output}")
    print("WARNING: A complete device installation erases stock firmware and device data. Stock recovery is not guaranteed.")


if __name__ == "__main__":
    main()
