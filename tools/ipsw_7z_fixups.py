#!/usr/bin/env python3
"""7-Zip's APFS reader leaves an empty placeholder for absolute symlinks and
for files with an unsupported compression method. From `7zz l -slt` output
and an extraction dir, write links.tsv (lapispath, target) and broken.txt."""
import os, sys
listing, root, links_out, broken_out = sys.argv[1:5]
meta = {}; cur = {}
for l in open(listing, errors="replace"):
    l = l.rstrip("\n")
    if not l:
        if "Path" in cur: meta[cur["Path"]] = cur
        cur = {}; continue
    if " = " in l:
        k, v = l.split(" = ", 1); cur[k] = v
nl = nb = 0
with open(links_out, "w") as L, open(broken_out, "w") as B:
    for r, ds, fs in os.walk(root):
        for f in fs:
            hp = os.path.join(r, f)
            if os.path.islink(hp) or os.path.getsize(hp): continue
            rel = os.path.relpath(hp, root); m = meta.get(rel, {})
            if m.get("Symbolic Link"):
                L.write(f"/{rel}\t{m['Symbolic Link']}\n"); nl += 1
            elif m.get("Size") not in (None, "", "0"):
                B.write(f"/{rel}\n"); nb += 1
print(f"links {nl}, broken {nb}")
