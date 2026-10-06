#!/usr/bin/env python3
"""Reject forged native captures of invalid, occupied or disabled objectives."""
exec(open(__file__.replace('seize_invalid.py','runtime.py')).read().split('\ng=Game()')[0])
import json
options=int(json.loads((ROOT/'build/versus/manifest.json').read_text())['symbols']['VersusOptions'],16)
cases=[(2,1,7,None),(2,3,7,None),(0,7,3,None),(2,13,7,'dead'),(2,13,7,'spent'),(2,13,7,'occupied')]
for mode,x,y,fixture in cases:
 g=Game()
 try:
  g.frames(120);g.write(0x203eff0,0x56534254);g.frames(80);g.write(options+3,mode,8);g.key(1);g.frames(100)
  if fixture:
   # Place the actor in range so rejection tests the actor/occupancy rule rather than distance.
   g.write(0x202be4c+16,13,8);g.write(0x202be4c+17,6,8)
   g.write(0x203f07c+16,13,8);g.write(0x203f07c+17,6,8)
   if fixture=='dead':g.write(0x202be4c+19,0,8)
   if fixture=='spent':
    g.write(0x202be4c+12,2);g.write(0x203f07c+12,2)
   if fixture=='occupied':
    g.write(0x202cfbc+16,13,8);g.write(0x202cfbc+17,7,8)
  g.cmd(17,actor=1,x=x,y=y,cost=1)
  assert g.read(0x203f359,8)==2,(mode,x,y,fixture)
  assert g.read(0x203f004)==0
 finally:g.close()
print('PASS: native Seize rejects own castle, fort, disabled mode, dead/spent actors and occupied castle')
