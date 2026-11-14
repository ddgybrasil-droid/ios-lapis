#!/usr/bin/env python3
"""Replace the lapis-ramfs blob in an already-patched Image (pth14+ / pth18).

patch_kernel_ramfs.py requires a free (zero) blob region. This tool zeros the
existing blob region, then re-applies the ramfs vnodeop/wrap patches and the
amfi boot/IOKit restores against the new build (xnu/lapis-ramfs/ramfs.bin).

usage: patch_kernel_ramfs_replace.py IN OUT
"""
import os, struct, subprocess, sys

BASE = 0xFFFFFE0007004000
FOFF = 0x175B0
BLOB_VA, BLOB_END = 0xfffffe000743e100, 0xfffffe00074443d8
CALL_SITE, OLD_TGT = 0xfffffe00078a5a94, 0xfffffe000743d9e8


def off(va):
    return va - BASE + FOFF


def bl(src, dst):
    return 0x94000000 | (((dst - src) // 4) & 0x3ffffff)


d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'xnu', 'lapis-ramfs')
blob = open(os.path.join(d, 'ramfs.bin'), 'rb').read()
assert BLOB_VA + len(blob) <= BLOB_END, 'blob too large'
nm = subprocess.check_output(['aarch64-linux-gnu-nm', os.path.join(d, 'ramfs.elf')], text=True).splitlines()


def sym(n):
    for l in nm:
        if l.endswith(' ' + n):
            return int(l.split()[0], 16)
    raise SystemExit('missing symbol %s' % n)


wrap = sym('lapis_ramfs_wrap')
img = bytearray(open(sys.argv[1], 'rb').read())

# Drop the previous blob (any size up to the reserved region).
img[off(BLOB_VA):off(BLOB_END)] = bytes(BLOB_END - BLOB_VA)
img[off(BLOB_VA):off(BLOB_VA) + len(blob)] = blob

# bsd_init mountroot call -> lapis_ramfs_wrap
cur = struct.unpack_from('<I', img, off(CALL_SITE))[0]
assert cur in (bl(CALL_SITE, OLD_TGT), bl(CALL_SITE, wrap)) or (cur & 0xFC000000) == 0x94000000, hex(cur)
struct.pack_into('<I', img, off(CALL_SITE), bl(CALL_SITE, wrap))

# lapisfs vnodeop slots
RD_SLOT, ERR_READDIR = 0xfffffe000743de28, 0xfffffe0007613200
assert struct.unpack_from('<Q', img, off(RD_SLOT - 8))[0] == 0xfffffe0007c87058
rd = sym('lapisfs_readdir')
struct.pack_into('<Q', img, off(RD_SLOT), rd)

RL_SLOT, ERR_READLINK = 0xfffffe000743de38, 0xfffffe0007613240
assert struct.unpack_from('<Q', img, off(RL_SLOT - 8))[0] == 0xfffffe0007c87130
rl = sym('lapisfs_readlink')
struct.pack_into('<Q', img, off(RL_SLOT), rl)

VT = 0xfffffe000743d268
struct.pack_into('<2I', img, off(VT), bl(VT, sym('lapisfs_vtype')), 0x51000400)

GA_SLOT = 0xfffffe000743dd48
assert struct.unpack_from('<Q', img, off(GA_SLOT - 8))[0] == 0xfffffe0007c86870
ga = sym('lapisfs_getattr')
struct.pack_into('<Q', img, off(GA_SLOT), ga)

# amfi: same restores as patch_kernel_amfi.py
SITE, LSTC = 0xfffffe0007ba6e10, 0xfffffe000791a1c4
boot = sym('lapis_amfi_boot')
struct.pack_into('<I', img, off(SITE), bl(SITE, boot))
NOP, MOV1, BLR8 = 0xd503201f, 0x52800020, 0xd63f0100
LDR = lambda o: 0xf9400108 | ((o // 8) << 10)
QB, QS, CP = 0x1c0, 0x1d0, 0x1e0
SITES = [
    (0xfffffe0007b49878, 0xfffffe0007b49884, QS),
    (0xfffffe0007b498f8, 0xfffffe0007b49904, QS),
    (0xfffffe0007b49968, 0xfffffe0007b49970, QB),
    (0xfffffe0007b499d0, 0xfffffe0007b499d8, QB),
    (0xfffffe0007b49a70, 0xfffffe0007b49a7c, CP),
    (0xfffffe0007b49b80, 0xfffffe0007b49b8c, CP),
    (0xfffffe0007b49cc8, 0xfffffe0007b49cd4, QB),
    (0xfffffe0007b49ce4, 0xfffffe0007b49cf4, CP),
]
for a, b, o in SITES:
    ca, cb = struct.unpack_from('<I', img, off(a))[0], struct.unpack_from('<I', img, off(b))[0]
    assert (ca, cb) in ((NOP, MOV1), (LDR(o), BLR8)), (hex(a), hex(ca), hex(cb))
    struct.pack_into('<I', img, off(a), LDR(o))
    struct.pack_into('<I', img, off(b), BLR8)

open(sys.argv[2], 'wb').write(img)
print('replaced ramfs %d bytes @%#x wrap %#x sandbox %#x amfi_boot %#x' % (
    len(blob), BLOB_VA, wrap, sym('lapis_sandbox_register'), boot))
