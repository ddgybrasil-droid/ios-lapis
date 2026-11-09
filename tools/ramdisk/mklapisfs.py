#!/usr/bin/env python3
"""Build a lapisfs image (see xnu/lapis-mockfs/lapis_mockfs.c for the format).
usage: mklapisfs.py OUT  DEST=SRC [DEST=SRC ...]  [--dir /path ...]
  DEST=SRC     file at absolute DEST from host file SRC (SRC may be a directory:
               then every regular file below it is added under DEST)
  DEST=@OTHER  alias: DEST shares OTHER's data (no copy)
Parent directories are created automatically (mode 0755)."""
import os, struct, sys, shutil
PAGE = 0x4000
def main():
    out = sys.argv[1]; args = sys.argv[2:]
    ents = [dict(parent=0, type=1, mode=0o755, name='', src=None)]   # root
    index = {'/': 0}
    def mkdir(path):
        path = path.rstrip('/') or '/'
        if path in index: return index[path]
        par = mkdir(os.path.dirname(path))
        ents.append(dict(parent=par, type=1, mode=0o755, name=os.path.basename(path), src=None))
        index[path] = len(ents) - 1; return index[path]
    def addfile(dest, src):
        par = mkdir(os.path.dirname(dest))
        st = os.stat(src)
        ents.append(dict(parent=par, type=2, mode=st.st_mode & 0o7777 or 0o644, name=os.path.basename(dest), src=src, size=st.st_size))
        index[dest] = len(ents) - 1
    aliases = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == '--dir': mkdir(args[i + 1]); i += 2; continue
        dest, src = a.split('=', 1)
        if src.startswith('@'): aliases.append((dest, src[1:]))
        elif os.path.isdir(src):
            for root, _, files in os.walk(src):
                for f in sorted(files):
                    p = os.path.join(root, f)
                    if os.path.isfile(p) and not os.path.islink(p):
                        addfile(os.path.join(dest, os.path.relpath(p, src)), p)
        else: addfile(dest, src)
        i += 1
    for dest, other in aliases:
        if other.endswith('/'):        # directory alias: alias every file below
            for path, idx in list(index.items()):
                if path.startswith(other) and ents[idx]['type'] == 2:
                    d = dest.rstrip('/') + '/' + path[len(other):]
                    par = mkdir(os.path.dirname(d))
                    ents.append(dict(ents[idx], parent=par, name=os.path.basename(d), alias=idx)); index[d] = len(ents) - 1
        else:
            idx = index[other]; par = mkdir(os.path.dirname(dest))
            ents.append(dict(ents[idx], parent=par, name=os.path.basename(dest), alias=idx)); index[dest] = len(ents) - 1
    strs = bytearray(); 
    for e in ents:
        e['name_off'] = len(strs); nb = e['name'].encode(); e['name_len'] = len(nb); strs += nb + b'\0'
    ent_off = 0x28; str_off = ent_off + 32 * len(ents)
    data = (str_off + len(strs) + PAGE - 1) & ~(PAGE - 1)
    for e in ents:
        if e['type'] == 2 and 'alias' not in e:
            e['data_off'] = data; data += (e['size'] + PAGE - 1) & ~(PAGE - 1)
    for e in ents:
        if 'alias' in e: e['data_off'] = ents[e['alias']]['data_off']
    total = max(data, 2 * PAGE)
    with open(out, 'wb') as f:
        f.write(struct.pack('<8sIIQQQ', b'LAPISFS1', len(ents), 2, ent_off, str_off, total))
        for e in ents:
            f.write(struct.pack('<IHHQQII', e['parent'], e['type'], e['mode'], e.get('data_off', 0), e.get('size', 0), e['name_off'], e['name_len']))
        f.write(strs)
        for e in ents:
            if e['type'] == 2 and 'alias' not in e:
                f.seek(e['data_off'])
                with open(e['src'], 'rb') as s: shutil.copyfileobj(s, f, 1 << 24)
        f.truncate(total)
    print(f'{out}: {len(ents)} entries, {total:#x} bytes ({total/2**20:.1f} MiB)')
main()
