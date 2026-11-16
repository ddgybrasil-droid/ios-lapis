#!/usr/bin/env python3
"""usage: patch_kernel_cs_invalid_always_allow.py IN OUT

cs_invalid_page soft path (forced by make_boot_image) still takes a deny/SIGKILL
branch when a per-process flag is set (UserEventAgent + invalid /usr/lib/dyld
pages → final status 0x25003201, denying page sending SIGKILL).

At RT 0xfffffe0007473764 the soft path does `cbnz w8, hard` then
`mov w0,#0x32; ret` for the allow path. Rewrite the cbnz into an unconditional
branch to the allow epilogue so every soft-path hit allows the page.
"""
import hashlib
import struct
import sys

TEXT_BASE = 0xFFFFFE0007004000
SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE

SITE = 0xFFFFFE0007473764  # cbnz w8, hard
EXPECT = 0x350001A8         # cbnz w8, #+0x34
ALLOW = 0xFFFFFE0007473768  # mov w0, #0x32


def b(pc, tgt):
    return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)


src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())
cur = struct.unpack_from("<I", img, off(SITE))[0]
assert cur == EXPECT or cur == b(SITE, ALLOW), hex(cur)
struct.pack_into("<I", img, off(SITE), b(SITE, ALLOW))
open(dst, "wb").write(img)
print("sha256", hashlib.sha256(img).hexdigest())
