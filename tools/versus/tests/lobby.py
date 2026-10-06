#!/usr/bin/env python3
exec(open(__file__.replace('lobby.py','runtime.py')).read().split('\ng=Game()')[0])
g=Game()
try:
 g.frames(120);g.key(0x304)
 for _ in range(3):g.key(0x80)
 g.key(1);assert g.read(0x203f35b,8)==1
 for _ in range(3):g.key(0x40)
 g.key(1);g.frames(100);g.check(0,1)
 g.cmd(0xf1,frames=60);assert g.read(0x203f358,8)==1
 print('PASS: opening-army menu selection and red surrender')
finally:g.close()
g=Game()
try:
 g.frames(120);g.key(0x304);g.key(0x80);g.key(1);g.frames(1900)
 assert g.read(0x203f359,8)==9
 assert g.read(0x203f358,8)==4
 assert g.read(0x203f35c,8)==0
 print('PASS: absent cable peer times out to an explicit abort result')
finally:g.close()
