#!/usr/bin/env python3
"""usage: patch_kernel_lapisfs_rootauth.py IN OUT
lapisfs VNOP_IOCTL (RT 0xfffffe000743ce44) only knows 0x40084a6a and returns
ENOTSUP otherwise. Once the root is not flagged as a ramdisk (rootdev=md0),
bsd_init requires VNOP_IOCTL(rootvnode, FSIOC_KERNEL_ROOTAUTH) == 0 and
panics "rootvp not authenticated after mounting". Teach lapisfs to answer
FSIOC_KERNEL_ROOTAUTH (_IOW('h',51,uint32_t) = 0x80046833) with 0.
Bring-up policy, same class as the AMFI stubs: the lapisfs root is
read-only but not sealed/verified. Code goes in the dead _copyio_test cave
after the lapis_pthread blob."""
import struct, sys, hashlib
TEXT_BASE = 0xFFFFFE0007004000; SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE
BNE = 0xFFFFFE000743CE68      # b.ne <ENOTSUP path> after cmp cmd,0x40084a6a
ENOTSUP_PATH = 0xFFFFFE000743CEC8
RET0 = 0xFFFFFE000743CEB8     # mov w0,#0 ; epilogue
EPILOGUE = 0xFFFFFE000743CEBC
CAVE = 0xFFFFFE0007996000     # inside _copyio_test (ends 0xfffffe0007998070), after lapis_pthread blob
def b_cond(pc, tgt, cond): return 0x54000000 | ((((tgt - pc) >> 2) & 0x7FFFF) << 5) | cond
def b(pc, tgt): return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)
src, dst = sys.argv[1:3]
img = bytearray(open(src, "rb").read())
assert struct.unpack_from("<I", img, off(BNE))[0] == b_cond(BNE, ENOTSUP_PATH, 1), "unexpected b.ne"
assert struct.unpack_from("<I", img, off(ENOTSUP_PATH))[0] == 0x528005A0   # mov w0,#45
assert struct.unpack_from("<I", img, off(0xFFFFFE0007993E78))[0] == 0x528005A0, \
    "apply patch_kernel_lapis_pthread.py first (cave must be the dead _copyio_test)"
# cec8 (`mov w0,#45`, reached only via b.ne with x1 = cmd) becomes `b CAVE`;
# conditional branches cannot reach the cave (+-1 MiB), so branch locally.
seq = [
    0xD28D0660,                     # movz x0, #0x6833
    0xF2B00080,                     # movk x0, #0x8004, lsl #16
    0xEB00003F,                     # cmp  x1, x0
    b_cond(CAVE + 12, CAVE + 20, 1),  # b.ne +8
    b(CAVE + 16, RET0),             # b    RET0 (mov w0,#0; epilogue)
    0x528005A0,                     # mov  w0, #45 (ENOTSUP)
    b(CAVE + 24, EPILOGUE),         # b    EPILOGUE
]
for i, w in enumerate(seq):
    struct.pack_into("<I", img, off(CAVE + 4 * i), w)
struct.pack_into("<I", img, off(ENOTSUP_PATH), b(ENOTSUP_PATH, CAVE))
open(dst, "wb").write(img)
print("sha256", hashlib.sha256(img).hexdigest())
