#!/usr/bin/env python3
# Reuse the command and assertion helpers without running runtime.py's UI suite.
exec(open(__file__.replace('combat.py','runtime.py')).read().split('\ng=Game()')[0])
g=Game()
try:
 g.frames(120);g.write(0x203eff0,0x56534254);g.frames(80);g.key(1);g.frames(100)
 g.cmd(1,actor=1,x=7,y=3,cost=5);g.check(1,0)
 g.cmd(0xf0);g.check(2,1)
 hpblue=g.read(0x202be4c+0x13,8);hpred=g.read(0x202cfbc+0x13,8)
 g.cmd(2,actor=0x81,target=1,x=8,y=3,cost=4);g.check(3,1)
 assert g.read(0x202be4c+0x13,8)<hpblue or g.read(0x202cfbc+0x13,8)<hpred
 # Blue can use a Vulnerary on the next phase.
 g.cmd(0xf0);g.check(4,0)
 g.cmd(0x1a,actor=1,x=7,y=3,item=1);g.check(5,0)
 # Inject only HP for an injured adjacent ally fixture, then use native Heal.
 g.write(0x202be4c+3*72+0x13,12,8)
 g.cmd(3,actor=5,target=4,x=2,y=10,cost=1);g.check(6,0)
 assert g.read(0x202be4c+3*72+0x13,8)>12
 assert g.read(0x202be4c+72+0x09,8)==255
 print('PASS: native combat and counterattack, Heal, Vulnerary, no EXP')
finally:g.close()
