#!/usr/bin/env python3
"""Browser transport cannot execute native previews before room acceptance."""
exec(open(__file__.replace('browser_gate.py','runtime.py')).read().split('\ng=Game()')[0])
import json
options=int(json.loads((ROOT/'build/versus/manifest.json').read_text())['symbols']['VersusOptions'],16)
g=Game()
try:
 g.frames(120);g.write(0x203eff0,0x56534254);g.frames(80);g.write(options+9,1,8);g.key(1);g.frames(100)
 for k in [1,0x40,1,0x80,1]:g.key(k)
 assert g.read(0x203f004)==0 and g.read(0x203f353,8)==2
 command=bytes(g.read(0x203f064+i,8) for i in range(24))
 assert command[21:]==b'\0\0\0', 'native command padding must be canonical'
 def accept(data):
  for i,v in enumerate(data):g.write(0x203f804+i,v,8)
  g.write(0x203f800,0x56534243);g.frames(300)
 wrong=bytearray(command);wrong[8]^=1;accept(wrong)
 assert g.read(0x203f004)==0 and g.read(0x203f353,8)==2
 accept(command);g.check(1,0)
 accept(command);g.check(1,0)
 print('PASS: native browser gate waits for room approval, rejects wrong RNG, executes once and normalizes command padding')
finally:g.close()
