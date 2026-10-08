#!/usr/bin/env python3
"""Build the native SDL2 frontend against the pinned mGBA library."""
from pathlib import Path
import shlex
import shutil
import sys
import os
import plistlib
import subprocess

ROOT = Path(__file__).resolve().parents[3]
if not shutil.which('pkg-config'):
    raise SystemExit('Install pkg-config and SDL2 development headers first')
try:
    sdl = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'], text=True))
except subprocess.CalledProcessError:
    raise SystemExit('SDL2 development package is required (macOS: brew install sdl2 pkg-config)')
lib = ROOT / '.deps/mgba-build'
if not (lib / 'CMakeFiles/mgba.dir/flags.make').exists():
    raise SystemExit('Bootstrap the pinned mGBA core with --with-tests first')
flags = (lib / 'CMakeFiles/mgba.dir/flags.make').read_text().split('C_DEFINES = ')[1].splitlines()[0].split()
subprocess.run([
    'cc', '-std=c11', '-Wall', '-Wextra', *flags, '-DNDEBUG', '-DVERSUS_DESKTOP',
    '-I.deps/mgba/include', '-I.deps/mgba-build/include', '-I.deps/mgba/src/platform/sdl',
    'tools/versus/agents/bridge.c', '.deps/mgba/src/platform/sdl/sdl-audio.c',
    '-L' + str(lib), '-lmgba', '-Wl,-rpath,' + str(lib), *sdl,
    '-o', str(ROOT / 'build/versus/agent-desktop'),
], cwd=ROOT, check=True)
print(ROOT / 'build/versus/agent-desktop')

if sys.platform == 'darwin':
    app = ROOT / 'build/versus/Fire Emblem Versus.app'
    executable = app / 'Contents/MacOS/agent-desktop'
    executable.parent.mkdir(parents=True, exist_ok=True)
    temporary = executable.with_suffix(".new")
    shutil.copy2(ROOT / "build/versus/agent-desktop", temporary)
    os.replace(temporary, executable)
    with (app / 'Contents/Info.plist').open('wb') as handle:
        plistlib.dump({'CFBundleName': 'Fire Emblem Versus', 'CFBundleDisplayName': 'Fire Emblem Versus',
                      'CFBundleIdentifier': 'org.llmletsplay.fire-emblem-versus',
                      'CFBundleExecutable': 'agent-desktop', 'CFBundlePackageType': 'APPL',
                      'CFBundleVersion': '1', 'NSHighResolutionCapable': True}, handle)
    print(app)
