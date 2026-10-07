#!/usr/bin/env python3
exec(open(__file__.replace('elimination.py','runtime.py')).read().split('\ng=Game()')[0])
g=Game()
try:
 g.frames(120);g.write(0x203eff0,0x56534254);g.frames(80);g.key(1);g.frames(100)
 g.cmd(1,actor=1,x=3,y=7,cost=5);g.cmd(0xf0)
 # Terminal fixture: one red unit remains, critically injured.
 for n in range(1,5):
  u=0x202cfbc+n*72;g.write(u+0x13,0,8);g.write(u+0xc,5)
 g.write(0x202cfbc+0x13,1,8)
 g.write(0x202be4c+0x15,60,8)
 g.cmd(2,actor=0x81,target=1,x=3,y=6,cost=4,frames=600)
 assert g.read(0x203f359,8)==0
 assert g.read(0x203f358,8)==1
 assert g.read(0x203f35c,8)==0
 assert g.read(0x202cfbc+0x13,8)==0
 assert g.read(0x202cfbc)!=0
 print('PASS: native fatal counterattack, elimination, dead red-unit identity retained')
finally:g.close()
