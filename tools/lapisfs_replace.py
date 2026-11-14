#!/usr/bin/env python3
"""Replace the contents of an existing regular file in the lapis-root LAPISFS IN PLACE,
when the new data fits in the file's 16 KiB-aligned extent. All alias entries sharing the
same data extent (e.g. /etc/fstab mirror of /private/etc/fstab) get the new size too.
usage: lapisfs_replace.py IMG LAPISPATH HOSTFILE"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
PAGE = 0x4000
img, lpath, host = sys.argv[1:4]
data = open(host, 'rb').read()
with open(img, 'r+b') as f:
    root, rsize, _bps = [(p[1], p[2], p[3]) for p in gpt_partitions(f) if p[0] == 'lapis-root'][0]
    f.seek(root); hdr = f.read(40)
    magic, nent, ver, eo, so, total = struct.unpack('<8sIIQQQ', hdr)
    assert magic == b'LAPISFS1' and ver == 2
    f.seek(root + eo); E = [list(struct.unpack('<IHHQQII', f.read(32))) for _ in range(nent)]
    f.seek(root + so); S = f.read(max(e[5] + e[6] for e in E) + 1)
    def name(i): return S[E[i][5]:E[i][5] + E[i][6]].decode()
    def path(i):
        p = []
        while i: p.append(name(i)); i = E[i][0]
        return '/' + '/'.join(reversed(p))
    idx = [i for i in range(1, nent) if E[i][1] == 2 and path(i) == lpath]
    assert idx, 'not found: ' + lpath
    e = E[idx[0]]; off, old = e[3], e[4]
    cap = (old + PAGE - 1) // PAGE * PAGE or PAGE
    assert len(data) <= cap, 'does not fit (%d > %d)' % (len(data), cap)
    f.seek(root + off); f.write(data + b'\0' * (cap - len(data)))
    for i in range(1, nent):
        if E[i][1] == 2 and E[i][3] == off:
            E[i][4] = len(data); f.seek(root + eo + 32 * i); f.write(struct.pack('<IHHQQII', *E[i]))
            print('updated', path(i), old, '->', len(data))
