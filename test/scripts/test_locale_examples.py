#!/usr/bin/env python3
import contextlib
import io
import tempfile
from pathlib import Path
from unittest.mock import patch

import LocaleExamplesTest as validator


def main():
    expected = {"GO_PLAY", "LIGHTS_OUT", "GAME_MOVES", "SOKOBAN", "TITLE", "LANDSCAPE_CW", "LANDSCAPE_CCW",
                "HOLD_CONFIRM_MENU", "SAVE_EXIT", "UNDO", "NEXT"}
    base = ("GO_PLAY=Go\nLIGHTS_OUT=Lights Out\nGAME_MOVES=Moves\nSOKOBAN=Sokoban\nTITLE=Title\n"
            "LANDSCAPE_CW=Right\nLANDSCAPE_CCW=Left\nHOLD_CONFIRM_MENU=Hold\nSAVE_EXIT=Save\nUNDO=Undo\nNEXT=Next\n")
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)

        def check(name, content, result, message):
            path = root / name
            path.write_text(content, encoding="utf-8")
            output = io.StringIO()
            with patch.object(validator, "ROOT", root), patch.object(validator, "LOCALE_DIR", root), \
                    patch.object(validator, "i18n_keys", return_value=expected), contextlib.redirect_stdout(output):
                actual = validator.main()
            assert actual == result, output.getvalue()
            assert message in output.getvalue(), output.getvalue()
            path.unlink()

        check("fr.txt", base.replace("GO_PLAY=Go\n", ""), 0, "PASS:")
        check("en.txt", base.replace("GO_PLAY=Go\n", ""), 1, "missing: GO_PLAY")
        check("fr.txt", base.replace("LIGHTS_OUT=Lights Out\n", "").replace("GAME_MOVES=Moves\n", "")
              .replace("SOKOBAN=Sokoban\n", ""), 0, "PASS:")
        check("en.txt", base.replace("LIGHTS_OUT=Lights Out\n", ""), 1, "missing: LIGHTS_OUT")
        check("fr.txt", base.replace("TITLE=Title\n", ""), 1, "missing: TITLE")
        shared = {"HOLD_CONFIRM_MENU", "SAVE_EXIT", "UNDO", "NEXT"}
        content = "\n".join(line for line in base.splitlines() if line.split("=", 1)[0] not in shared) + "\n"
        check("fr.txt", content, 0, "PASS:")
        check("en.txt", content, 1, "missing:")
        check("fr.txt", base + "UNKNOWN=value\n", 1, "unexpected: UNKNOWN")
        check("fr.txt", base.replace("LANDSCAPE_CCW=Left", "LANDSCAPE_CCW=Right"),
              1, "must have distinct labels")
        # Three fixed values use 14 bytes including their terminators.
        boundary = "GO_PLAY=Go\nLANDSCAPE_CW=Right\nLANDSCAPE_CCW=Left\nTITLE="
        check("fr.txt", boundary + "é" * 2296 + "x\n_METADATA=" + "a" * 5000,
              0, "4608/4608 bytes")
        check("fr.txt", boundary + "é" * 2296 + "xx\n", 1, "override bytes: 4609/4608")


if __name__ == "__main__":
    main()
