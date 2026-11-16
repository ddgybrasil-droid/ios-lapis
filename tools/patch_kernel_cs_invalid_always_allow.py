#!/usr/bin/env python3
"""usage: patch_kernel_cs_invalid_always_allow.py IN OUT

Force every cs_invalid_page deny/SIGKILL path onto the soft allow epilogue.

boot80: late cbnz@0x…3764 insufficient (tbnz@0x…3710 escaped).
boot81: soft-entry@0x…3700→allow still insufficient — earlier
  cbnz w0 @ RT 0xfffffe00074731ac jumps straight to deny logging @ 0x…379c
  (CS_KILL + tainted dyld page path for UserEventAgent).

Patches:
  1) soft entry 0x…3700 → b allow epilogue 0x…3768
  2) early deny cbnz 0x…31ac → b fallthrough (never take deny)
  3) deny-block entry 0x…379c → b allow epilogue (belt-and-suspenders)
  4) late site 0x…3764 → b allow (idempotent)
"""
import hashlib
import struct
import sys

TEXT_BASE = 0xFFFFFE0007004000
SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE

SOFT = 0xFFFFFE0007473700
ALLOW = 0xFFFFFE0007473768  # mov w0,#0x32; epilogue
EARLY_CBNZ = 0xFFFFFE00074731AC  # cbnz w0, deny
EARLY_EXPECT = 0x35002F80
EARLY_CONT = 0xFFFFFE00074731B0  # fallthrough after cbnz
DENY_BLK = 0xFFFFFE000747379C
LATE = 0xFFFFFE0007473764


def b(pc, tgt):
    return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)


src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())

struct.pack_into("<I", img, off(SOFT), b(SOFT, ALLOW))

cur = struct.unpack_from("<I", img, off(EARLY_CBNZ))[0]
assert cur in (EARLY_EXPECT, b(EARLY_CBNZ, EARLY_CONT), b(EARLY_CBNZ, ALLOW)), hex(cur)
struct.pack_into("<I", img, off(EARLY_CBNZ), b(EARLY_CBNZ, EARLY_CONT))

struct.pack_into("<I", img, off(DENY_BLK), b(DENY_BLK, ALLOW))

cur = struct.unpack_from("<I", img, off(LATE))[0]
if cur in (0x350001A8, b(LATE, ALLOW)):
    struct.pack_into("<I", img, off(LATE), b(LATE, ALLOW))

open(dst, "wb").write(img)
print("sha256", hashlib.sha256(img).hexdigest())
