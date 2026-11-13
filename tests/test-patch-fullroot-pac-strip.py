#!/usr/bin/env python3
"""Unit-check PAC strip rewrites (no fullroot required)."""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import patch_fullroot_pac_strip as p  # noqa: E402

NOP, RET = p.NOP, p.RET

# blraa x8, x21 (binutils) → blr x8
assert p.rewrite_word(0xD73F0915) == 0xD63F0100
# retab → ret
assert p.rewrite_word(0xD65F0FFF) == RET
assert p.rewrite_word(0xD65F0BFE) == RET
# pacibsp → nop; autiasp → xpaclri
assert p.rewrite_word(0xD503237F) == NOP
assert p.rewrite_word(0xD50323BF) == p.XPACLRI
# autda x16, x17 → xpacd x16
assert p.rewrite_word(0xDAC11A30) == (p.XPACD_BASE | 16)
# autia x0, x1 → xpaci x0
assert p.rewrite_word(0xDAC11020) == (p.XPACI_BASE | 0)
# pacia → nop
assert p.rewrite_word(0xDAC10020) == NOP
# brk #0xc471 → nop; brk #1 left alone unless NOP_BRK1
assert p.rewrite_word(0xD4388E20) == NOP
assert p.rewrite_word(0xD4200020) is None
p.NOP_BRK1 = True
assert p.rewrite_word(0xD4200020) == NOP
p.NOP_BRK1 = False
# ordinary blr / ret / nop untouched
assert p.rewrite_word(0xD63F0100) is None
assert p.rewrite_word(RET) is None
assert p.rewrite_word(NOP) is None
# intentional libpthread assert brk #0xb001 left alone
assert p.rewrite_word(0xD4360020) is None

blob = bytearray(
    struct.pack(
        "<6I",
        0xD73F0915,
        0xD65F0FFF,
        0xD503237F,
        0xDAC11A30,
        0xD4388E20,
        0xD4200020,
    )
)
counts = p.strip_blob(blob)
words = struct.unpack("<6I", blob)
assert words == (
    0xD63F0100,
    RET,
    NOP,
    p.XPACD_BASE | 16,
    NOP,
    0xD4200020,
)
assert counts["blr_auth"] == 1 and counts["ret_auth"] == 1
assert counts["hint"] == 1 and counts["pac_aut"] == 1
assert counts["brk_pac"] == 1 and "brk1" not in counts

# movk x16, #imm, lsl #48 ; blraa x8, x16 → xpaci x8 ; blr x8
movk_x16_lsl48 = 0xF2E00010 | (0x4F8 << 5)  # movk x16, #0x4f8, lsl #48
seq = bytearray(struct.pack("<2I", movk_x16_lsl48, 0xD73F0915))  # blraa x8,x21 — wrong mod
# Use blraa x8, x16: Rm=8 addr, Rn=16 mod → 0xD73F0800|(8<<5)|16 = 0xD73F0910
seq = bytearray(struct.pack("<2I", movk_x16_lsl48, 0xD73F0910))
counts = p.strip_blob(seq)
assert struct.unpack("<2I", seq) == (p.XPACI_BASE | 8, 0xD63F0100)
assert counts.get("xpaci_before_br") == 1

# Minimal Mach-O with __TEXT: fileoff=0x1000, filesize=0x100, initprot=0
# (dyld on-disk often has initprot=0 on TEXT).
hdr = bytearray(0x1100)
struct.pack_into("<IIIIIII", hdr, 0, 0xFEEDFACF, 0x100000C, 0, 2, 1, 72, 0)
# LC_SEGMENT_64
struct.pack_into("<II", hdr, 32, 0x19, 72)
hdr[40:56] = b"__TEXT" + b"\0" * 10
struct.pack_into("<QQQQ", hdr, 56, 0, 0x100, 0x1000, 0x100)  # vm/file
struct.pack_into("<II", hdr, 88, 5, 0)  # maxprot=r-x, initprot=0
slices = p._macho_text_slices(bytes(hdr))
assert slices == [(0x1000, 0x100)], slices

print("PASS: pac strip rewrites")
