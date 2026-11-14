#!/usr/bin/env python3
"""Set mode 0755 on every regular (non-symlink) LAPISFS file under PREFIX whose data starts with a
Mach-O magic (thin cffaedfe / fat cafebabe) and that is not already executable, IN PLACE.
Needed for files decoded by an earlier decmpfs_decode_tree.py that lost the exec bit (launchd:
"service will always use xpcproxy because its binary is missing or not executable"). iOS
ships Mach-O executables and dylibs as 0755.
usage: lapisfs_fix_macho_modes.py IMG PREFIX [PREFIX...]"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
img, prefixes = sys.argv[1], sys.argv[2:]
with open(img, 'r+b') as f:
    root = [p[1] for p in gpt_partitions(f) if p[0] == 'lapis-root'][0]
    f.seek(root); magic, nent, ver, eo, so, total = struct.unpack('<8sIIQQQ', f.read(40))
    assert magic == b'LAPISFS1' and eo == 40
    f.seek(root + eo); E = [struct.unpack('<IHHQQII', f.read(32)) for _ in range(nent)]
    f.seek(root + so); S = f.read(max(e[5] + e[6] for e in E) + 1)
    cache = {0: ''}
    def path(i):
        if i not in cache:
            cache[i] = path(E[i][0]) + '/' + S[E[i][5]:E[i][5] + E[i][6]].decode()
        return cache[i]
    n = 0
    for i in range(1, nent):
        par, typ, mode, off, size, no, nl = E[i]
        p = path(i)
        if typ != 2 or (mode & 0o170000) == 0o120000 or size < 4 or (mode & 0o111):
            continue
        if not any(p == x or p.startswith(x.rstrip('/') + '/') for x in prefixes):
            continue
        f.seek(root + off)
        if f.read(4) in (b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe'):
            f.seek(root + eo + i * 32 + 6); f.write(struct.pack('<H', (mode & 0o170000) | 0o755)); n += 1
    print('set 0755 on', n, 'Mach-O files')
