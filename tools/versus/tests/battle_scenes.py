#!/usr/bin/env python3
"""Real FE8 character scenes, class ranks and return-to-map regression."""
exec(open(__file__.replace('battle_scenes.py', 'runtime.py')).read().split('\ng=Game()')[0])
import json

symbols = {}
for line in (ROOT / 'build/versus/engine.ld').read_text().splitlines():
    if ' = ' in line:
        key, value = line.rstrip(';').split(' = ')
        symbols[key] = int(value, 16)
options = int(json.loads((ROOT / 'build/versus/manifest.json').read_text())['symbols']['VersusOptions'], 16)
catalog = json.loads((ROOT / 'versus/catalog.json').read_text())

def start(g, party=0):
    g.frames(120)
    g.write(0x203eff0, 0x56534254)
    g.frames(80)
    g.write(options + 1, party, 8)
    g.write(options + 2, party, 8)
    g.key(1)
    g.frames(100)

def ranks(g):
    for base in [0x202be4c, 0x202cfbc]:
        for i in range(5):
            unit = base + i * 72
            cls = g.read(unit + 4)
            for weapon in range(8):
                expected = 181 if g.read(cls + 0x2c + weapon, 8) else 0
                assert g.read(unit + 0x28 + weapon, 8) == expected

for party in range(len(catalog['parties'])):
    g = Game()
    try:
        start(g, party)
        ranks(g)
    finally:
        g.close()

# Includes Fire, a ranged bow, a sword counterattack and the formerly stuck
# axe-versus-archer scene. Commands use the real validator and action pipeline.
commands = [
    (1, 1, 0, 3, 7, 5), (1, 2, 0, 4, 8, 5),
    (1, 3, 0, 7, 8, 5), (1, 4, 0, 8, 8, 5),
    (0xf0, 0, 0, 0, 0, 0),
    (2, 0x84, 4, 8, 6, 5), (2, 0x83, 3, 7, 6, 5),
    (2, 0x81, 1, 3, 6, 4), (1, 0x82, 0, 3, 5, 5),
    (0xf0, 0, 0, 0, 0, 0),
    (2, 1, 0x83, 6, 6, 4), (2, 2, 0x83, 7, 7, 5),
]
g = Game()
try:
    start(g)
    for seq, (kind, actor, target, x, y, cost) in enumerate(commands, 1):
        g.cmd(kind, actor=actor, target=target, x=x, y=y, cost=cost, frames=90)
        if kind == 2:
            assert g.read(symbols['gBattleDeamonActive']) == 1, 'full scene never started'
            g.send('shot ' + str(ROOT / 'build/versus' / f'battle-scene-{seq}.ppm'))
        g.frames(1710)
        g.check(seq, 0 if seq < 5 or seq >= 10 else 1)
        assert g.read(symbols['gBattleDeamonActive']) == 0, 'scene did not return to map'
        ranks(g)
    g.send('shot ' + str(ROOT / 'build/versus/battle-scene-map-restored.ppm'))
    print('PASS: all 18 presets have class-correct ranks; full Fire, bow, sword and axe scenes return to a confirmed map')
finally:
    g.close()
