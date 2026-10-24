#!/usr/bin/env python3
import copy
import importlib.util
import json
import struct
import tempfile
import zipfile
from hashlib import md5, sha256
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_firmware", ROOT / "scripts/package_firmware.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

PARTITIONS = [
    ("nvs", 1, 2, 0x9000, 0x5000),
    ("otadata", 1, 0, 0xE000, 0x2000),
    ("app0", 0, 0x10, 0x10000, 0x640000),
    ("app1", 0, 0x11, 0x650000, 0x640000),
    ("spiffs", 1, 0x82, 0xC90000, 0x360000),
    ("coredump", 1, 3, 0xFF0000, 0x10000),
]
LICENSES = {
    "LICENSE": "LICENSE",
    "FREEINK_LICENSE": "lib/EInkDisplay/FREEINK_LICENSE",
    "CROSSMUX_LICENSE": "src/core/CROSSMUX_LICENSE",
    "BOARDSUPPORT_FREEINK_LICENSE": "lib/BoardSupport/FREEINK_LICENSE",
}


def s3_app(tag, chip=9, split=False):
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 2 if split else 1])
    struct.pack_into("<H", header, 12, chip)
    header[23] = 1
    payload = bytearray(64 * 1024)
    payload[4090:4090 + len(tag)] = tag
    if split:
        image = header + struct.pack("<II", 0x3C000020, 4097) + payload[:4097]
        image += struct.pack("<II", 0x3C001021, len(payload) - 4097) + payload[4097:]
    else:
        image = header + struct.pack("<II", 0x3C000020, len(payload)) + payload
    checksum = 0xEF
    for byte in payload:
        checksum ^= byte
    image += bytes(((len(image) + 16) & ~15) - len(image) - 1) + bytes([checksum])
    return bytes(image) + sha256(image).digest()


def partition_table(entries=PARTITIONS, checksum=True):
    table = b"".join(struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, label.encode(), 0)
                     for label, kind, subtype, offset, size in entries)
    if checksum:
        table += b"\xEB\xEB" + b"\xFF" * 14 + md5(table).digest()
    return table.ljust(0xC00, b"\xFF")


def expect_rejection(action, case):
    try:
        action()
    except (FileNotFoundError, RuntimeError):
        return
    raise AssertionError(f"{case} must fail packaging")


