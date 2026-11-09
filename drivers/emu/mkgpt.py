#!/usr/bin/env python3
"""mkgpt.py OUT SIZE_MiB [BS] - raw disk with protective MBR + GPT (UEFI 2.x ch.5) using a
logical block size BS (default 4096, the QEMU ufs-lu block size) and 3 test partitions.
Each partition's first block is stamped with its name so reads can be checked."""
import sys, struct, zlib, uuid
out, mib = sys.argv[1], int(sys.argv[2]); bs = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
n = mib * 1024 * 1024 // bs
NENT, ESZ = 128, 128
eblk = NENT * ESZ // bs
first, last = 2 + eblk, n - 2 - eblk
HFS = uuid.UUID('48465300-0000-11AA-AA11-00306543ECAC'); APFS = uuid.UUID('7C3457EF-0000-11AA-AA11-00306543ECAC')
ESP = uuid.UUID('C12A7328-F81F-11D2-BA4B-00A0C93EC93B')
parts = [('EFI', ESP, first, first + 2047), ('lapis-root', APFS, first + 2048, first + 2048 + 4095),
         ('lapis-data', HFS, first + 6144, last)]
disk = uuid.UUID('6C617069-732D-7566-732D-746573743031')
ents = bytearray(NENT * ESZ)
for i, (nm, t, f, l) in enumerate(parts):
    ents[i*ESZ:(i+1)*ESZ] = t.bytes_le + uuid.uuid5(disk, nm).bytes_le + struct.pack('<QQQ', f, l, 0) + nm.encode('utf-16-le').ljust(72, b'\0')
def hdr(cur, alt, elba):
    h = bytearray(struct.pack('<8sIIIIQQQQ16sQIII', b'EFI PART', 0x10000, 92, 0, 0, cur, alt, first, last,
                              disk.bytes_le, elba, NENT, ESZ, zlib.crc32(ents)))
    struct.pack_into('<I', h, 16, zlib.crc32(h)); return bytes(h).ljust(bs, b'\0')
mbr = bytearray(bs); mbr[446:462] = struct.pack('<BBBBBBBBII', 0, 0, 2, 0, 0xEE, 0xff, 0xff, 0xff, 1, min(n - 1, 0xffffffff))
mbr[510:512] = b'\x55\xaa'
with open(out, 'wb') as fp:
    fp.truncate(n * bs)
    fp.seek(0); fp.write(mbr); fp.write(hdr(1, n - 1, 2)); fp.write(ents)
    fp.seek((n - 1 - eblk) * bs); fp.write(ents); fp.write(hdr(n - 1, 1, n - 1 - eblk))
    for nm, t, f, l in parts:
        fp.seek(f * bs); fp.write(f'LAPIS-PART:{nm}\n'.encode())
print(out, n, 'blocks of', bs, [(p[0], p[2], p[3]) for p in parts])
