#!/usr/bin/env python3
"""Build an address-preserving FE8U overlay and verified BPS patch."""

from pathlib import Path
import hashlib, json, re, struct, subprocess, zlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build/versus"
BASE_SHA = "c25b145e37456171ada4b0d440bf88a19f4d509f"
HOOKS = {
    "StartDestSelectedEvent": "VsDestEvent",
    "HandlePostActionTraps": "VsTraps",
    "GoalDisplay_Init": "VsGoalDisplay_Init",
    "CheckBattleDefeatTalk": "VsCheckBattleDefeatTalk",
    "PidStatsAddBattleAmt": "VsPidStatsAddBattleAmt",
    "PidStatsRecordBattleRes": "VsPidStatsRecordBattleRes",
    "PidStatsRecordDefeatInfo": "VsPidStatsRecordDefeatInfo",
    "PidStatsAddWinAmt": "VsPidStatsAddWinAmt",
    "PidStatsRecordLoseData": "VsPidStatsRecordLoseData",
    "PidStatsAddActAmt": "VsPidStatsAddActAmt",
    "PidStatsAddStatViewAmt": "VsPidStatsAddStatViewAmt",
    "PidStatsAddDeployAmt": "VsPidStatsAddDeployAmt",
    "PidStatsSubFavval08": "VsPidStatsSubFavval08",
    "PidStatsSubFavval100": "VsPidStatsSubFavval100",
    "PidStatsAddSquaresMoved": "VsPidStatsAddSquaresMoved",
    "PidStatsAddExpGained": "VsPidStatsAddExpGained",
    "PidStatsAddFavval": "VsPidStatsAddFavval",
    "StartLinkArenaMainMenu": "VersusEntry",
    "OnMain": "VsOnMain",
    "WriteSuspendSave": "VsWriteSuspendSave",
    "WriteGameSave": "VsWriteGameSave",
    "StartPlayerPhaseStartTutorialEvent": "VsTutorial",
    "StartAfterUnitMovedEvent": "VsMovedEvent",
    "TryCallSelectEvents": "VsSelectEvent",
    "ShouldCallEndEvent": "VsEndEvent",
    "RunPotentialWaitEvents": "VsWaitEvent",
    "StartBattleForecastTutorialEvent": "VsForecastTutorial",
    "TryMakeCantoUnit": "VsCanto",
    "BattleApplyExpGains": "VsExp",
    "UnitKill": "VsKill",
    "NextRN": "VsNextRN",
    "BattleGenerate": "VsBattleGenerate",
    "OverriddenMenuAvailability": "VsMenuAvailability",
    "PlayerPhase_MainIdle": "VsPlayerIdle",
    "ApplyUnitAction": "VsApplyUnitAction",
    "PlayerPhase_FinishAction": "VsFinishAction",
    "CommandEffectEndPlayerPhase": "VsEndPhase",
    "TrySwitchViewedUnit": "VsSwitchViewedUnit",
}


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True)


def symbols(elf):
    result = {}
    for line in run("arm-none-eabi-readelf", "-sW", str(elf)).splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[0].endswith(":"):
            try:
                value = int(parts[1], 16)
            except ValueError:
                continue
            if parts[3] in ("FUNC", "OBJECT", "NOTYPE") and parts[6] != "UND":
                result[parts[7]] = (value, parts[3], int(parts[2]))
    return result


