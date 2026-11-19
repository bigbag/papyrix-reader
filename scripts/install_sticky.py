#!/usr/bin/env python3
"""Build and install PapyriX on a confirmed Seeed reTerminal Sticky."""

import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path

from package_firmware import ARTIFACTS, write_install_package
from select_usb_upload_port import select_port


def main():
    root = Path(__file__).resolve().parents[1]
    artifact = next(item for item in ARTIFACTS if item["environment"] == "release_sticky")
    esptool = shutil.which("esptool")
    if esptool is None:
        raise RuntimeError("Install esptool 5 before you run this command.")
    subprocess.run(["pio", "run", "-e", artifact["environment"]], cwd=root, check=True)
    with tempfile.TemporaryDirectory(prefix="papyrix-sticky-install-") as directory:
        manifest_path = write_install_package(root, Path(directory), artifact)
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        list_ports = ["pio", "device", "list", "--json-output"]
        devices = json.loads(subprocess.check_output(list_ports, text=True))
        port = select_port(devices, "sticky", os.environ.get("PLATFORMIO_UPLOAD_PORT", ""))
        print(f"Confirm that {port} connects to a Seeed reTerminal Sticky.")
        print("This replaces the firmware and clears internal LittleFS. It does not erase the SD card.")
        if input("Type install to continue: ").strip() != "install":
            raise RuntimeError("Installation canceled. No flash writes occurred.")
        select_port(json.loads(subprocess.check_output(list_ports, text=True)), "sticky", port)
        command = [
            esptool, "--chip", "esp32s3", "--port", port, "--baud", "460800", "write-flash",
            "--flash-size", manifest["flashSize"], "--flash-mode", manifest["flashMode"],
            "--flash-freq", manifest["flashFrequency"],
        ]
        for part in manifest["builds"][0]["parts"]:
            command.extend([hex(part["offset"]), part["path"]])
        subprocess.run(command, cwd=manifest_path.parent, check=True)


if __name__ == "__main__":
    try:
        main()
    except (EOFError, KeyboardInterrupt):
        raise SystemExit(1) from None
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
