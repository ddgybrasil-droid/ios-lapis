#!/usr/bin/env python3
"""usage: patch_kernel_lapisfs_update.py IN OUT
lapisfs (built-in read-only root fs) vfs_mount re-initialises its private
state unconditionally. A remount of / (MNT_UPDATE, e.g. from mount -P 1 /
a mount_lapisfs helper) must not do that: on MNT_UPDATE return 0 (the fs is
read-only and keeps its existing state). Entry 0xfffffe000743d4e8 (first
member of lapisfs vfsops @0xfffffe000743dc20) branches to a cave that calls
vfs_isupdate(mp); non-update mounts run the original code unchanged."""
import struct, sys, hashlib
TEXT_BASE = 0xFFFFFE0007004000; SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE
ENTRY = 0xFFFFFE000743D4E8; VFSOPS = 0xFFFFFE000743DC20
VFS_ISUPDATE = 0xFFFFFE000761C930
CAVE = 0xFFFFFE0007996120        # after the proc_getcsflags cave (0x...6100-0x...610f)
def b(pc, t, link=False): return (0x94000000 if link else 0x14000000) | (((t - pc) >> 2) & 0x3FFFFFF)
src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())
rd = lambda a: struct.unpack_from("<I", img, off(a))[0]
assert rd(ENTRY) == 0xA9B67BFD, hex(rd(ENTRY))                       # stp x29,x30,[sp,#-0xa0]!
assert struct.unpack_from("<Q", img, off(VFSOPS))[0] == ENTRY
assert rd(0xFFFFFE0007993E78) == 0x528005A0, "need lapis_pthread (copyio_test dead)"  # cave = dead _copyio_test body
seq = [0xa9bd7bfd, 0x910003fd, 0xa90107e0, 0xa9020fe2,
       b(CAVE + 16, VFS_ISUPDATE, True),
       0x2a0003e9, 0xa94107e0, 0xa9420fe2, 0xa8c37bfd, 0x35000069,
       0xa9b67bfd, b(CAVE + 44, ENTRY + 4),
       0x52800000, 0xd65f03c0]
for i, w in enumerate(seq): struct.pack_into("<I", img, off(CAVE + 4*i), w)
struct.pack_into("<I", img, off(ENTRY), b(ENTRY, CAVE))
open(dst, "wb").write(img); print("sha256", hashlib.sha256(img).hexdigest())
