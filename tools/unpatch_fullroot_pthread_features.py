#!/usr/bin/env python3
"""Undo tools/patch_fullroot_pthread_features.py (#13) on a fullroot copy:
restore stock _pthread_workqueue_supported and the zero feature word."""
import sys, struct, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import patch_fullroot_pthread_features as P
from lapisfs_names import gpt_partitions, lapisfs_paths
img = sys.argv[1]
with open(img, 'r+b') as f:
    root = [p for p in gpt_partitions(f) if p[0] == 'lapis-root'][0][1]
    f.seek(root); _, _, paths = lapisfs_paths(f.read(1 << 20))
    done = set()
    for path, typ, off, size in paths:
        if typ != 2 or off in done: continue
        if path.endswith(P.TEXT_SUBCACHE):
            a = root + off + P.SITE_OFF; f.seek(a); cur = f.read(16)
            assert cur in (P.REPL_CODE, P.EXPECT_CODE), cur.hex()
            f.seek(a); f.write(P.EXPECT_CODE); done.add(off); print('code restored', path, hex(a))
        elif path.endswith(P.DATA_SUBCACHE):
            a = root + off + P.FEATURE_OFF; f.seek(a); cur = struct.unpack('<I', f.read(4))[0]
            assert cur in (0, P.REQUIRED_FEATURES), hex(cur)
            f.seek(a); f.write(b'\0\0\0\0'); done.add(off); print('data restored', path, hex(a))
