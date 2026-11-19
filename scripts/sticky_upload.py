"""Check the USB port and installed partition table before a Sticky upload."""

import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory


def check_partition_table(esptool, port, partitions):
    if len(esptool) >= 2 and esptool[0] == esptool[-1] == '"':
        esptool = esptool[1:-1]
    expected = partitions.read_bytes()
    with TemporaryDirectory(prefix="papyrix-sticky-check-") as directory:
        current = Path(directory) / "partitions.bin"
        subprocess.run([esptool, "--chip", "esp32s3", "--port", port, "--baud", "460800",
                        "read-flash", "0x8000", str(len(expected)), str(current)], check=True)
        if current.read_bytes() != expected:
            raise RuntimeError("Sticky partition table does not match PapyriX. "
                               "Use make install-sticky for a first installation.")


def check_sticky_upload(source, target, env):
    try:
        port = select_port(list_serial_ports(), "sticky", env.subst("$UPLOAD_PORT"))
        partitions = Path(env.subst("$BUILD_DIR")) / "partitions.bin"
        check_partition_table(env.subst("$ERASETOOL"), port, partitions)
        env.Replace(UPLOAD_PORT=port)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Sticky upload stopped: {error}", file=sys.stderr)
        return 1


if "Import" in globals():
    Import("env")  # noqa: F821
    from platformio.device.list.util import list_serial_ports
    from select_usb_upload_port import select_port

    env.AddPreAction("upload", check_sticky_upload)  # noqa: F821
