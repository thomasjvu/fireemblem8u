#!/usr/bin/env python3
exec(open(__file__.replace('lifecycle.py','runtime.py')).read().split('\ng=Game()')[0])
g=Game()
try:
 # Retail shortcut, with no boot mailbox, from the opening presentation.
 g.frames(120);g.key(0x304)
 assert g.read(0x203f000)==0x56533130
 g.key(1);g.frames(100);g.check(0,0)
 for match in range(10):
  g.cmd(0xf1,frames=60)
  assert g.read(0x203f35c,8)==0
  g.key(1);g.frames(100);g.check(0,(match+1)%2)
 for n in range(60):g.cmd(0xf0,frames=60)
 assert g.read(0x203f358,8)==3
 assert g.read(0x203f357,8)==30
 assert g.read(0x203f004)==60
 assert g.read(0x203f35c,8)==0
 g.key(0x80);g.key(1);g.frames(100);assert g.read(0x203f000)==0
 print('PASS: retail shortcut, ten rematches, 30-round draw')
finally:g.close()
