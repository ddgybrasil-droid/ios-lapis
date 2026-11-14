#!/usr/bin/env python3
"""Write the LAPISFS v2 owner table (backward-compatible extension, read by the kernel's
lapisfs_getattr in xnu/lapis-ramfs) IN PLACE, from IPSW filesystem metadata.
Layout (metadata prefix, header ent_off moved past it): 0x28 "LFSOWN01", u32 count, u32 0,
u64 table_off=0x40; table: count x {u32 uid, u32 gid} indexed by entry number; then entries,
strings. Old kernels/tools that honour ent_off ignore it.
METADATA: TSV "path<TAB>mode<TAB>uid<TAB>gid" (paths relative to the dmg root), e.g. from
  7zz l -slt OS.dmg | awk '/^Path = /{p=substr($0,8)} /^Mode = /{m=substr($0,8)}
    /^User ID = /{u=substr($0,11)} /^Group ID = /{g=substr($0,12); print p"\\t"m"\\t"u"\\t"g}'
Every lapisfs entry whose path exists in METADATA gets its uid/gid; entries not present stay
0:0 (root:wheel). With --modes PREFIX (repeatable) the permission bits (incl. setuid/sticky)
are also taken from METADATA for entries under PREFIX (directory/regular type kept).
usage: lapisfs_owners.py IMG METADATA [--modes /private/var ...]"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
PAGE = 0x4000
def parse_mode(s):
    if not s or len(s) < 10: return None
    bits = 0
    for i, c in enumerate(s[1:10]):
        if c != '-': bits |= 1 << (8 - i)
    if s[3] in 'sS': bits |= 0o4000
    if s[6] in 'sS': bits |= 0o2000
    if s[9] in 'tT': bits |= 0o1000
    for i, c in ((3, s[3]), (6, s[6]), (9, s[9])):
        if c in 'ST': bits &= ~(1 << (9 - i))   # S/T: set-id/sticky without x
    return bits
args = sys.argv[1:]; img, meta = args[0], args[1]; modes = []
a = args[2:]
while a:
    x = a.pop(0)
    if x == '--modes': modes.append(a.pop(0).rstrip('/'))
    else: sys.exit('bad arg ' + x)
M = {}
for line in open(meta, encoding='utf-8', errors='surrogateescape'):
    p = line.rstrip('\n').split('\t')
    if len(p) == 4 and p[2].isdigit() and p[3].isdigit(): M['/' + p[0]] = (p[1], int(p[2]), int(p[3]))
with open(img, 'r+b') as f:
    parts = gpt_partitions(f)
    pr = [p for p in parts if p[0] == 'lapis-root'][0]; root = pr[1]
    rsize = pr[2] if len(pr) > 2 else None
    f.seek(root); magic, nent, ver, eo, so, total = struct.unpack('<8sIIQQQ', f.read(40))
    assert magic == b'LAPISFS1' and ver == 2 and eo >= 40
    f.seek(root + eo); E = [list(struct.unpack('<IHHQQII', f.read(32))) for _ in range(nent)]
    f.seek(root + so); S = f.read(max(e[5] + e[6] for e in E) + 1)
    cache = {0: ''}
    def path(i):
        if i not in cache: cache[i] = path(E[i][0]) + '/' + S[E[i][5]:E[i][5] + E[i][6]].decode('utf-8', 'surrogateescape')
        return cache[i]
    tab = bytearray(struct.pack('<8sII', b'LFSOWN01', nent, 0)) + bytearray(8 * nent)
    nown = nmode = nmatch = 0
    for i in range(1, nent):
        p = path(i); m = M.get(p)
        if not m: continue
        nmatch += 1
        struct.pack_into('<II', tab, 16 + 8 * i, m[1], m[2])
        if m[1] or m[2]: nown += 1
        if any(p == x or p.startswith(x + '/') for x in modes) and (E[i][2] & 0o170000) != 0o120000:
            b = parse_mode(m[0])
            if b is not None and (E[i][2] & 0o7777) != b:
                E[i][2] = (E[i][2] & 0o170000) | b
                f.seek(root + eo + 32 * i + 6); f.write(struct.pack('<H', E[i][2])); nmode += 1
    # undo the first (rejected) layout: table hung off the root entry at the image end
    # (kernel lapisfs mount requires root data_off/size == 0) -> restore root entry and total
    if E[0][3] or E[0][4]:
        if E[0][3] + E[0][4] <= total and E[0][3] >= so: total = E[0][3]
        E[0][3] = E[0][4] = 0
    # metadata prefix: header(0x28) | ext(0x18) | owner table | entries | strings
    strs_len = max(e[5] + e[6] for e in E) + 1
    f.seek(root + so); strs = f.read(strs_len)
    first_data = min(e[3] for e in E if e[1] == 2 and e[4])
    toff = 0x40; neo = (toff + 8 * nent + 7) & ~7; nso = neo + 32 * nent
    assert nso + strs_len <= first_data, 'metadata would overlap file data (grow/relocate with lapisfs_add first)'
    out = bytearray(struct.pack('<8sIIQQQ', b'LAPISFS1', nent, 2, neo, nso, total))
    out += struct.pack('<8sIIQ', b'LFSOWN01', nent, 0, toff) + tab[16:]
    out += b'\0' * (neo - len(out))
    for e in E: out += struct.pack('<IHHQQII', *e)
    out += strs
    f.seek(root); f.write(out)
    print(f'owner table @{toff:#x} ({8 * nent} bytes, {nent} entries): {nmatch} matched in metadata, '
          f'{nown} non-root owners, {nmode} modes updated; LAPISFS total {total:#x}')