def trampoline(name, addr, base, size):
    # Only relocate supported Thumb-1 prologue instructions. Unknown PC-relative
    # instructions fail the build instead of silently producing a corrupt hook.
    lines = [".align 2", ".thumb_func", f".global Original_{name}", f"Original_{name}:"]
    pos = 0
    literals = []
    branches = []
    while pos < size:
        pc = addr + pos
        ins = struct.unpack_from("<H", base, pc - 0x08000000)[0]
        if ins & 0xF800 == 0x4800:
            reg = (ins >> 8) & 7
            lit = ((pc + 4) & ~3) + (ins & 255) * 4
            value = struct.unpack_from("<I", base, lit - 0x08000000)[0]
            label = f".L{name}_{pos}"
            lines.append(f"ldr r{reg}, {label}")
            literals.append((label, value))
        elif ins & 0xF800 == 0xA000:
            reg = (ins >> 8) & 7
            value = ((pc + 4) & ~3) + (ins & 255) * 4
            label = f".L{name}_{pos}"
            lines.append(f"ldr r{reg}, {label}")
            literals.append((label, value))
        elif ins & 0xF800 == 0xF000:
            second = struct.unpack_from("<H", base, pc + 2 - 0x08000000)[0]
            if second & 0xF800 != 0xF800:
                raise RuntimeError(f"{name}: unsupported Thumb long branch")
            hi = ins & 0x7FF
            if hi & 0x400:
                hi -= 0x800
            target = pc + 4 + (hi << 12) + ((second & 0x7FF) << 1)
            lines += [f"ldr r3, =0x{target|1:08x}", "bl VsCallR3"]
            pos += 2
        elif ins & 0xF000 == 0xD000 and (ins >> 8) & 15 < 14:
            cond = [
                "eq",
                "ne",
                "cs",
                "cc",
                "mi",
                "pl",
                "vs",
                "vc",
                "hi",
                "ls",
                "ge",
                "lt",
                "gt",
                "le",
            ][(ins >> 8) & 15]
            delta = ins & 255
            if delta & 128:
                delta -= 256
            target = pc + 4 + delta * 2
            label = f".Lbranch_{name}_{pos}"
            lines.append(f"b{cond} {label}")
            branches.append((label, target))
        elif ins & 0xF800 == 0xE000:
            delta = ins & 2047
            if delta & 1024:
                delta -= 2048
            target = pc + 4 + delta * 2
            label = f".Lbranch_{name}_{pos}"
            lines.append(f"b {label}")
            branches.append((label, target))
        elif ins & 0xFC00 == 0x4400 and ((ins & 7) | ((ins >> 4) & 8)) == 15:
            raise RuntimeError(f"{name}: PC arithmetic in hook prologue")
        else:
            lines.append(f".hword 0x{ins:04x}")
        pos += 2
    lines += [
        "push {r3}",
        f"ldr r3, =0x{(addr+pos)|1:08x}",
        "mov ip, r3",
        "pop {r3}",
        "bx ip",
    ]
    for label, target in branches:
        if addr <= target < addr + pos:
            raise RuntimeError(f"{name}: branch into replaced prologue")
        lines += [
            label + ":",
            "push {r3}",
            f"ldr r3, =0x{target|1:08x}",
            "mov ip,r3",
            "pop {r3}",
            "bx ip",
        ]
    lines += [".ltorg", ".align 2"]
    for label, value in literals:
        lines += [label + ":", f".word 0x{value:08x}"]
    return "\n".join(lines) + "\n", pos


def varint(n):
    b = bytearray()
    while True:
        x = n & 127
        n >>= 7
        if not n:
            b.append(x | 128)
            return b
        b.append(x)
        n -= 1


