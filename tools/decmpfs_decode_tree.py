#!/usr/bin/env python3
"""Decode APFS transparent compression left behind by 7-Zip when extracting an APFS dmg
(e.g. the SystemOS cryptex 094-55686-100.dmg): 7-Zip writes the logical file as 0 bytes plus
NAME:com.apple.decmpfs (16-byte 'fpmc' header: type u32, size u64) and
NAME:com.apple.ResourceFork. Types handled: 14 (LZBITMAP, resource fork; via libzbitmap
`dec14`), 13 (inline, stored block), 3 (zlib inline), 4 (zlib resource fork).
Each decoded file must match the header size; then the two side files are removed.
usage: decmpfs_decode_tree.py DIR [--dec14 /workspace/tools/libzbitmap/dec14]
Recipe (chunk 11): ipsw fw aea -o S 094-55686-100.dmg.aea;
  7zz x -oX S/094-55686-100.dmg '-x!System/Library/Caches/com.apple.dyld';
  decmpfs_decode_tree.py X; ln -s libstdc++.6.dylib X/usr/lib/libstdc++.dylib (7z refuses link-via-link);
  lapisfs_add.py IMG --grow-mb 192 /System/Cryptexes/OS=X"""
import os, struct, subprocess, sys, zlib, collections
top = sys.argv[1]
dec14 = sys.argv[sys.argv.index('--dec14') + 1] if '--dec14' in sys.argv else '/workspace/tools/libzbitmap/dec14'
types = collections.Counter(); fails = []
for root, ds, fs in os.walk(top):
    for f in fs:
        if not f.endswith(':com.apple.decmpfs'):
            continue
        xa = os.path.join(root, f); base = xa[:-len(':com.apple.decmpfs')]; rs = base + ':com.apple.ResourceFork'
        h = open(xa, 'rb').read(); t, sz = struct.unpack('<IQ', h[4:16]); types[t] += 1; ok = False
        if t == 14:
            ok = subprocess.run([dec14, xa, rs, base + '.dec'], capture_output=True).returncode == 0
        elif t in (3, 13):
            d = h[16:]
            if d[0] & 0x0f == 0x0f:
                out = d[1:]
            elif t == 3:
                out = zlib.decompress(d)
            else:
                out = None
            if out is not None:
                open(base + '.dec', 'wb').write(out); ok = True
        elif t == 4:
            r = open(rs, 'rb').read(); doff = struct.unpack('>I', r[:4])[0]; data = r[doff:]
            cnt = struct.unpack('<I', data[4:8])[0]; out = bytearray()
            for i in range(cnt):
                o, l = struct.unpack('<II', data[8 + i * 8:16 + i * 8]); blk = data[4 + o:4 + o + l]
                out += blk[1:] if blk[0] & 0x0f == 0x0f else zlib.decompress(blk)
            open(base + '.dec', 'wb').write(out); ok = True
        if ok and os.path.getsize(base + '.dec') == sz:
            os.replace(base + '.dec', base); os.remove(xa)
            if os.path.exists(rs):
                os.remove(rs)
        else:
            fails.append((base, t))
print('types', dict(types), 'failed', len(fails))
for b, t in fails:
    print('FAIL', t, b)
sys.exit(1 if fails else 0)
