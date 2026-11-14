#!/usr/bin/env python3
"""usage: patch_kernel_lapis_pthread.py qemu/Image.ipados OUT
(build first: xnu/lapis_pthread/build.sh)
Install lapis_pthread blob into Image.ipados: blob at _copyio_test cave,
_pthread_functions -> lapis_pthread_functions (replaces stub table)."""
import struct, sys, pathlib, hashlib
TEXT_BASE = 0xFFFFFE0007004000; SLIDE = 0x175B0
off = lambda rt: rt + SLIDE - TEXT_BASE
CAVE = 0xFFFFFE0007993E80
COPYIO_TEST = 0xFFFFFE0007993E78; COPYIO_TEST_END = COPYIO_TEST + 16888
COPYIO_PROLOGUE = 0xD104C3FF  # sub sp, sp, #0x130 (stock first insn)
PTHREAD_FUNCTIONS = 0xFFFFFE000724C538
OLD_TABLE = 0xFFFFFE000756A800
src, dst = map(pathlib.Path, sys.argv[1:3])
here = pathlib.Path(__file__).resolve().parent.parent / "xnu" / "lapis_pthread"
blob = (here / "lp.bin").read_bytes()
table = int((here / "lp.table").read_text(), 16)
img = bytearray(src.read_bytes())
assert struct.unpack_from("<I", img, off(COPYIO_TEST))[0] == COPYIO_PROLOGUE, "cave not stock _copyio_test"
assert CAVE + len(blob) <= COPYIO_TEST_END
cur = struct.unpack_from("<Q", img, off(PTHREAD_FUNCTIONS))[0]
assert cur == OLD_TABLE, hex(cur)
img[off(CAVE):off(CAVE) + len(blob)] = blob
# make the dead sysctl handler fail fast: mov w0,#45 ; ret at its entry
struct.pack_into("<II", img, off(COPYIO_TEST), 0x528005A0, 0xD65F03C0)
struct.pack_into("<Q", img, off(PTHREAD_FUNCTIONS), table)
dst.write_bytes(img)
print("wrote", dst, "sha256", hashlib.sha256(img).hexdigest(), "table", hex(table))
