#!/usr/bin/env python3
"""Prepare source-only build tools. Supply your own verified FE8U ROM."""

from pathlib import Path
import argparse, hashlib, shutil, subprocess, sys, os, importlib.util

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("--base", type=Path)
parser.add_argument("--with-tests", action="store_true")
args = parser.parse_args()


def run(cmd, cwd=ROOT, env=None):
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def checkout(name, url, pin):
    dest = ROOT / ".deps" / name
    if not dest.exists():
        run(["git", "clone", url, str(dest)])
    run(["git", "checkout", "--detach", pin], cwd=dest)
    return dest


for tool in [
    "git",
    "make",
    "cc",
    "arm-none-eabi-gcc",
    "arm-none-eabi-as",
    "arm-none-eabi-ld",
    "arm-none-eabi-objcopy",
    "arm-none-eabi-readelf",
]:
    if not shutil.which(tool):
        raise SystemExit("Install the required build tool: " + tool)
for module in ["numpy", "PIL"]:
    if importlib.util.find_spec(module) is None:
        raise SystemExit("Install the Python build dependencies: python3 -m pip install numpy Pillow")
rom = args.base or ROOT / "baserom.gba"
if (
    not rom.exists()
    or hashlib.sha1(rom.read_bytes()).hexdigest()
    != "c25b145e37456171ada4b0d440bf88a19f4d509f"
):
    raise SystemExit("Supply --base /path/to/your/verified-FE8-USA.gba")
if rom.resolve() != (ROOT / "baserom.gba").resolve():
    shutil.copyfile(rom, ROOT / "baserom.gba")
ROOT.joinpath(".deps").mkdir(exist_ok=True)
# Pin filled from the compiler used for the matching baseline.
agbcc = checkout(
    "agbcc",
    "https://github.com/pret/agbcc.git",
    "da598c1d918402c42c0c0d7128ba14567f3175e9",
)
run(["sh", "build.sh"], cwd=agbcc)
run(["sh", "install.sh", str(ROOT)], cwd=agbcc)
# The upstream helper builder enumerates every tools/ directory; our Python
# toolset is not a Make target. Build only the native helpers with Makefiles.
for helper in sorted((ROOT / "tools").iterdir()):
    if helper.name != "agbcc" and (helper / "Makefile").is_file():
        run(["make", "-C", str(helper)])
run(["make", "-f", "Makefile.versus", "all"])
if args.with_tests:
    mgba = checkout(
        "mgba",
        "https://github.com/mgba-emu/mgba.git",
        "3a5e34be33dc7f8f707e5bc9db69e8a430046f21",
    )
    cmake = shutil.which("cmake")
    env = None
    if not cmake:
        destination = ROOT / ".deps/cmake"
        run(
            [
                sys.executable,
                "-m",
                "pip",
                "install",
                "--target",
                str(destination),
                "cmake",
            ]
        )
        cmake = str(destination / "bin/cmake")
        env = dict(os.environ, PYTHONPATH=str(destination))
    build = ROOT / ".deps/mgba-build"
    flags = [
        "-DBUILD_QT=OFF",
        "-DBUILD_SDL=OFF",
        "-DBUILD_GL=OFF",
        "-DBUILD_GLES2=OFF",
        "-DBUILD_GLES3=OFF",
        "-DBUILD_TEST=OFF",
        "-DBUILD_SUITE=OFF",
        "-DUSE_FFMPEG=OFF",
        "-DUSE_LIBZIP=OFF",
        "-DUSE_SQLITE3=OFF",
        "-DUSE_DISCORD_RPC=OFF",
        "-DENABLE_SCRIPTING=OFF",
        "-DBUILD_SHARED=ON",
    ]
    run([cmake, "-S", str(mgba), "-B", str(build), *flags], env=env)
    run([cmake, "--build", str(build), "-j8"], env=env)
    run([sys.executable, "tools/versus/test.py", "--linked", "--agents", "--scenarios"])
