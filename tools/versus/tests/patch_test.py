"""Independently apply the emitted BPS and verify every target byte and CRC."""
from pathlib import Path
import struct,zlib
root=Path(__file__).resolve().parents[3]
base=(root/'baserom.gba').read_bytes();p=(root/'build/versus/fire-emblem-versus.bps').read_bytes();at=4
assert p[:4]==b'BPS1'
def number():
 global at
 n=0;shift=1
 while True:
  b=p[at];at+=1;n+=(b&127)*shift
  if b&128:return n
  shift<<=7;n+=shift
assert number()==len(base)
size=number();metadata=number();at+=metadata
out=bytearray()
while len(out)<size:
 code=number();length=(code>>2)+1;mode=code&3
 if mode==0:out+=base[len(out):len(out)+length]
 elif mode==1:out+=p[at:at+length];at+=length
 else:raise AssertionError('Unexpected copy opcode')
assert at==len(p)-12
source_crc,target_crc,patch_crc=struct.unpack_from('<III',p,at)
assert source_crc==zlib.crc32(base)
assert target_crc==zlib.crc32(out)
assert patch_crc==zlib.crc32(p[:-4])
assert out==(root/'build/versus/fire-emblem-versus.gba').read_bytes()
print('PASS: independent BPS application, source/target/patch CRC, exact ROM equality')
