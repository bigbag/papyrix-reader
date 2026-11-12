"""Backport bounded WebServer parsing without changing installed packages."""

import hashlib
from pathlib import Path
import re


PARSER_SHA256 = "117ba2a370abf95b7367fff234f9ba2ee42efbb1842df752e7e488c26b04f54b"
if "Import" in globals():
    Import("env")  # noqa: F821
    project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
else:
    project = Path(__file__).resolve().parents[1]
PATCH = project / "scripts/patches/arduino-esp32-3.3.7-webserver.patch"


def patch_parser(source: str) -> str:
    if hashlib.sha256(source.encode("utf-8")).hexdigest() != PARSER_SHA256:
        raise RuntimeError("WebServer parser differs from Arduino-ESP32 3.3.7; review the backport before building")
    original = source.splitlines(keepends=True)
    hunks = re.split(r"^@@ -(\d+)(?:,\d+)? \+\d+(?:,\d+)? @@[^\n]*\n", PATCH.read_text(encoding="utf-8"), flags=re.MULTILINE)
    result = []
    cursor = 0
    for start, hunk in zip(hunks[1::2], hunks[2::2]):
        start = int(start) - 1
        result.extend(original[cursor:start])
        cursor = start
        for line in hunk.splitlines(keepends=True):
            if line[0] in " -":
                if cursor >= len(original) or original[cursor] != line[1:]:
                    raise RuntimeError("WebServer backport context does not match")
                cursor += 1
            if line[0] in " +":
                result.append(line[1:])
    result.extend(original[cursor:])
    return "".join(result)


def replace_parser(target, source, env):
    original = Path(source[0].srcnode().get_abspath())
    if not original.match("*/WebServer/src/Parsing.cpp"):
        return target, source
    patched = patch_parser(original.read_text(encoding="utf-8"))
    generated = Path(env.subst("$BUILD_DIR")) / "patched-webserver/Parsing.cpp"
    generated.parent.mkdir(parents=True, exist_ok=True)
    if not generated.exists() or generated.read_text(encoding="utf-8") != patched:
        generated.write_text(patched, encoding="utf-8")
    env.AppendUnique(CPPPATH=[str(original.parent)])
    return target, [env.File(str(generated))]


# Compiler setup replaces emitters, so install this one from middleware.
def install_parser_emitter(env, node):
    from SCons.Builder import ListEmitter

    builder = env["BUILDERS"]["StaticObject"]
    emitter = builder.emitter[".cpp"]
    if not isinstance(emitter, ListEmitter) or replace_parser not in emitter:
        builder.add_emitter(".cpp", ListEmitter([emitter, replace_parser]))
    return node


if "Import" in globals():
    env.AddBuildMiddleware(install_parser_emitter, "*/WebServer/src/Parsing.cpp")  # noqa: F821
