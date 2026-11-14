#!/usr/bin/env python3
"""Add files/dirs to the lapis-root LAPISFS of a fullroot GPT image, IN PLACE
(run it on a copy). lapisfs has no symlinks/readdir, so symlinks such as
/etc -> private/etc are emulated: a directory link is mirrored (every entry
below the target is added again under the link path, files as alias entries
sharing the same data); a file link becomes an alias entry.

usage: lapisfs_add.py IMG [options] [DEST=SRC ...]
  DEST=SRC        host file, or host dir (added recursively)
  --mirror L=T    add symlink L -> T
  --dir PATH      create an empty directory
  --exclude P     do not add lapis path P (or anything below it)
  --links-file F  TAB-separated "lapispath<TAB>target": symlinks the extractor
                  could not create (7-Zip leaves an empty placeholder file)
  --broken-list F lapis paths whose empty host copy is an extraction failure
  --grow-mb N     extend lapis-root by N MiB: the all-zero lapis-data
                  partition after it is shifted up, the image file extended if
                  needed and the backup GPT moved; GPT CRCs rewritten
Existing paths are kept (never replaced). New entries are appended (existing
indices unchanged); the string table is rebuilt. The kernel reads metadata as
one contiguous prefix [0, end of strings) (max 32 MiB), so if it would overlap
the first data extents those extents are moved to the end of the image.
New file data goes 16 KiB-aligned after the old LAPISFS `total`."""
import os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions
PAGE = 0x4000

def gpt_grow(f, grow_bytes):
    bps = 4096
    f.seek(bps); hdr = bytearray(f.read(bps))
    assert hdr[:8] == b"EFI PART"
    hsz = struct.unpack_from("<I", hdr, 12)[0]
    alt = struct.unpack_from("<Q", hdr, 32)[0]
    ent_lba, nent, esz = struct.unpack_from("<QII", hdr, 72)
    eblocks = (nent * esz + bps - 1) // bps
    f.seek(ent_lba * bps); ents = bytearray(f.read(nent * esz))
    def name(i): return ents[i*esz+56:i*esz+128].decode("utf-16le").split("\0")[0]
    idx = {name(i): i for i in range(nent) if any(ents[i*esz:i*esz+16])}
    r, d = idx["lapis-root"], idx["lapis-data"]
    rf, rl = struct.unpack_from("<QQ", ents, r*esz+32)
    df, dl = struct.unpack_from("<QQ", ents, d*esz+32)
    nb = grow_bytes // bps
    assert df == rl + 1, "lapis-data must follow lapis-root"
    f.seek(df * bps)
    for _ in range(dl - df + 1):
        assert not any(f.read(bps)), "lapis-data is not all zero"
    dlen = dl - df + 1
    last_usable = struct.unpack_from("<Q", hdr, 48)[0]
    new_df = df + nb; new_dl = new_df + dlen - 1
    if new_dl > last_usable:
        new_last_usable = new_dl
        new_alt = new_last_usable + eblocks + 1
        f.seek(alt * bps); f.write(b"\0" * bps)
        f.truncate((new_alt + 1) * bps)
    else:
        new_last_usable, new_alt = last_usable, alt
    struct.pack_into("<Q", ents, r*esz+40, rl + nb)
    struct.pack_into("<QQ", ents, d*esz+32, new_df, new_dl)
    ecrc = zlib.crc32(ents) & 0xffffffff
    for my, other, elba in ((1, new_alt, ent_lba), (new_alt, 1, new_alt - eblocks)):
        h = bytearray(hdr)
        struct.pack_into("<QQ", h, 24, my, other)
        struct.pack_into("<Q", h, 48, new_last_usable)
        struct.pack_into("<Q", h, 72, elba)
        f.seek(elba * bps); f.write(ents)
        struct.pack_into("<I", h, 88, ecrc); struct.pack_into("<I", h, 16, 0)
        struct.pack_into("<I", h, 16, zlib.crc32(bytes(h[:hsz])) & 0xffffffff)
        f.seek(my * bps); f.write(h)
    print(f"GPT: lapis-root LBA {rf}..{rl + nb} (+{nb} blocks), lapis-data {new_df}..{new_dl}, backup GPT @ {new_alt}")

