#!/usr/bin/env python3
"""Reach a castle through legal movement, then select native Seize using buttons."""
exec(open(__file__.replace('seize_ui.py','runtime.py')).read().split('\ng=Game()')[0])
import json
manifest=json.loads((ROOT/'build/versus/manifest.json').read_text())
options=int(manifest['symbols']['VersusOptions'],16)
g=Game()
try:
 g.frames(120);g.write(0x203eff0,0x56534254);g.frames(80)
 g.write(options+3,2,8);g.key(1);g.frames(100);g.check(0,0)
 seq=0
 # Move sword to (7,2), retaining a distinct explicit Seize action for the following phase.
 for turn in range(10):
  pos=(g.read(0x202be4c+16,8),g.read(0x202be4c+17,8))
  if pos==(7,2):break
  commands=[];first=0
  while True:
   g.write(0x203f804,first);g.write(0x203f800,0x56534c47);g.frames(30)
   assert g.read(0x203f800)==0
   count=g.read(0x203f808);total=g.read(0x203f80c)
   for i in range(count):
    raw=b''.join(g.read(0x203f900+24*i+j).to_bytes(4,'little') for j in range(0,24,4));commands.append(struct.unpack('<II3H7B3x',raw))
   first+=count
   if first>=total:break
  moves=[c for c in commands if c[5]==1 and c[9]==1]
  assert moves, (turn, pos, g.read(0x203f353,8),g.read(0x203f354,8),g.read(0x203f355,8),commands[:2],count,total)
  c=min(moves,key=lambda c:abs(c[7]-7)+abs(c[8]-2))
  g.cmd(1,actor=1,x=c[7],y=c[8],cost=c[11]);seq+=1;g.check(seq,0)
  g.cmd(0xf0);seq+=1;g.cmd(0xf0);seq+=1;g.check(seq,0)
 else:raise AssertionError('failed to reach castle approach')
 # Cursor was reset to (6,12); move north to the castle approach.
 g.key(0x10)
 for _ in range(10):g.key(0x40)
 # Native unit info must preserve the castle rules and confirmed state.
 g.key(0x100);g.key(2);g.check(seq,0)
 for key in [1,0x40,1,1]:g.key(key)
 g.frames(200)
 assert g.read(0x203f358,8)==1
 assert g.read(options+8,8)==2
 assert g.read(0x203f004)==seq+1
 print('PASS: native Seize menu wins and confirms one castle action')
finally:g.close()
