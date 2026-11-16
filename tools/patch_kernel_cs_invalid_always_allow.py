#!/usr/bin/env python3
"""usage: patch_kernel_cs_invalid_always_allow.py IN OUT

cs_invalid_page soft path (forced by make_boot_image) still reaches a deny/SIGKILL
branch for UserEventAgent + invalid /usr/lib/dyld pages (final status 0x25003201).

boot80: patching only the late `cbnz` at 0x…3764 was insufficient — an earlier
`tbnz w8,#1` at 0x…3710 jumps into the hard/deny block at 0x…37d8.

Fix: at soft-path entry RT 0xfffffe0007473700, unconditional branch to the
allow epilogue at 0xfffffe0007473768 (`mov w0,#0x32` + stack restore + ret).
"""
import hashlib
import struct
import sys

TEXT_BASE = 0xFFFFFE0007004000
SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE

SOFT = 0xFFFFFE0007473700  # soft-path entry (make_boot_image target)
ALLOW = 0xFFFFFE0007473768  # mov w0,#0x32; epilogue
# Prior boot80 attempt (kept idempotent if present):
OLD_CBNZ = 0xFFFFFE0007473764


def b(pc, tgt):
    return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)


src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())

# Soft entry: always jump to allow epilogue.
struct.pack_into("<I", img, off(SOFT), b(SOFT, ALLOW))

# Keep the late site on the allow path too (idempotent vs boot80 partial patch).
cur = struct.unpack_from("<I", img, off(OLD_CBNZ))[0]
if cur in (0x350001A8, b(OLD_CBNZ, ALLOW)):
    struct.pack_into("<I", img, off(OLD_CBNZ), b(OLD_CBNZ, ALLOW))

open(dst, "wb").write(img)
print("sha256", hashlib.sha256(img).hexdigest())
