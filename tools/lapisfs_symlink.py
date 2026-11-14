#!/usr/bin/env python3
"""Create real symlinks in the lapis-root LAPISFS of a fullroot image, IN PLACE.
A lapisfs symlink is a regular (type 2) entry whose mode has S_IFLNK (0120755); its 16 KiB
data extent holds the target (kernel: lapisfs_vtype -> VLNK, lapisfs_readlink).
  existing file  -> converted in place (new data extent)
  existing dir   -> (an emulated mirror made by lapisfs_add.py --mirror) moved, with its
                    subtree, under /private/.lapis-mirrors/<flattened path>; new link entry added
  missing        -> new link entry added (parent must exist)
Kernel rule: parent index < child index. New entries are appended (indices unchanged); string table rebuilt after the entry table and
must end before the first data extent; link data goes 16K-aligned at the old `total`.
usage: lapisfs_symlink.py IMG LINK=TARGET ..."""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
PAGE = 0x4000; LNK = 0o120755
img = sys.argv[1]; links = [a.split('=', 1) for a in sys.argv[2:]]
with open(img, 'r+b') as f:
    root, rsize = [(p[1], p[2]) for p in gpt_partitions(f) if p[0] == 'lapis-root'][0]
    f.seek(root); magic, nent, ver, eo, so, total = struct.unpack('<8sIIQQQ', f.read(40))
    assert magic == b'LAPISFS1' and ver == 2 and eo == 40
    f.seek(root + eo); E = [list(struct.unpack('<IHHQQII', f.read(32))) for _ in range(nent)]
    f.seek(root + so); S = f.read(max(e[5] + e[6] for e in E) + 1)
    names = [S[e[5]:e[5] + e[6]].decode() for e in E]
    def path(i):
        p = []
        while i: p.append(names[i]); i = E[i][0]
        return '/' + '/'.join(reversed(p))
    P = {path(i): i for i in range(1, len(E))}
    first_data = min(e[3] for e in E if e[1] == 2 and e[4])
    def add(parent, typ, mode, nm):
        E.append([parent, typ, mode, 0, 0, 0, 0]); names.append(nm); return len(E) - 1
    def setdata(i, data):
        global total
        off = (total + PAGE - 1) // PAGE * PAGE
        assert off + PAGE <= rsize, 'lapis-root full'
        f.seek(root + off); f.write(data + b'\0' * (PAGE - len(data)))
        total = off + PAGE; E[i][3], E[i][4] = off, len(data)
    hold = None
    for link, target in links:
        par, nm = os.path.split(link.rstrip('/'))
        pi = P.get(par, 0) if par != '/' else 0
        assert par == '/' or par in P, 'no parent ' + par
        i = P.get(link)
        if i is not None and E[i][1] == 2:
            E[i][2] = LNK; setdata(i, target.encode()); print('converted file', link, '->', target); continue
        if i is not None:
            # the kernel requires parent index < child index: the first moved dir becomes the
            # holder itself (renamed /private/.lapis-mirrors), later ones go below it if allowed
            if hold is None:
                hold = P.get('/private/.lapis-mirrors')
                if hold is None:
                    assert P['/private'] < i
                    E[i][0] = P['/private']; names[i] = '.lapis-mirrors'; hold = i
                    print('mirror dir', link, 'renamed /private/.lapis-mirrors'); j = add(pi, 2, LNK, nm)
                    setdata(j, target.encode()); print('symlink', link, '->', target, 'idx', j); continue
            if hold < i: E[i][0] = hold; names[i] = link.strip('/').replace('/', '_'); where = '/private/.lapis-mirrors/'
            else: names[i] = '.lapis-mirror.' + nm; where = par + '/'
            print('moved mirror dir', link, '->', where + names[i])
        j = add(pi, 2, LNK, nm); setdata(j, target.encode()); print('symlink', link, '->', target, 'idx', j)
    # rebuild strings
    so = eo + 32 * len(E); sb = bytearray()
    for k, e in enumerate(E):
        b = names[k].encode(); e[5], e[6] = len(sb), len(b); sb += b + b'\0'
    assert so + len(sb) + 1 <= first_data, 'metadata would overlap data'
    f.seek(root); f.write(struct.pack('<8sIIQQQ', magic, len(E), ver, eo, so, total))
    f.seek(root + eo); f.write(b''.join(struct.pack('<IHHQQII', *e) for e in E))
    f.write(bytes(sb) + b'\0')
    print('nent', nent, '->', len(E), 'strings end', so + len(sb), 'first data', first_data, 'total', total)