with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / "lib/BoardSupport/include").mkdir(parents=True)
    (root / "lib/BoardSupport/include/HardwareIdentity.h").write_text(
        "inline constexpr uint8_t kProfileSchemaVersion = 7;\n", encoding="utf-8")
    (root / "platformio.ini").write_text(
        "[papyrix]\nversion = 9.8.7\n[base]\nboard_upload.offset_address = 0x10000\n", encoding="utf-8")
    for relative in LICENSES.values():
        destination = root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT / relative).read_bytes())
    payloads = {
        "release_xteink_c3": b"c3-image",
        "release_x4pro": b"x4pro-image",
        "release_x4c": b"classic-image",
        "release_sticky": s3_app(b"PAPYRIX-BOARD-V1:sticky;"),
        "release_murphy_m4": s3_app(b"PAPYRIX-BOARD-V1:murphy_m4;"),
    }
    for environment, payload in payloads.items():
        build = root / ".pio/build" / environment
        build.mkdir(parents=True)
        (build / "firmware.bin").write_bytes(payload)
    output = root / "dist"
    first_archives = {}
    for environment, key, display_name, flash_size, flash_bytes, tag in (
        ("release_sticky", "sticky", "PapyriX Sticky", "32MB", 0x2000000, b"PAPYRIX-BOARD-V1:sticky;"),
        ("release_murphy_m4", "murphy-m4", "PapyriX Murphy M4", "16MB", 0x1000000,
         b"PAPYRIX-BOARD-V1:murphy_m4;"),
    ):
        artifact = next(item for item in module.ARTIFACTS if item["environment"] == environment)
        build = root / ".pio/build" / environment
        bootloader = build / "bootloader.bin"
        app = build / "firmware.bin"
        inputs = {
            "flash_size": flash_size, "flash_mode": "dio", "flash_frequency": "80m",
            "parts": [{"path": str(build / name), "offset": offset}
                      for name, offset in [("bootloader.bin", 0), ("partitions.bin", 0x8000),
                                           ("boot_app0.bin", 0xE000), ("firmware.bin", 0x10000)]],
        }
        inputs_path = build / "flash-inputs.json"

        def restore():
            bootloader.write_bytes(b"B" * 0x6000)
            app.write_bytes(payloads[environment])
            (build / "partitions.bin").write_bytes(partition_table())
            (build / "boot_app0.bin").write_bytes(b"\xFF" * 0x2000)
            inputs_path.write_text(json.dumps(inputs))

        restore()
        install = module.write_install_package(root, output, artifact)
        manifest = json.loads(install.read_text())
        assert manifest["name"] == display_name and manifest["version"] == "9.8.7"
        assert manifest["flashSize"] == flash_size and manifest["flashMode"] == "dio"
        assert manifest["flashFrequency"] == "80m" and manifest["new_install_prompt_erase"] is True
        assert manifest["builds"][0]["chipFamily"] == "ESP32-S3"
        parts = sorted(manifest["builds"][0]["parts"], key=lambda part: part["offset"])
        assert {part["offset"] for part in parts} == {0, 0x8000, 0xE000, 0x10000}
        for part in parts:
            assert not Path(part["path"]).is_absolute() and ".." not in Path(part["path"]).parts
            data = (install.parent / part["path"]).read_bytes()
            assert part["size"] == len(data) and part["sha256"] == sha256(data).hexdigest()
            assert 0 <= part["offset"] < part["offset"] + len(data) <= flash_bytes
        for first, second in zip(parts, parts[1:]):
            assert first["offset"] + first["size"] <= second["offset"]
        assert (install.parent / f"papyrix-{key}.bin").read_bytes() == payloads[environment]
        archive = output / f"papyrix-{key}-install.zip"
        first_archives[archive.name] = archive.read_bytes()
        with zipfile.ZipFile(archive) as package:
            assert {"manifest.json"} | set(LICENSES) | {part["path"] for part in parts} == set(package.namelist())
            assert json.loads(package.read("manifest.json")) == manifest
            for name, relative in LICENSES.items():
                assert package.read(name) == (ROOT / relative).read_bytes()
            for name in package.namelist():
                assert not Path(name).is_absolute() and ".." not in Path(name).parts

        for fault in ("missing bootloader", "overlap", "flash bound", "oversized app", "wrong board",
                      "missing tag", "duplicate tag", "malformed tag", "split tag", "wrong chip",
                      "checksum", "hash", "truncated app", "missing boot input", "partition mismatch",
                      "partition checksum", "missing partition checksum", "hidden partition",
                      "filesystem layout", "coredump layout", "stock layout", "reserved name", "wrong flash size"):
            restore()
            bad_inputs = copy.deepcopy(inputs)
            if fault == "missing bootloader":
                bootloader.unlink()
            elif fault == "overlap":
                bad_inputs["parts"][1]["offset"] = 0x100
            elif fault == "flash bound":
                bad_inputs["parts"][0]["offset"] = flash_bytes
            elif fault == "oversized app":
                app.write_bytes(b"\0" * (0x640000 + 1))
            elif fault == "wrong board":
                app.write_bytes(s3_app(b"PAPYRIX-BOARD-V1:x4pro;"))
            elif fault == "missing tag":
                app.write_bytes(s3_app(b""))
            elif fault == "duplicate tag":
                app.write_bytes(s3_app(tag * 2))
            elif fault == "malformed tag":
                app.write_bytes(s3_app(tag[:-1]))
            elif fault == "split tag":
                app.write_bytes(s3_app(tag, split=True))
            elif fault == "wrong chip":
                app.write_bytes(s3_app(tag, chip=5))
            elif fault in ("checksum", "hash", "truncated app"):
                damaged = bytearray(s3_app(tag))
                if fault == "truncated app":
                    damaged = damaged[:-8]
                else:
                    damaged[-33 if fault == "checksum" else -1] ^= 1
                app.write_bytes(damaged)
            elif fault == "missing boot input":
                bad_inputs["parts"].pop(0)
            elif fault == "partition mismatch":
                entries = copy.deepcopy(PARTITIONS)
                entries[2] = ("app0", 0, 0x10, 0x20000, 0x640000)
                (build / "partitions.bin").write_bytes(partition_table(entries))
            elif fault == "partition checksum":
                changed = bytearray(partition_table())
                changed[len(PARTITIONS) * 32 + 16] ^= 1
                (build / "partitions.bin").write_bytes(changed)
            elif fault == "missing partition checksum":
                (build / "partitions.bin").write_bytes(partition_table(checksum=False))
            elif fault == "hidden partition":
                changed = bytearray(partition_table())
                changed[(len(PARTITIONS) + 1) * 32] = 0
                (build / "partitions.bin").write_bytes(changed)
            elif fault in ("filesystem layout", "coredump layout", "stock layout"):
                entries = copy.deepcopy(PARTITIONS)
                if fault == "filesystem layout":
                    entries[4] = ("spiffs", 1, 0x82, 0xC90000, 0x350000)
                elif fault == "coredump layout":
                    entries[5] = ("coredump", 1, 3, 0xFF0000, 0xF000)
                else:
                    entries[2] = ("app0", 0, 0x10, 0x20000, 0x200000)
                    entries[3] = ("app1", 0, 0x11, 0x220000, 0x200000)
                (build / "partitions.bin").write_bytes(partition_table(entries))
            elif fault == "reserved name":
                reserved = build / "manifest.json"
                reserved.write_bytes(b"not a flash image")
                bad_inputs["parts"].append({"path": str(reserved), "offset": 0xF00000})
            elif fault == "wrong flash size":
                bad_inputs["flash_size"] = "32MB" if flash_size == "16MB" else "16MB"
            inputs_path.write_text(json.dumps(bad_inputs))
            before = archive.read_bytes()
            expect_rejection(lambda: module.write_install_package(root, output, artifact), f"{key}: {fault}")
            assert archive.read_bytes() == before, f"{key}: rejected input must not replace the valid ZIP"
        restore()
        edge = build / "boundary.bin"
        edge.write_bytes(b"X")
        edge_inputs = copy.deepcopy(inputs)
        edge_inputs["parts"].append({"path": str(edge), "offset": flash_bytes - 1})
        inputs_path.write_text(json.dumps(edge_inputs))
        at_bound = module.write_install_package(root, output, artifact)
        edge_part = next(part for part in json.loads(at_bound.read_text())["builds"][0]["parts"]
                         if part["path"] == "boundary.bin")
        assert edge_part["offset"] + edge_part["size"] == flash_bytes
        edge.write_bytes(b"XX")
        expect_rejection(lambda: module.write_install_package(root, output, artifact), f"{key}: bound plus one")
        restore()
        module.write_install_package(root, output, artifact)
        assert archive.read_bytes() == first_archives[archive.name]
        print(f"PASS: {key} manifest, payloads, licenses, corruption rejection, and exact flash boundary")

    module.package(root, output, build=False, check_features=False)
    first_manifest = (output / "manifest.json").read_bytes()
    module.package(root, output, build=False, check_features=False)
    assert (output / "manifest.json").read_bytes() == first_manifest
    manifest = json.loads(first_manifest)
    assert manifest["version"] == "9.8.7" and manifest["profile_schema"] == 7
    assert manifest["flash_offset"] == "0x10000"
    assert {entry["environment"] for entry in manifest["artifacts"]} == set(payloads)
    for name, data in first_archives.items():
        assert (output / name).read_bytes() == data
    for entry in manifest["artifacts"]:
        payload = payloads[entry["environment"]]
        assert entry["sha256"] == sha256(payload).hexdigest()
        assert (output / entry["filename"]).read_bytes() == payload
    by_name = {entry["filename"]: entry for entry in manifest["artifacts"]}
    assert by_name["papyrix-x4c.bin"]["panels"] == ["SSD1677", "UC8179_X4PRO", "UC8279_X4PRO"]
    assert by_name["papyrix-sticky.bin"]["boards"] == ["Sticky"]
    assert by_name["papyrix-murphy-m4.bin"]["boards"] == ["MurphyM4"]
    assert by_name["papyrix-murphy-m4.bin"]["panels"] == ["SSD1677"]
    assert by_name["papyrix-murphy-m4.bin"]["mcu"] == "ESP32-S3"
    (root / ".pio/build/release_x4c/firmware.bin").unlink()
    expect_rejection(lambda: module.package(root, output, build=False, check_features=False), "missing Classic input")

print("package firmware tests passed")
