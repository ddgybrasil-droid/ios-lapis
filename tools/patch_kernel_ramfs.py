#!/usr/bin/env python3
"""Inject lapis-ramfs (xnu/lapis-ramfs/ramfs.bin) into the xnu-12377 Image and route
bsd_init's mountroot call through lapis_ramfs_wrap (registers vfs "tmpfs", then tail-calls
the existing lapisfs register+mountroot wrapper). usage: IN OUT"""
import sys, struct, subprocess, os
BASE = 0xFFFFFE0007004000; FOFF = 0x175B0
def off(va): return va - BASE + FOFF
BLOB_VA, BLOB_END = 0xfffffe000743e100, 0xfffffe00074443d8
CALL_SITE, OLD_TGT = 0xfffffe00078a5a94, 0xfffffe000743d9e8
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'xnu', 'lapis-ramfs')
blob = open(os.path.join(d, 'ramfs.bin'), 'rb').read()
wrap = int([l.split()[0] for l in subprocess.check_output(['aarch64-linux-gnu-nm', os.path.join(d, 'ramfs.elf')], text=True).splitlines() if l.endswith(' lapis_ramfs_wrap')][0], 16)
img = bytearray(open(sys.argv[1], 'rb').read())
assert BLOB_VA + len(blob) <= BLOB_END
region = img[off(BLOB_VA):off(BLOB_VA) + len(blob)]
assert region == bytes(len(blob)) or region == blob, 'target region not free'
img[off(BLOB_VA):off(BLOB_VA) + len(blob)] = blob
def bl(src, dst): return 0x94000000 | (((dst - src) // 4) & 0x3ffffff)
cur = struct.unpack_from('<I', img, off(CALL_SITE))[0]
assert cur in (bl(CALL_SITE, OLD_TGT), bl(CALL_SITE, wrap)), hex(cur)
struct.pack_into('<I', img, off(CALL_SITE), bl(CALL_SITE, wrap))
open(sys.argv[2], 'wb').write(img)
print('ramfs %d bytes @%#x, wrap %#x, call site %#x -> bl wrap' % (len(blob), BLOB_VA, wrap, CALL_SITE))