def bps(source, target):
    out = bytearray(b"BPS1") + varint(len(source)) + varint(len(target)) + varint(0)
    i = 0
    while i < len(target):
        equal = i < len(source) and source[i] == target[i]
        j = i + 1
        while j < len(target) and (j < len(source) and source[j] == target[j]) == equal:
            j += 1
        out += varint(((j - i - 1) << 2) | (0 if equal else 1))
        if not equal:
            out += target[i:j]
        i = j
    out += struct.pack("<II", zlib.crc32(source), zlib.crc32(target))
    out += struct.pack("<I", zlib.crc32(out))
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    base = (ROOT / "baserom.gba").read_bytes()
    if hashlib.sha1(base).hexdigest() != BASE_SHA:
        raise SystemExit("Wrong base ROM. Expected FE8 USA SHA-1 " + BASE_SHA)
    if (ROOT / "fireemblem8.gba").read_bytes() != base:
        raise SystemExit("Reproduce the matching baseline before building Versus.")
    syms = symbols(ROOT / "fireemblem8.elf")
    sources = (
        sorted((ROOT / "versus").glob("*.c"))
        + sorted((ROOT / "versus").glob("*.h"))
        + sorted((ROOT / "versus").glob("*.json"))
    )
    catalog = json.loads((ROOT / "versus/catalog.json").read_text())
    counts = {
        "VS_MAP_COUNT": len(catalog["maps"]),
        "VS_PARTY_COUNT": len(catalog["parties"]),
        "VS_OBJECTIVE_COUNT": len(catalog["objectives"]),
    }
    if any(n < 1 or n > 255 for n in counts.values()):
        raise SystemExit("Catalog selector counts must fit one byte")
    for entries in [catalog["maps"], catalog["parties"]]:
        if len({e["id"] for e in entries}) != len(entries):
            raise SystemExit("Catalog IDs must be unique")
    for m in catalog["maps"]:
        if (
            m["width"] != 15
            or m["height"] != 15
            or len(m["tiles"]) != 15
            or any(
                len(row) != 15 or any(v not in range(8) for v in row)
                for row in m["tiles"]
            )
        ):
            raise SystemExit("Maps must contain 15x15 supported terrain tiles")
        if any(
            m["tiles"][y][x] != m["tiles"][14 - y][x]
            for y in range(15)
            for x in range(15)
        ):
            raise SystemExit("Maps must mirror terrain between armies")
        castle_cells = {
            (x, y): 5 if x == 7 and y in [1, 13] else 4
            for y in [0, 1, 13, 14]
            for x in [6, 7, 8]
        }
        if any(
            (
                m["tiles"][y][x] != castle_cells[(x, y)]
                if (x, y) in castle_cells
                else m["tiles"][y][x] in [4, 5]
            )
            for y in range(15)
            for x in range(15)
        ):
            raise SystemExit("Castle terrain must match the native castle footprint")
        if m["castles"] != [[7, 13], [7, 1]] or m["deployment"] != [
            [[x, 12] for x in [3, 5, 6, 9, 11]],
            [[x, 2] for x in [3, 5, 6, 9, 11]],
        ]:
            raise SystemExit(
                "Catalog geometry must match native deployment and castles"
            )
        # Ground units must connect from every deployment to both gates.
        visited = set()
        pending = [tuple(m["castles"][0])]
        while pending:
            x, y = pending.pop()
            if (x, y) in visited or not (0 <= x < 15 and 0 <= y < 15):
                continue
            if m["tiles"][y][x] in [4, 6]:
                continue
            visited.add((x, y))
            pending.extend([(x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)])
        required = m["castles"] + m["deployment"][0] + m["deployment"][1]
        if any(tuple(xy) not in visited for xy in required):
            raise SystemExit(
                "Every deployment and castle must connect for ground units"
            )
        if any(m["tiles"][y][x] != 0 for side in m["deployment"] for x, y in side):
            raise SystemExit("Deployments must start on equal open terrain")
    for p in catalog["parties"]:
        if [u["role"] for u in p["units"]] != catalog["roles"]:
            raise SystemExit("Each party must contain the five ordered roles")
    (OUT / "catalog_counts.h").write_text(
        "\n".join(f"#define {key} {value}" for key, value in counts.items()) + "\n"
    )
    generated = [
        "static const u8 scenarioTiles[VS_MAP_COUNT][15][15] = "
        + str([m["tiles"] for m in catalog["maps"]]).replace("[", "{").replace("]", "}")
        + ";"
    ]
    generated.append(
        "static const struct VsRoster roster[VS_PARTY_COUNT][5] = {"
        + ",".join(
            "{"
            + ",".join(
                "{"
                + ",".join(
                    str(u[k])
                    for k in [
                        "class_symbol",
                        "item_symbol",
                        "hp",
                        "power",
                        "speed",
                        "defense",
                        "resistance",
                    ]
                )
                + "}"
                for u in p["units"]
            )
            + "}"
            for p in catalog["parties"]
        )
        + "};"
    )
    for label, entries, prefix in [
        ("mapNames", catalog["maps"], ""),
        ("bluePartyNames", catalog["parties"], "Blue: "),
        ("redPartyNames", catalog["parties"], "Red: "),
    ]:
        generated.append(
            "static const char *"
            + label
            + "[] = {"
            + ",".join(json.dumps(prefix + entry["name"]) for entry in entries)
            + "};"
        )
    (OUT / "catalog.h").write_text("\n".join(generated))
    (OUT / "catalog.json").write_text(json.dumps(catalog, indent=2) + "\n")
    content = hashlib.sha256(
        b"".join(p.name.encode() + p.read_bytes() for p in sources)
        + Path(__file__).read_bytes()
    ).hexdigest()
    (OUT / "content.h").write_text(f"#define VS_CONTENT_ID 0x{content[:8]}u\n")
    (OUT / "engine.ld").write_text(
        "\n".join(
            f"{k} = 0x{v[0]:08x};"
            for k, v in syms.items()
            if re.fullmatch(r"[A-Za-z_]\w*", k) and k not in ("end",)
        )
    )
    assembly = [
        ".syntax unified",
        ".cpu arm7tdmi",
        ".thumb",
        '.section .text.trampolines,"ax",%progbits',
    ]
    assembly += [".thumb_func", ".global VsCallR3", "VsCallR3:", "bx r3"]
    lengths = {}
    for name in HOOKS:
        if name == "StartLinkArenaMainMenu":
            continue
        addr = syms[name][0] & ~1
        size = 8 if addr % 4 == 0 else 10
        code, n = trampoline(name, addr, base, size)
        assembly.append(code)
        lengths[name] = n
    (OUT / "trampolines.s").write_text("\n".join(assembly))
    flags = [
        "-std=gnu11",
        "-mcpu=arm7tdmi",
        "-mthumb",
        "-mthumb-interwork",
        "-mlong-calls",
        "-Os",
        "-g",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-common",
        "-Wall",
        "-Werror=implicit-function-declaration",
        "-isystem",
        "tools/agbcc/include",
        "-Iinclude",
        "-Iversus",
        "-I" + str(OUT),
    ]
    objects = []
    for source in sorted((ROOT / "versus").glob("*.c")):
        obj = OUT / (source.stem + ".o")
        run("arm-none-eabi-gcc", *flags, "-c", str(source), "-o", str(obj))
        objects.append(str(obj))
    run(
        "arm-none-eabi-as",
        "-mcpu=arm7tdmi",
        "-mthumb-interwork",
        str(OUT / "trampolines.s"),
        "-o",
        str(OUT / "trampolines.o"),
    )
    objects.append(str(OUT / "trampolines.o"))
    (OUT / "overlay.ld").write_text(
        """MEMORY { rom (rx) : ORIGIN = 0x09000000, LENGTH = 16M
 ram (rw) : ORIGIN = 0x0203F000, LENGTH = 4K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) *(.data*) } > rom
 .bss (NOLOAD) : { *(.bss.VersusData) *(.bss*) *(COMMON) } > ram
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.eh_frame*) }
}
ASSERT(VersusData == 0x0203F000, "Versus context must start at fixed mailbox address")
ASSERT(SIZEOF(.bss) <= 0x800, "Versus exceeds free EWRAM")
"""
    )
    # Put the context in an explicit section to make RAM placement reproducible.
    gcc_lib = run(
        "arm-none-eabi-gcc", "-mcpu=arm7tdmi", "-mthumb", "-print-libgcc-file-name"
    ).strip()
    run(
        "arm-none-eabi-ld",
        "-T",
        str(OUT / "engine.ld"),
        "-T",
        str(OUT / "overlay.ld"),
        "-Map",
        str(OUT / "versus.map"),
        "-o",
        str(OUT / "versus.elf"),
        *objects,
        gcc_lib,
    )
    run(
        "arm-none-eabi-objcopy",
        "-O",
        "binary",
        "-j",
        ".text",
        str(OUT / "versus.elf"),
        str(OUT / "overlay.bin"),
    )
    overlay = (OUT / "overlay.bin").read_bytes()
    hack = bytearray(base) + overlay
    newsyms = symbols(OUT / "versus.elf")
    hooks = []
    for name, replacement in HOOKS.items():
        addr = syms[name][0] & ~1
        dest = newsyms[replacement][0] | 1
        off = addr - 0x08000000
        if addr % 4 == 0:
            stub = struct.pack("<HHI", 0x4B00, 0x4718, dest)
        else:
            stub = struct.pack("<HHHI", 0x4B01, 0x4718, 0x46C0, dest)
        old = bytes(hack[off : off + len(stub)])
        hack[off : off + len(stub)] = stub
        hooks.append(
            {
                "symbol": name,
                "address": hex(addr),
                "target": hex(dest),
                "original": old.hex(),
                "patch": stub.hex(),
            }
        )
    (OUT / "fire-emblem-versus.gba").write_bytes(hack)
    (OUT / "fire-emblem-versus.bps").write_bytes(bps(base, hack))
    manifest = {
        "catalog_sha256": hashlib.sha256(
            (OUT / "catalog.json").read_bytes()
        ).hexdigest(),
        "base_sha1": BASE_SHA,
        "baseline_commit": "ecc6798b68fc7d0d164b2b6dd96a9fee4306cadb",
        "source_commit": run("git", "rev-parse", "HEAD").strip(),
        "content_sha256": content,
        "rom_sha256": hashlib.sha256(hack).hexdigest(),
        "overlay_bytes": len(overlay),
        "compiler": run("arm-none-eabi-gcc", "--version").splitlines()[0],
        "ram_address": "0x0203F000",
        "hooks": hooks,
        "symbols": {
            k: hex(v[0]) for k, v in newsyms.items() if k.startswith(("Vs", "Versus"))
        },
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(
        f"Built ROM ({len(hack)} bytes), BPS patch, ELF, symbols and manifest in {OUT}"
    )


if __name__ == "__main__":
    main()
