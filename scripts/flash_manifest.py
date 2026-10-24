import json
from pathlib import Path

Import("env")


def write_flash_manifest(source, target, env):
    images = list(env.get("FLASH_EXTRA_IMAGES", []))
    images.append((env.subst("$ESP32_APP_OFFSET"), env.subst("$BUILD_DIR/${PROGNAME}.bin")))
    manifest = {
        "flash_size": env.BoardConfig().get("upload.flash_size"),
        "flash_mode": env.subst("${__get_board_flash_mode(__env__)}"),
        "flash_frequency": env.subst("${__get_board_f_image(__env__)}"),
        "parts": [
            {"path": str(Path(env.subst(str(path))).resolve()), "offset": int(env.subst(str(offset)), 0)}
            for offset, path in images
        ],
    }
    output = Path(env.subst("$BUILD_DIR")) / "flash-inputs.json"
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


# Record current inputs even when the app binary does not change.
env.AddPostAction("buildprog", write_flash_manifest)
