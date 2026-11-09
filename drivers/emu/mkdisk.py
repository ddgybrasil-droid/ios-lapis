#!/usr/bin/env python3
"""mkdisk.py OUT ROOTFS.lfs [BS] - sparse raw disk: protective MBR + GPT (bs 4096),
partitions: EFI (8 MiB), lapis-root (= ROOTFS, LAPISFS1 image copied in), lapis-data (64 MiB).
Same layout the IPSW-BOOT procedure asks for on the real tablet (partition name lapis-root)."""
import sys, struct, zlib, uuid, os
out, rootfs = sys.argv[1], sys.argv[2]; bs = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
rsz = os.path.getsize(rootfs); rblk = (rsz + bs - 1) // bs
NENT, ESZ = 128, 128; eblk = NENT * ESZ // bs
first = 2 + eblk
esp = (first, first + (8 << 20) // bs - 1)
root = (esp[1] + 1, esp[1] + rblk)
data = (root[1] + 1, root[1] + (64 << 20) // bs)
n = data[1] + 2 + eblk + 1
last = n - 2 - eblk
disk = uuid.UUID('6C617069-732D-7566-732D-726F6F746673')
T = {'EFI': uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B'), 'lapis-root': uuid.UUID('7C3457EF-0000-11AA-AA11-00306543ECAC'),
     'lapis-data': uuid.UUID('48465300-0000-11AA-AA11-00306543ECAC')}
parts = [('EFI',) + esp, ('lapis-root',) + root, ('lapis-data',) + data]
ents = bytearray(NENT * ESZ)
for i, (nm, f, l) in enumerate(parts):
    ents[i*ESZ:(i+1)*ESZ] = T[nm].bytes_le + uuid.uuid5(disk, nm).bytes_le + struct.pack('<QQQ', f, l, 0) + nm.encode('utf-16-le').ljust(72, b'\0')
def hdr(cur, alt, elba):
    h = bytearray(struct.pack('<8sIIIIQQQQ16sQIII', b'EFI PART', 0x10000, 92, 0, 0, cur, alt, first, last,
                              disk.bytes_le, elba, NENT, ESZ, zlib.crc32(ents)))
    struct.pack_into('<I', h, 16, zlib.crc32(h)); return bytes(h).ljust(bs, b'\0')
mbr = bytearray(bs); mbr[446:462] = struct.pack('<BBBBBBBBII', 0, 0, 2, 0, 0xEE, 0xff, 0xff, 0xff, 1, min(n - 1, 0xffffffff))
mbr[510:512] = b'\x55\xaa'
with open(out, 'wb') as fp:
    fp.truncate(n * bs)
    fp.write(mbr); fp.write(hdr(1, n - 1, 2)); fp.write(ents)
    fp.seek((n - 1 - eblk) * bs); fp.write(ents); fp.write(hdr(n - 1, 1, n - 1 - eblk))
    fp.seek(root[0] * bs)
    with open(rootfs, 'rb') as r:
        while True:
            b = r.read(16 << 20)
            if not b: break
            if b.count(0) == len(b): fp.seek(len(b), 1)   # keep sparse
            else: fp.write(b)
print(out, n, 'blocks x', bs, parts)
