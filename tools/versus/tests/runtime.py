#!/usr/bin/env python3
"""Native FE8 map checks against the headless mGBA core; no save file."""
from pathlib import Path
import subprocess, struct
ROOT=Path(__file__).resolve().parents[3]
class Game:
 def __init__(self):
  self.p=subprocess.Popen([str(ROOT/'build/versus/headless'),str(ROOT/'build/versus/fire-emblem-versus.gba')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
 def send(self,s,reply=False):
  self.p.stdin.write(s+'\n');self.p.stdin.flush()
  return self.p.stdout.readline().strip() if reply else None
 def frames(self,n):return self.send(f'frames {n}',True)
 def read(self,a,width=32):return int(self.send(f'r{width} {a:x}',True).split()[1],16)
 def write(self,a,n,width=32):self.send(f'w{width} {a:x} {n:x}')
 def key(self,k):self.send(f'keys {k:x}');self.frames(3);self.send('keys 0');self.frames(30)
 def check(self,seq,seat):
  assert self.read(0x203f004)==seq
  assert self.read(0x203f353,8)==1
  assert self.read(0x203f355,8)==seat
  assert self.read(0x203f359,8)==0
 def cmd(self,kind,actor=0,target=0,x=0,y=0,item=0,cost=0,frames=300):
  data=struct.pack('<II',self.read(0x203f004)+1,self.read(0x203f008))
  data+=struct.pack('<IH',self.read(0x3000000),self.read(0x3000004)&65535)
  data+=bytes([actor,target,x,y,kind,item,cost,0,0,0])
  for i in range(0,24,4):self.write(0x203f804+i,int.from_bytes(data[i:i+4],'little'))
  self.write(0x203f800,0x5653434d);self.frames(frames)
 def close(self):self.send('quit');assert self.p.wait(timeout=5)==0

g=Game()
try:
 g.frames(120);g.write(0x203eff0,0x56534254);g.frames(80);g.key(1);g.frames(100);g.check(0,0)
 # Native stat-screen round trip must not alter map graphics or match state.
 tiles=[g.read(0x6008400+i*4) for i in range(24)]
 g.key(0x100);g.key(2);g.check(0,0)
 assert tiles==[g.read(0x6008400+i*4) for i in range(24)]
 # Cancel a movement preview before committing an action.
 for k in [1,0x10,2]:g.key(k)
 g.check(0,0)
 assert g.read(0x202be4c+2*72+0x10,8)==2
 # Select Neimi, move east, choose Wait through the actual native menus.
 for k in [1,0x10,1,0x80,1]:g.key(k)
 g.frames(180);g.check(1,0)
 assert g.read(0x202be4c+2*72+0x10,8)==3
 # Native map End menu on the spent unit.
 g.key(1);g.key(1);g.frames(180);g.check(2,1)
 # Red archer is controllable through the same native move/Wait path.
 for k in [1,0x20,1,0x80,1]:g.key(k)
 g.frames(180);g.check(3,1)
 assert g.read(0x202cfbc+2*72+0x10,8)==11
 g.cmd(0xf0);g.check(4,0)
 # Surrender produces a result screen without writing campaign data.
 g.cmd(0xf1);assert g.read(0x203f358,8)==2;assert g.read(0x203f35c,8)==0
 g.key(1);g.frames(180);g.check(0,1)
 print('PASS: native blue/red movement, Wait, End, surrender, swapped-opener rematch')
finally:g.close()
