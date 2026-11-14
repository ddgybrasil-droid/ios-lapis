#!/usr/bin/env python3
"""Recompute the code-slot page hashes of every CodeDirectory in a thin Mach-O's embedded
signature (LC_CODE_SIGNATURE), for files whose code pages were patched in place.
Special slots (Info.plist, requirements, entitlements) are left as is; the CMS blob is not
re-signed, so the cdhash changes (the kernel validates pages against the CD slots).
usage: macho_cd_rehash.py IN OUT   (prints the slots that changed)"""
import struct, sys, hashlib
src, dst = sys.argv[1:3]
b = bytearray(open(src, 'rb').read())
magic, cpu, sub, ft, ncmds, sz, fl, _ = struct.unpack_from('<IiiIIIII', b, 0)
assert magic == 0xfeedfacf, 'need thin 64-bit Mach-O'
off = 32; sig = None
for _ in range(ncmds):
    cmd, csz = struct.unpack_from('<II', b, off)
    if cmd == 0x1d: sig = struct.unpack_from('<II', b, off + 8)
    off += csz
assert sig, 'no LC_CODE_SIGNATURE'
so, ss = sig
m, ln, cnt = struct.unpack_from('>III', b, so)
assert m == 0xfade0cc0
H = {1: hashlib.sha1, 2: hashlib.sha256, 3: hashlib.sha256, 4: hashlib.sha384}
total = 0
for k in range(cnt):
    typ, boff = struct.unpack_from('>II', b, so + 12 + 8 * k)
    cd = so + boff
    if struct.unpack_from('>I', b, cd)[0] != 0xfade0c02: continue
    (cmag, clen, ver, flags, hoff, ioff, nspecial, ncode, climit,
     hsize, htype, plat, pgshift) = struct.unpack_from('>IIIIIIIIIBBBB', b, cd)
    if ver >= 0x20300:
        climit64 = struct.unpack_from('>Q', b, cd + 0x38)[0] if ver >= 0x20300 else 0
        if climit64: climit = climit64
    pg = 1 << pgshift if pgshift else climit
    h = H[htype]; changed = []
    for i in range(ncode):
        start = i * pg; end = min(start + pg, climit)
        d = h(bytes(b[start:end])).digest()[:hsize]
        slot = cd + hoff + i * hsize
        if bytes(b[slot:slot + hsize]) != d:
            b[slot:slot + hsize] = d; changed.append(i)
    total += len(changed)
    print('CD type %#x hash %d page %#x slots %d: rehashed %d %s' % (typ, htype, pg, ncode, len(changed),
          [hex(i * pg) for i in changed[:8]] + (['...'] if len(changed) > 8 else [])))
open(dst, 'wb').write(b)
print('total slots rehashed', total)
