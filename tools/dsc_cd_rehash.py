#!/usr/bin/env python3
"""Recompute the code-slot page hashes of every dyld shared cache (sub)cache file in the
lapis-root LAPISFS of a fullroot image, IN PLACE (only changed hash slots are written).
Needed after in-place code patches of the cache (patch_fullroot_pac_strip.py,
reseal_corecrypto_fips_hmac.py): the kernel validates every cache page it pages in against
the cache's CodeDirectory slots (cs_invalid_page -> SIGKILL for CS_KILL processes).
dyld_cache_header: codeSignatureOffset @0x28, codeSignatureSize @0x30.
usage: dsc_cd_rehash.py IMG [--write]   (without --write: report only)"""
import struct, sys, os, mmap, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
img = sys.argv[1]; write = '--write' in sys.argv
H = {1: hashlib.sha1, 2: hashlib.sha256, 3: hashlib.sha256, 4: hashlib.sha384}
DIR = '/System/Library/Caches/com.apple.dyld/'
f = open(img, 'r+b' if write else 'rb')
root = [p[1] for p in gpt_partitions(f) if p[0] == 'lapis-root'][0]
mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_WRITE if write else mmap.ACCESS_READ)
magic, nent, ver, eo, so, total = struct.unpack_from('<8sIIQQQ', mm, root)
E = [struct.unpack_from('<IHHQQII', mm, root + eo + 32 * i) for i in range(nent)]
S = mm[root + so: root + so + max(e[5] + e[6] for e in E) + 1]
def path(i):
    p = []
    while i: p.append(S[E[i][5]:E[i][5] + E[i][6]].decode()); i = E[i][0]
    return '/' + '/'.join(reversed(p))
grand = 0
for i in range(1, nent):
    e = E[i]
    if e[1] != 2 or (e[2] & 0o170000) == 0o120000: continue
    p = path(i)
    if not p.startswith(DIR + 'dyld_shared_cache_') or p.endswith(('.atlas', '.map')): continue
    base = root + e[3]; size = e[4]
    if mm[base:base + 6] != b'dyld_v': continue
    cso, css = struct.unpack_from('<QQ', mm, base + 0x28)
    if not cso or cso + css > size: print(p, 'no signature'); continue
    sb = base + cso
    m, ln, cnt = struct.unpack_from('>III', mm, sb)
    assert m == 0xfade0cc0, p
    nch = 0; ncd = 0
    for k in range(cnt):
        typ, boff = struct.unpack_from('>II', mm, sb + 12 + 8 * k)
        cd = sb + boff
        if struct.unpack_from('>I', mm, cd)[0] != 0xfade0c02: continue
        (cmag, clen, cver, flags, hoff, ioff, nspecial, ncode, climit,
         hsize, htype, plat, pgshift) = struct.unpack_from('>IIIIIIIIIBBBB', mm, cd)
        if cver >= 0x20300:
            c64 = struct.unpack_from('>Q', mm, cd + 0x38)[0]
            if c64: climit = c64
        pg = 1 << pgshift; h = H[htype]; ncd += 1
        for s in range(ncode):
            a = s * pg; b = min(a + pg, climit)
            d = h(mm[base + a: base + b]).digest()[:hsize]
            slot = cd + hoff + s * hsize
            if mm[slot:slot + hsize] != d:
                nch += 1
                if write: mm[slot:slot + hsize] = d
    if nch: print('%s: %d CDs, %d slots %s' % (p, ncd, nch, 'rewritten' if write else 'stale'))
    grand += nch
if write: mm.flush()
print('total stale slots', grand, '(written)' if write else '(dry run)')
