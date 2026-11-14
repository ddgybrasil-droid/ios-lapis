#!/usr/bin/env python3
"""Replace the lapis-ramfs blob in an already-patched Image (pth14+ / pth18).

patch_kernel_ramfs.py requires a free (zero) blob region. This tool zeros the
existing blob region, then re-applies the ramfs vnodeop/wrap patches and the
amfi boot/IOKit restores against the new build (xnu/lapis-ramfs/ramfs.bin).

Critical: ramfs.bin and ramfs.elf MUST be from the same build.sh run. An elf/bin
mismatch (e.g. rebuild produces a new elf while the committed bin is used) makes
kernel_bootstrap_thread's `bl lapis_amfi_boot` land mid-function; the next BL
inside that mistarget then jumps into lapis_ramfs_wrap and panics (boot56:
pc in wrap, lr = mistaken_amfi_boot+8, far=NULL+0xaf8).

This tool:
  - checks elf/bin consistency (symbols fall inside the placed blob);
  - rewrites lapis_amfi_boot's two external BLs to load_static_trust_cache /
    amfi_interface_register (same absolute VAs as --defsym / ksyms);
  - asserts kernel_bootstrap_thread's BL and those in-blob BLs match ksyms.

usage: patch_kernel_ramfs_replace.py IN OUT
"""
import hashlib
import os
import struct
import subprocess
import sys

BASE = 0xFFFFFE0007004000
FOFF = 0x175B0
BLOB_VA, BLOB_END = 0xfffffe000743e100, 0xfffffe00074443d8
CALL_SITE, OLD_TGT = 0xfffffe00078a5a94, 0xfffffe000743d9e8

# Absolute kernel targets (xnu-12377.121.6 Image / ksyms).
LSTC = 0xfffffe000791a1c4          # load_static_trust_cache
AIR = 0xfffffe0007a91d50           # amfi_interface_register
SITE = 0xfffffe0007ba6e10          # kernel_bootstrap_thread: was bl LSTC

# stp x29,x30,[sp,#-0x10]! ; mov x29,sp  — lapis_amfi_boot prologue
AMFI_BOOT_PROLOGUE = (0xa9bf7bfd, 0x910003fd)


def off(va):
    return va - BASE + FOFF


def bl(src, dst):
    return 0x94000000 | (((dst - src) // 4) & 0x3ffffff)


def decode_bl(insn, pc):
    if (insn & 0xFC000000) != 0x94000000:
        return None
    imm = insn & 0x3FFFFFF
    if imm & 0x2000000:
        imm -= 0x4000000
    return pc + imm * 4


def rd32(img, va):
    return struct.unpack_from('<I', img, off(va))[0]


def wr32(img, va, w):
    struct.pack_into('<I', img, off(va), w)


d = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'xnu', 'lapis-ramfs')
bin_path = os.path.join(d, 'ramfs.bin')
elf_path = os.path.join(d, 'ramfs.elf')
blob = open(bin_path, 'rb').read()
assert BLOB_VA + len(blob) <= BLOB_END, 'blob too large (%d)' % len(blob)

nm = subprocess.check_output(['aarch64-linux-gnu-nm', elf_path], text=True).splitlines()


def sym(n):
    for l in nm:
        if l.endswith(' ' + n):
            return int(l.split()[0], 16)
    raise SystemExit('missing symbol %s' % n)


wrap = sym('lapis_ramfs_wrap')
boot = sym('lapis_amfi_boot')
sandbox = sym('lapis_sandbox_register')

# elf/bin consistency: every text symbol we care about must sit inside the blob.
for name, va in (('lapis_ramfs_wrap', wrap), ('lapis_amfi_boot', boot),
                 ('lapis_sandbox_register', sandbox)):
    if not (BLOB_VA <= va < BLOB_VA + len(blob)):
        raise SystemExit(
            'elf/bin mismatch: %s=%#x outside placed blob [%#x,%#x) size=%d — '
            're-run xnu/lapis-ramfs/build.sh so ramfs.bin and ramfs.elf match'
            % (name, va, BLOB_VA, BLOB_VA + len(blob), len(blob)))

img = bytearray(open(sys.argv[1], 'rb').read())

# Drop the previous blob (any size up to the reserved region).
img[off(BLOB_VA):off(BLOB_END)] = bytes(BLOB_END - BLOB_VA)
img[off(BLOB_VA):off(BLOB_VA) + len(blob)] = blob

# bsd_init mountroot call -> lapis_ramfs_wrap
cur = rd32(img, CALL_SITE)
assert cur in (bl(CALL_SITE, OLD_TGT), bl(CALL_SITE, wrap)) or (cur & 0xFC000000) == 0x94000000, hex(cur)
wr32(img, CALL_SITE, bl(CALL_SITE, wrap))

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

# --- amfi_boot: verify prologue, force external BLs to ksyms, patch SITE ---
prolog = (rd32(img, boot), rd32(img, boot + 4))
if prolog != AMFI_BOOT_PROLOGUE:
    raise SystemExit(
        'lapis_amfi_boot %#x prologue %s != %s — elf symbol does not match '
        'bytes in ramfs.bin (elf/bin mismatch)'
        % (boot, [hex(x) for x in prolog], [hex(x) for x in AMFI_BOOT_PROLOGUE]))

# bl load_static_trust_cache @ boot+8; bl amfi_interface_register @ boot+0x14
# (layout of lapis_amfi_boot with -Os: stp; mov; bl LSTC; adrp; add; bl AIR; ...)
bl_lstc_pc = boot + 8
bl_air_pc = boot + 0x14
old_lstc = decode_bl(rd32(img, bl_lstc_pc), bl_lstc_pc)
old_air = decode_bl(rd32(img, bl_air_pc), bl_air_pc)
wr32(img, bl_lstc_pc, bl(bl_lstc_pc, LSTC))
wr32(img, bl_air_pc, bl(bl_air_pc, AIR))
assert decode_bl(rd32(img, bl_lstc_pc), bl_lstc_pc) == LSTC
assert decode_bl(rd32(img, bl_air_pc), bl_air_pc) == AIR
if old_lstc not in (None, LSTC) or old_air not in (None, AIR):
    print('rewrote amfi_boot BLs: LSTC %#x -> %#x; AIR %#x -> %#x' % (
        old_lstc or 0, LSTC, old_air or 0, AIR))

wr32(img, SITE, bl(SITE, boot))
assert decode_bl(rd32(img, SITE), SITE) == boot, (
    'kernel_bootstrap_thread %#x bl target %#x != lapis_amfi_boot %#x'
    % (SITE, decode_bl(rd32(img, SITE), SITE), boot))

# IOKit entitlement entry points: same restores as patch_kernel_amfi.py
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
    ca, cb = rd32(img, a), rd32(img, b)
    assert (ca, cb) in ((NOP, MOV1), (LDR(o), BLR8)), (hex(a), hex(ca), hex(cb))
    wr32(img, a, LDR(o))
    wr32(img, b, BLR8)

open(sys.argv[2], 'wb').write(img)
blob_sha = hashlib.sha256(blob).hexdigest()
print('replaced ramfs %d bytes sha256=%s @%#x' % (len(blob), blob_sha, BLOB_VA))
print('  wrap %#x sandbox %#x amfi_boot %#x' % (wrap, sandbox, boot))
print('  SITE %#x -> amfi_boot; amfi_boot+8 -> LSTC %#x; amfi_boot+0x14 -> AIR %#x' % (
    SITE, LSTC, AIR))
