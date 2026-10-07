#!/usr/bin/env python3
"""Compile host tools with the same ABI flags as the configured mGBA library."""

from pathlib import Path
import subprocess, sys

root = Path(__file__).resolve().parents[2]
out = root / "build/versus"
out.mkdir(parents=True, exist_ok=True)


def run(args, **kw):
    subprocess.run(args, cwd=root, check=True, **kw)


run(
    [
        "cc",
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Iversus",
        "versus/wire.c",
        "tools/versus/tests/wire_test.c",
        "-o",
        str(out / "wire-test"),
    ]
)
run([str(out / "wire-test")])
run([sys.executable, "tools/versus/tests/patch_test.py"])
flags = (
    (root / ".deps/mgba-build/CMakeFiles/mgba.dir/flags.make")
    .read_text()
    .split("C_DEFINES = ")[1]
    .splitlines()[0]
    .split()
)
lib = root / ".deps/mgba-build"
for name, source in [
    ("headless", "headless.c"),
    ("linked-test", "linked.c"),
    ("agent-bridge", "../agents/bridge.c"),
]:
    run(
        [
            "cc",
            "-std=c11",
            *flags,
            "-DNDEBUG",
            "-I.deps/mgba/include",
            "-I.deps/mgba-build/include",
            f"tools/versus/tests/{source}",
            "-L" + str(lib),
            "-lmgba",
            "-Wl,-rpath," + str(lib),
            "-o",
            str(out / name),
        ]
    )
run([sys.executable, "tools/versus/tests/runtime.py"], timeout=60)
run([sys.executable, "tools/versus/tests/combat.py"], timeout=60)
run([sys.executable, "tools/versus/tests/lifecycle.py"], timeout=60)
run([sys.executable, "tools/versus/tests/elimination.py"], timeout=60)
run([sys.executable, "tools/versus/tests/lobby.py"], timeout=60)
if "--linked" in sys.argv:
    for flags in [[], ["--red"]]:
        run(
            [str(out / "linked-test"), str(out / "fire-emblem-versus.gba"), *flags],
            timeout=70,
        )

if "--agents" in sys.argv:
    run([sys.executable, "tools/versus/agents/check.py"], timeout=300)
    run([sys.executable, "tools/versus/agents/max_level.py"], timeout=120)

if "--scenarios" in sys.argv:
    run([sys.executable, "tools/versus/tests/lobby_scenarios.py"], timeout=60)
    run([sys.executable, "tools/versus/tests/seize_invalid.py"], timeout=60)
    run([sys.executable, "tools/versus/tests/seize_ui.py"], timeout=60)
    run([sys.executable, "tools/versus/agents/scenarios.py"], timeout=2400)
