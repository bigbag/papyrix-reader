#!/usr/bin/env python3
import importlib.util
import json
import struct
import tempfile
import zipfile
from hashlib import md5, sha256
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "package_firmware.py"

spec = importlib.util.spec_from_file_location("package_firmware", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def sticky_app(tag=b"PAPYRIX-BOARD-V1:sticky;", chip=9):
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    struct.pack_into("<H", header, 12, chip)
    header[23] = 1
    payload = bytearray(64 * 1024)
    payload[4090:4090 + len(tag)] = tag
    image = header + struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    image += bytes(((len(image) + 16) & ~15) - len(image) - 1) + bytes([checksum])
    return bytes(image) + sha256(image).digest()

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "lib/BoardSupport/include").mkdir(parents=True)
    (root / "lib/BoardSupport/include/HardwareIdentity.h").write_text(
        "inline constexpr uint8_t kProfileSchemaVersion = 7;\n", encoding="utf-8"
    )
    (root / "platformio.ini").write_text(
        "[papyrix]\nversion = 9.8.7\n[base]\nboard_upload.offset_address = 0x10000\n", encoding="utf-8"
    )
    for relative in ("lib/EInkDisplay/FREEINK_LICENSE", "src/core/CROSSMUX_LICENSE"):
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT / relative).read_bytes())
    payloads = {
        "release_xteink_c3": b"c3-image",
        "release_x4pro": b"x4pro-image",
        "release_x4c": b"classic-image",
        "release_sticky": sticky_app(),
    }
    for environment, payload in payloads.items():
        build = root / ".pio/build" / environment
        build.mkdir(parents=True)
        (build / "firmware.bin").write_bytes(payload)

    sticky_build = root / ".pio/build/release_sticky"
    bootloader = sticky_build / "bootloader.bin"
    bootloader.write_bytes(b"B" * 0x6000)
    table = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, label.encode(), 0)
                     for label, kind, subtype, offset, size in [
                         ("nvs", 1, 2, 0x9000, 0x5000),
                         ("otadata", 1, 0, 0xE000, 0x2000),
                         ("app0", 0, 0x10, 0x10000, 0x640000),
                         ("app1", 0, 0x11, 0x650000, 0x640000),
                         ("spiffs", 1, 0x82, 0xC90000, 0x360000),
                         ("coredump", 1, 3, 0xFF0000, 0x10000),
                     ])
    table += b"\xEB\xEB" + b"\xFF" * 14 + md5(table).digest()
    (sticky_build / "partitions.bin").write_bytes(table.ljust(0xC00, b"\xFF"))
    (sticky_build / "boot_app0.bin").write_bytes(b"\xFF" * 0x2000)
    inputs = {
        "flash_size": "32MB", "flash_mode": "dio", "flash_frequency": "80m",
        "parts": [{"path": str(sticky_build / name), "offset": offset}
                  for name, offset in [("bootloader.bin", 0), ("partitions.bin", 0x8000),
                                       ("boot_app0.bin", 0xE000), ("firmware.bin", 0x10000)]],
    }
    inputs_path = sticky_build / "sticky-flash-inputs.json"
    inputs_path.write_text(json.dumps(inputs))
    output = root / "dist"
    install = module.write_sticky_install_package(root, output)
    install_manifest = json.loads(install.read_text())
    assert install_manifest["flashSize"] == "32MB"
    assert install_manifest["flashMode"] == "dio"
    assert install_manifest["flashFrequency"] == "80m"
    assert install_manifest["new_install_prompt_erase"] is True
    assert install_manifest["builds"][0]["chipFamily"] == "ESP32-S3"
    parts = sorted(install_manifest["builds"][0]["parts"], key=lambda part: part["offset"])
    assert {part["offset"] for part in parts} == {0, 0x8000, 0xE000, 0x10000}
    for part in parts:
        assert not Path(part["path"]).is_absolute() and ".." not in Path(part["path"]).parts
        data = (install.parent / part["path"]).read_bytes()
        assert part["size"] == len(data)
        assert part["sha256"] == sha256(data).hexdigest()
        assert 0 <= part["offset"] < part["offset"] + len(data) <= 0x2000000
    for first, second in zip(parts, parts[1:]):
        assert first["offset"] + first["size"] <= second["offset"]
    archive = output / "papyrix-sticky-install.zip"
    first_archive = archive.read_bytes()
    with zipfile.ZipFile(archive) as package:
        assert {"manifest.json"} | {part["path"] for part in parts} <= set(package.namelist())
        for name in package.namelist():
            assert not Path(name).is_absolute() and ".." not in Path(name).parts
        assert json.loads(package.read("manifest.json")) == install_manifest
    module.package(root, output, build=False, check_features=False)
    first_manifest = (output / "manifest.json").read_bytes()
    module.package(root, output, build=False, check_features=False)
    assert (output / "manifest.json").read_bytes() == first_manifest

    manifest = json.loads(first_manifest)
    assert manifest["version"] == "9.8.7"
    assert manifest["profile_schema"] == 7
    assert manifest["flash_offset"] == "0x10000"
    assert {entry["environment"] for entry in manifest["artifacts"]} == set(payloads)
    assert archive.read_bytes() == first_archive
    for entry in manifest["artifacts"]:
        payload = payloads[entry["environment"]]
        assert entry["sha256"] == sha256(payload).hexdigest()
        assert (output / entry["filename"]).read_bytes() == payload
    by_name = {entry["filename"]: entry for entry in manifest["artifacts"]}
    classic = by_name["papyrix-x4c.bin"]
    assert classic["boards"] == ["X4Classic"]
    assert classic["panels"] == ["SSD1677", "UC8179_X4PRO", "UC8279_X4PRO"]
    sticky = by_name["papyrix-sticky.bin"]
    assert sticky["boards"] == ["Sticky"] and sticky["panels"] == ["SSD1677", "SSD2677"]
    assert sticky["mcu"] == "ESP32-S3"

    app = sticky_build / "firmware.bin"
    for fault in ("missing bootloader", "overlap", "flash bound", "oversized app", "wrong board",
                  "missing tag", "duplicate tag", "wrong chip", "missing boot input", "partition mismatch",
                  "reserved name"):
        bootloader.write_bytes(b"B" * 0x6000)
        app.write_bytes(payloads["release_sticky"])
        (sticky_build / "partitions.bin").write_bytes(table.ljust(0xC00, b"\xFF"))
        bad_inputs = json.loads(json.dumps(inputs))
        if fault == "missing bootloader":
            bootloader.unlink()
        elif fault == "overlap":
            bad_inputs["parts"][1]["offset"] = 0x100
        elif fault == "flash bound":
            bad_inputs["parts"][0]["offset"] = 0x2000000
        elif fault == "oversized app":
            app.write_bytes(b"\0" * (0x640000 + 1))
        elif fault == "wrong board":
            app.write_bytes(sticky_app(b"PAPYRIX-BOARD-V1:x4pro;"))
        elif fault == "missing tag":
            app.write_bytes(sticky_app(b""))
        elif fault == "duplicate tag":
            app.write_bytes(sticky_app(b"PAPYRIX-BOARD-V1:sticky;" * 2))
        elif fault == "wrong chip":
            app.write_bytes(sticky_app(chip=5))
        elif fault == "missing boot input":
            bad_inputs["parts"].pop(0)
        elif fault == "partition mismatch":
            changed = bytearray(table.ljust(0xC00, b"\xFF"))
            struct.pack_into("<I", changed, 2 * 32 + 4, 0x20000)
            (sticky_build / "partitions.bin").write_bytes(changed)
        elif fault == "reserved name":
            reserved = sticky_build / "manifest.json"
            reserved.write_bytes(b"not a flash image")
            bad_inputs["parts"].append({"path": str(reserved), "offset": 0x1000000})
        inputs_path.write_text(json.dumps(bad_inputs))
        try:
            module.write_sticky_install_package(root, output)
        except (FileNotFoundError, RuntimeError):
            pass
        else:
            raise AssertionError(f"{fault} must fail packaging")
    bootloader.write_bytes(b"B" * 0x6000)
    app.write_bytes(payloads["release_sticky"])
    (sticky_build / "partitions.bin").write_bytes(table.ljust(0xC00, b"\xFF"))
    inputs_path.write_text(json.dumps(inputs))
    (root / ".pio/build/release_x4c/firmware.bin").unlink()
    try:
        module.package(root, output, build=False, check_features=False)
    except (FileNotFoundError, RuntimeError):
        pass
    else:
        raise AssertionError("missing Classic input must fail packaging")

print("package firmware tests passed")
