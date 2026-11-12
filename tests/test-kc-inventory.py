#!/usr/bin/env python3
"""Synthetic-only structural tests for the read-only kernelcache inventory."""
import plistlib,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from kc_inventory import inspect,ROOTS

def name(s,n=16):return s.encode()+bytes(n-len(s))
def build():
    base=0xfffffe0007000000
    md=[]
    for i,n in enumerate(ROOTS):
        md.append({'CFBundleIdentifier':n,'OSBundleLibraries':({ROOTS[-1]:'1.0'} if i<3 else {})})
    plist=plistlib.dumps({'_PrelinkInfoDictionary':md})
    seg1=struct.pack('<II16s4Q4I',0x19,72,name('__PRELINK_TEXT'),base+0x4000,0x1000,0x4000,0x1000,7,5,0,0)
    sec=struct.pack('<16s16sQQIIIIIIII',name('__info'),name('__PRELINK_INFO'),base+0x8000,len(plist),0x8000,0,0,0,0,0,0,0)
    seg2=struct.pack('<II16s4Q4I',0x19,152,name('__PRELINK_INFO'),base+0x8000,len(plist),0x8000,len(plist),3,1,1,0)+sec
    cmds=seg1+seg2
    for i,n in enumerate(ROOTS):
        label=n.encode()+b'\0';size=(32+len(label)+7)&~7
        cmds+=struct.pack('<IIQQII',0x80000035,size,base+0x4000+i*0x100,0x4000+i*0x100,32,0)+label+bytes(size-32-len(label))
    b=bytearray(0x8000+len(plist));struct.pack_into('<8I',b,0,0xfeedfacf,0x100000c,2,12,2+len(ROOTS),len(cmds),0,0);b[32:32+len(cmds)]=cmds
    for i in range(len(ROOTS)):struct.pack_into('<8I',b,0x4000+i*0x100,0xfeedfacf,0x100000c,2,2,0,0,0,0)
    b[0x8000:]=plist;return b

def reject(b):
    try:inspect(b)
    except ValueError:return
    raise AssertionError('malformed input accepted')
b=build();r=inspect(b);assert r['entry_count']==4 and r['metadata_count']==4
assert r['components'][ROOTS[0]]['transitive_executables']==sorted([ROOTS[0],ROOTS[-1]])
assert not r['components'][ROOTS[0]]['missing_metadata']
for n in (0,4,31,32,0x100,0x4000,0x4100,len(b)-1):reject(b[:n])
for offset,value in ((0,0),(12,2),(16,0xffffffff),(36,4),(32+72+4,0xffffffff)):
    c=bytearray(b);struct.pack_into('<I',c,offset,value);reject(c)
# Reject a file-backed segment larger than its declared VM mapping.
c=bytearray(b); struct.pack_into('<Q',c,32+32,0x800);reject(c)
# Reject a segment whose VM range wraps the 64-bit address space.
c=bytearray(b); struct.pack_into('<Q',c,32+24,0xfffffffffffff800);reject(c)
# Incorrect mapped fileset offset must not be accepted even if it points to another Mach-O.
c=bytearray(b); struct.pack_into('<Q',c,32+72+152+16,0x4100);reject(c)
print('PASS: KC inventory parses dependency closure and rejects truncated/malformed/incorrect mappings (synthetic)')
