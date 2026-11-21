#!/usr/bin/env python3
"""usage: patch_kernel_platform_binary.py IN OUT

Bring-up: make every process look like a platform binary to csops/launchd.

Without AMFI, mac_vnode_check_signature leaves cs_flags as CS_VALID|CS_ADHOC
(no CS_SIGNED, no CS_PLATFORM_BINARY). ORing those bits inside ubc_cs_blob_add
trips the platform-CD validation path and does not reliably reach p_csflags.

Instead, OR CS_VALID|CS_SIGNED|CS_PLATFORM_BINARY (0x24000001) into the value
returned by proc_getcsflags (offset 0x24 of proc_ro). That is what
csops(CS_OPS_STATUS) and csproc_get_platform_binary gate on.

CS_VALID must be forced on the *return* path: cs_invalid_page's soft allow
path clears VALID in the stored flags ("allowing (remove VALID)"), and
SpringBoard's SBStaticInitializer requires
(status & 0x0fffffff & 0xfc000001) == 0x04000001 via SecTaskGetCodeSignStatus.
Cave in dead _copyio_test (RT 0x...6100..0x...6113; lapisfs_update at 0x...6120)."""
import struct, sys, hashlib
TEXT_BASE = 0xFFFFFE0007004000; SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE
# proc_getcsflags: ldr w0,[x19,#0x24] ; ldp ... ; ldp ... ; ret
SITE = 0xFFFFFE00078E8BCC     # ldr w0, [x19, #0x24]
BACK = SITE + 4               # ldp x29,x30,[sp,#0x10]
CAVE = 0xFFFFFE0007996100
LDR  = 0xB9402660             # ldr w0, [x19, #0x24]
def b(pc, tgt): return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)
src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())
# Idempotent: accept either the original ldr or an already-installed branch to CAVE.
site_insn = struct.unpack_from("<I", img, off(SITE))[0]
assert site_insn == LDR or site_insn == b(SITE, CAVE), hex(site_insn)
assert struct.unpack_from("<I", img, off(0xFFFFFE0007993E78))[0] == 0x528005A0, "need lapis_pthread first"
# also require rootauth cave present (optional soft check)
seq = [
    LDR,                      # ldr w0, [x19, #0x24]
    0x32030000,               # orr w0, w0, #0x20000000  (CS_SIGNED)
    0x32060000,               # orr w0, w0, #0x04000000  (CS_PLATFORM_BINARY)
    0x32000000,               # orr w0, w0, #0x00000001  (CS_VALID)
    b(CAVE + 16, BACK),
]
for i, w in enumerate(seq):
    struct.pack_into("<I", img, off(CAVE + 4 * i), w)
struct.pack_into("<I", img, off(SITE), b(SITE, CAVE))
open(dst, "wb").write(img)
print("sha256", hashlib.sha256(img).hexdigest())
