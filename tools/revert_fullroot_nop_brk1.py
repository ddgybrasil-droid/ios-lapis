#!/usr/bin/env python3
"""Revert patch_fullroot_pac_strip.py --nop-brk1 on a fullroot copy: every
aligned word that is `brk #1` in the ORIGINAL image and NOP in the copy is
restored to `brk #1`. Writes the list of restored offsets (reversible)."""
import sys, numpy as np
orig, copy, listf = sys.argv[1:4]
BRK1, NOP = 0xD4200020, 0xD503201F
CH = 64 << 20
n = 0
with open(orig, 'rb') as fo, open(copy, 'r+b') as fc, open(listf, 'w') as L:
    pos = 0
    while True:
        a = fo.read(CH); b = fc.read(CH)
        if not a: break
        m = len(a) // 4 * 4
        wa = np.frombuffer(a[:m], '<u4'); wb = np.frombuffer(b[:m], '<u4')
        idx = np.nonzero((wa == BRK1) & (wb == NOP))[0]
        for i in idx:
            off = pos + int(i) * 4
            fc.seek(off); fc.write(BRK1.to_bytes(4, 'little')); L.write('%#x\n' % off)
        n += len(idx); pos += len(a); fc.seek(pos)
print('restored', n, 'brk #1 words')
