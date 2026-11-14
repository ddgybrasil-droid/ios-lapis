#!/usr/bin/env python3
"""Inject lapis-ramfs (xnu/lapis-ramfs/ramfs.bin) into the xnu-12377 Image and route
bsd_init's mountroot call through lapis_ramfs_wrap (registers vfs "tmpfs", then tail-calls
the existing lapisfs register+mountroot wrapper). usage: IN OUT"""
import sys, struct, subprocess, os
BASE = 0xFFFFFE0007004000; FOFF = 0x175B0
def off(va): return va - BASE + FOFF
BLOB_VA, BLOB_END = 0xfffffe000743e100, 0xfffffe00074443d8
CALL_SITE, OLD_TGT = 0xfffffe00078a5a94, 0xfffffe000743d9e8
d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'xnu', 'lapis-ramfs')
blob = open(os.path.join(d, 'ramfs.bin'), 'rb').read()
wrap = int([l.split()[0] for l in subprocess.check_output(['aarch64-linux-gnu-nm', os.path.join(d, 'ramfs.elf')], text=True).splitlines() if l.endswith(' lapis_ramfs_wrap')][0], 16)
img = bytearray(open(sys.argv[1], 'rb').read())
assert BLOB_VA + len(blob) <= BLOB_END
region = img[off(BLOB_VA):off(BLOB_VA) + len(blob)]
assert region == bytes(len(blob)), 'target region not free (patch a kernel without ramfs)'
img[off(BLOB_VA):off(BLOB_VA) + len(blob)] = blob
def bl(src, dst): return 0x94000000 | (((dst - src) // 4) & 0x3ffffff)
cur = struct.unpack_from('<I', img, off(CALL_SITE))[0]
assert cur in (bl(CALL_SITE, OLD_TGT), bl(CALL_SITE, wrap)), hex(cur)
struct.pack_into('<I', img, off(CALL_SITE), bl(CALL_SITE, wrap))
# lapisfs vnodeop table (kernel blob @0x743dcd0): readdir slot err_readdir -> lapisfs_readdir
RD_SLOT, ERR_READDIR = 0xfffffe000743de28, 0xfffffe0007613200
assert struct.unpack_from('<Q', img, off(RD_SLOT - 8))[0] == 0xfffffe0007c87058  # vnop_readdir_desc
rd = int([l.split()[0] for l in subprocess.check_output(['aarch64-linux-gnu-nm', os.path.join(d, 'ramfs.elf')], text=True).splitlines() if l.endswith(' lapisfs_readdir')][0], 16)
assert struct.unpack_from('<Q', img, off(RD_SLOT))[0] in (ERR_READDIR, rd)
struct.pack_into('<Q', img, off(RD_SLOT), rd)
print('lapisfs readdir slot -> %#x' % rd)
sym = lambda n: int([l.split()[0] for l in subprocess.check_output(['aarch64-linux-gnu-nm', os.path.join(d, 'ramfs.elf')], text=True).splitlines() if l.endswith(' ' + n)][0], 16)
# lapisfs symlinks: readlink slot err_readlink -> lapisfs_readlink
RL_SLOT, ERR_READLINK = 0xfffffe000743de38, 0xfffffe0007613240
assert struct.unpack_from('<Q', img, off(RL_SLOT - 8))[0] == 0xfffffe0007c87130  # vnop_readlink_desc
rl = sym('lapisfs_readlink')
assert struct.unpack_from('<Q', img, off(RL_SLOT))[0] in (ERR_READLINK, rl)
struct.pack_into('<Q', img, off(RL_SLOT), rl)
# lapisfs vget vtype: was  ldrb w0,[x19,#4]; cmp w0,#2; cset w0,ne; cmp w20,#0; add w0,w0,#1  (VREG/VDIR only)
# now                      ldrb w0,[x19,#4]; bl lapisfs_vtype; sub w0,w0,#1; cmp w20,#0; add w0,w0,#1  (VREG/VDIR/VLNK)
VT = 0xfffffe000743d268
old = [0x7100081f, 0x1a9f07e0]
new = [bl(VT, sym('lapisfs_vtype')), 0x51000400]
cur = list(struct.unpack_from('<2I', img, off(VT)))
assert cur in (old, new), [hex(x) for x in cur]
struct.pack_into('<2I', img, off(VT), *new)
print('lapisfs readlink slot -> %#x, vget vtype -> bl %#x' % (rl, sym('lapisfs_vtype')))
# lapisfs owners: getattr slot (kernel lapisfs getattr, always uid/gid 0) -> lapisfs_getattr (owner table)
GA_SLOT, OLD_GETATTR = 0xfffffe000743dd48, 0xfffffe000743caf4
assert struct.unpack_from('<Q', img, off(GA_SLOT - 8))[0] == 0xfffffe0007c86870  # vnop_getattr_desc
ga = sym('lapisfs_getattr')
assert struct.unpack_from('<Q', img, off(GA_SLOT))[0] in (OLD_GETATTR, ga)
struct.pack_into('<Q', img, off(GA_SLOT), ga)
print('lapisfs getattr slot -> %#x' % ga)
open(sys.argv[2], 'wb').write(img)
print('ramfs %d bytes @%#x, wrap %#x, call site %#x -> bl wrap' % (len(blob), BLOB_VA, wrap, CALL_SITE))
