#!/usr/bin/env python3
"""Compile a consumer through normal and pinned Windows parser dispatchers."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
SCONS = ROOT / ".pio/packages/tool-scons/scons.py"
PARSER = '#include "fixture_limit.h"\nbool accepts(int length) { return true; }\n'
PATCH = '''@@ -1,2 +1,2 @@
 #include "fixture_limit.h"
-bool accepts(int length) { return true; }
+bool accepts(int length) { return length <= fixture_limit; }
'''
CONSUMER = '''bool accepts(int length);
int main() { return accepts(0) && accepts(4) && !accepts(5) ? 0 : 1; }
'''
SCONSTRUCT = r'''
import ast
import fnmatch
import hashlib
import importlib.util
import os
from pathlib import Path
from SCons.Node import FS

root = Path(ARGUMENTS["project"])
work = Path.cwd()
env = Environment(tools=["g++", "gnulink"], ENV=dict(os.environ),
                  CXX=os.environ.get("CXX", "c++"), LINK="$CXX", CXXFLAGS=["-std=c++17"],
                  PROJECT_DIR=str(root), BUILD_DIR=str(work / "build"))
env["__PIO_BUILD_MIDDLEWARES"] = []


def add_middleware(env, callback, pattern=None):
    env["__PIO_BUILD_MIDDLEWARES"].append((callback, pattern))


env.AddMethod(add_middleware, "AddBuildMiddleware")
spec = importlib.util.spec_from_file_location("patch_webserver", root / "scripts/patch_webserver.py")
module = importlib.util.module_from_spec(spec)
module.env = env
module.Import = lambda *_: None
spec.loader.exec_module(module)

parser = work / "sdk/WebServer/src/Parsing.cpp"
module.PARSER_SHA256 = hashlib.sha256(parser.read_text().encode()).hexdigest()
module.PATCH = work / "parser.patch"
if ARGUMENTS.get("baseline") == "1":
    env["__PIO_BUILD_MIDDLEWARES"].clear()
callbacks = env["__PIO_BUILD_MIDDLEWARES"]


def compile_source(env, node):
    return env.Object(target=str(work / "objects" / (node.name + ".o")), source=node)


if ARGUMENTS["dispatcher"] == "windows":
    framework = Path(ARGUMENTS["framework"])
    tree = ast.parse(framework.read_text(encoding="utf-8"))
    wrapper = next(node for node in ast.walk(tree)
                   if isinstance(node, ast.FunctionDef) and node.name == "integrated_middleware")
    namespace = {"existing_middlewares": callbacks, "smart_include_length_shorten": compile_source}
    exec(compile(ast.Module(body=[wrapper], type_ignores=[]), str(framework), "exec"), namespace)
    dispatch = namespace["integrated_middleware"]
else:
    def dispatch(env, node):
        for callback, pattern in callbacks:
            if pattern is None or fnmatch.fnmatch(node.srcnode().get_path(), pattern):
                node = callback(env, node)
        return compile_source(env, node) if isinstance(node, FS.File) else node


env.VariantDir(str(work / "variant"), str(work / "sdk"), duplicate=False)
sources = (env.File(str(work / "main.cpp")), env.File(str(work / "variant/WebServer/src/Parsing.cpp")))
objects = [dispatch(env, node) for node in sources]
Default(env.Program(target=str(work / "consumer"), source=objects))
'''


def platform_framework():
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")).expanduser()
    platforms = Path(os.environ.get("PLATFORMIO_PLATFORMS_DIR", core / "platforms")).expanduser()
    for manifest in sorted(platforms.glob("espressif32*/platform.json")):
        if json.loads(manifest.read_text())["version"] == "55.03.37":
            return manifest.parent / "builder/frameworks/arduino.py"
    raise SystemExit("Install the pinned platform with: pio pkg install -e default")


def run_case(dispatcher, framework, baseline=False):
    with tempfile.TemporaryDirectory(prefix=f"papyrix-parser-{dispatcher}-") as directory:
        work = Path(directory)
        sdk = work / "sdk/WebServer/src"
        sdk.mkdir(parents=True)
        (sdk / "Parsing.cpp").write_text(PARSER)
        (sdk / "fixture_limit.h").write_text("constexpr int fixture_limit = 4;\n")
        (work / "parser.patch").write_text(PATCH)
        (work / "main.cpp").write_text(CONSUMER)
        (work / "SConstruct").write_text(SCONSTRUCT)
        subprocess.run([sys.executable, str(SCONS), "-Q", f"project={ROOT}", f"framework={framework}",
                        f"dispatcher={dispatcher}", f"baseline={int(baseline)}"], cwd=work, check=True)
        executable = work / ("consumer.exe" if os.name == "nt" else "consumer")
        subprocess.run([str(executable)], check=True)
    print(f"PASS: {dispatcher} parser dispatcher builds a bounded consumer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-middleware", action="store_true",
                        help="drop parser middleware as a negative control")
    options = parser.parse_args()
    if not SCONS.is_file():
        raise SystemExit("Install the build tools with: pio pkg install -e default")
    framework = platform_framework()
    for dispatcher in ("normal", "windows"):
        run_case(dispatcher, framework, options.baseline_middleware)


if __name__ == "__main__":
    main()