def main():
    a = sys.argv[1:]; img = a.pop(0)
    grow = 0; files = []; links = []; dirs = []; excludes = []; broken = set(); extra_links = {}
    while a:
        x = a.pop(0)
        if x == "--grow-mb": grow = int(a.pop(0)) << 20
        elif x == "--mirror": links.append(a.pop(0).split("=", 1))
        elif x == "--dir": dirs.append(a.pop(0))
        elif x == "--exclude": excludes.append(a.pop(0))
        elif x == "--links-file":
            for l in open(a.pop(0)):
                if l.strip(): k, v = l.rstrip("\n").split("\t"); extra_links[k] = v
        elif x == "--broken-list": broken.update(l.strip() for l in open(a.pop(0)) if l.strip())
        else: files.append(x.split("=", 1))
    with open(img, "r+b") as f:
        if grow: gpt_grow(f, grow)
        root, rsize = [(p[1], p[2]) for p in gpt_partitions(f) if p[0] == "lapis-root"][0]
        f.seek(root); head = f.read(0x28)
        magic, nent, ver, eo, so, total = struct.unpack_from("<8sIIQQQ", head)
        assert magic == b"LAPISFS1" and ver == 2
        f.seek(root); meta = f.read(min(so + (1 << 22), 1 << 25))
        E = [list(struct.unpack_from("<IHHQQII", meta, eo + 32*i)) for i in range(nent)]
        own = []   # LFSOWN01 owner table (tools/lapisfs_owners.py): kept, new entries 0:0
        if eo >= 0x40 and meta[0x28:0x30] == b"LFSOWN01":
            ocnt, _, otoff = struct.unpack_from("<IIQ", meta, 0x30)
            own = [struct.unpack_from("<II", meta, otoff + 8*i) for i in range(min(ocnt, nent))]
        names = [meta[so+e[5]:so+e[5]+e[6]].decode() for e in E]
        first_data = min(e[3] for e in E if e[1] == 2 and e[4])
        index = {"/": 0}
        def path_of(i):
            p = []
            while i: p.append(names[i]); i = E[i][0]
            return "/" + "/".join(reversed(p))
        for i in range(1, nent): index[path_of(i)] = i
        data_end = (total + PAGE - 1) & ~(PAGE - 1)
        pending = []
        kept = 0
        def add(path, typ, mode, off=0, size=0):
            nonlocal kept
            path = "/" + path.strip("/")   # accept relative DEST (e.g. private/etc/x)
            if path in index:
                i = index[path]
                if typ == 1:
                    assert E[i][1] == 1, path; return i
                kept += 1; return None
            par = mkdir(os.path.dirname(path))
            E.append([par, typ, mode, off, size, 0, 0]); names.append(os.path.basename(path))
            index[path] = len(E) - 1; return len(E) - 1
        def mkdir(path): return add(path, 1, 0o755)
        def addfile(dest, src):
            nonlocal data_end
            st = os.stat(src)
            if add(dest, 2, st.st_mode & 0o7777 or 0o644, data_end, st.st_size) is None: return
            pending.append((data_end, src)); data_end += (st.st_size + PAGE - 1) & ~(PAGE - 1)
        def excluded(p): return any(p == x or p.startswith(x.rstrip("/") + "/") for x in excludes)
        for d in dirs: mkdir(d)
        hosts = []
        for dest, src in files:
            if os.path.isdir(src):
                hosts.append(os.path.abspath(src))
                for r, ds, fs in os.walk(src):
                    rel = os.path.relpath(r, src); base = dest if rel == "." else os.path.join(dest, rel)
                    if excluded(base): ds[:] = []; continue
                    mkdir(base)
                    for nm in sorted(ds + fs):
                        hp = os.path.join(r, nm); lp = os.path.join(base, nm)
                        if excluded(lp): continue
                        if lp in extra_links or os.path.islink(hp):
                            t = extra_links.get(lp) or os.readlink(hp)
                            links.append([lp, t if t.startswith("/") else os.path.normpath(os.path.join(base, t))])
                        elif nm in fs:
                            if os.path.getsize(hp) == 0 and lp in broken:
                                print("skip unextracted", lp); continue
                            addfile(lp, hp)
            elif not excluded(dest): addfile(dest, src)
        for lt in links:   # 7-Zip rewrites some absolute targets to the host dir
            for h in hosts:
                if lt[1].startswith(h + "/"): lt[1] = lt[1][len(h):]
        lmap = {l.rstrip("/"): os.path.normpath(t) for l, t in links}
        def canon(t, depth=0):
            t = os.path.normpath(t)
            if depth > 16: return t
            for l in sorted(lmap, key=len, reverse=True):
                if t == l or t.startswith(l + "/"):
                    return canon(lmap[l] + t[len(l):], depth + 1)
            return t
        dangling = 0
        for link, target in sorted(links, key=lambda x: -x[0].count("/")):
            ct = canon(target)
            if ct in index and E[index[ct]][1] == 2:
                i = index[ct]; add(link, 2, E[i][2], E[i][3], E[i][4]); continue
            if ct not in index:
                dangling += 1; print("skip dangling symlink", link, "->", target); continue
            mkdir(link)
            for p, i in sorted(index.items()):
                if p.startswith(ct + "/"):
                    np = link.rstrip("/") + p[len(ct):]
                    if E[i][1] == 1: mkdir(np)
                    else: add(np, 2, E[i][2], E[i][3], E[i][4])
        strs = bytearray()
        for i, e in enumerate(E):
            nb = names[i].encode(); e[5] = len(strs); e[6] = len(nb); strs += nb + b"\0"
        neo = 0x28 if not own else (0x40 + 8 * len(E) + 7) & ~7
        nso = neo + 32 * len(E); end = nso + len(strs)
        assert end <= 0x2000000, "metadata over the kernel's 32 MiB limit"
        relocs = []
        while end > first_data:
            size = max(e[4] for e in E if e[1] == 2 and e[3] == first_data)
            for e in E:
                if e[1] == 2 and e[3] == first_data: e[3] = data_end
            relocs.append((first_data, data_end, size))
            data_end += (size + PAGE - 1) & ~(PAGE - 1)
            first_data = min(e[3] for e in E if e[1] == 2 and e[4])
        assert data_end <= rsize, f"data end {data_end:#x} > partition {rsize:#x} (use --grow-mb)"
        for old, new, size in relocs:
            f.seek(root + old); left = size; o = 0
            while left:
                n = min(left, 1 << 24); f.seek(root + old + o); buf = f.read(n)
                f.seek(root + new + o); f.write(buf); left -= n; o += n
            print(f"relocated data extent {old:#x} -> {new:#x} ({size} bytes)")
        for e in E:   # zero-size files: point at a valid offset
            if e[1] == 2 and not e[4] and e[3] < first_data: e[3] = first_data
        out = bytearray(struct.pack("<8sIIQQQ", b"LAPISFS1", len(E), 2, neo, nso, data_end))
        if own:
            out += struct.pack("<8sIIQ", b"LFSOWN01", len(E), 0, 0x40)
            for i in range(len(E)): out += struct.pack("<II", *(own[i] if i < len(own) else (0, 0)))
            out += b"\0" * (neo - len(out))
        for e in E: out += struct.pack("<IHHQQII", *e)
        out += strs; out += b"\0" * (first_data - len(out))
        for off, src in pending:
            with open(src, "rb") as s:
                f.seek(root + off)
                while True:
                    buf = s.read(1 << 24)
                    if not buf: break
                    f.write(buf)
        f.seek(root); f.write(out)
        print(f"LAPISFS: {nent} -> {len(E)} entries (kept {kept} existing, {dangling} dangling links skipped), "
              f"metadata {end:#x}/{first_data:#x}, total {total:#x} -> {data_end:#x}")

main()
