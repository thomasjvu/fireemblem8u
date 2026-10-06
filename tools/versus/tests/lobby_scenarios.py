#!/usr/bin/env python3
"""Select a complete non-default scenario through the native lobby controls."""

exec(
    open(__file__.replace("lobby_scenarios.py", "runtime.py"))
    .read()
    .split("\ng=Game()")[0]
)
import json

options = int(
    json.loads((ROOT / "build/versus/manifest.json").read_text())["symbols"][
        "VersusOptions"
    ],
    16,
)
catalog = json.loads((ROOT / "versus/catalog.json").read_text())
last_map = len(catalog["maps"]) - 1
last_party = len(catalog["parties"]) - 1
g = Game()
try:
    g.frames(120)
    g.write(0x203EFF0, 0x56534254)
    g.frames(80)
    for _ in range(4):
        g.key(0x80)
    for _ in range(last_map):
        g.key(1)
    assert g.read(options, 8) == last_map
    g.key(1)
    assert g.read(options, 8) == 0
    for _ in range(last_map):
        g.key(1)
    g.key(0x80)
    for _ in range(last_party):
        g.key(1)
    assert g.read(options + 1, 8) == last_party
    g.key(1)
    assert g.read(options + 1, 8) == 0
    for _ in range(last_party):
        g.key(1)
    g.key(0x80)
    for _ in range(last_party):
        g.key(1)
    assert g.read(options + 2, 8) == last_party
    g.key(1)
    assert g.read(options + 2, 8) == 0
    for _ in range(last_party):
        g.key(1)
    g.key(0x80)
    g.key(1)
    g.key(1)
    assert g.read(options + 3, 8) == 2
    for _ in range(7):
        g.key(0x40)
    g.key(1)
    g.frames(100)
    g.check(0, 0)
    assert [g.read(options + 4 + i, 8) for i in range(4)] == [
        last_map,
        last_party,
        last_party,
        2,
    ]
    assert g.read(0x202BE4C + 18, 8) == catalog["parties"][last_party]["units"][0]["hp"]
    assert g.read(0x202CFBC + 18, 8) == catalog["parties"][last_party]["units"][0]["hp"]
    g.cmd(0xF1)
    assert g.read(options + 8, 8) == 5
    g.key(1)
    g.frames(180)
    g.check(0, 1)
    assert [g.read(options + 4 + i, 8) for i in range(4)] == [
        last_map,
        last_party,
        last_party,
        2,
    ]
    print(
        "PASS: native lobby selects map, independent parties, objective and retains them for rematch"
    )
finally:
    g.close()
