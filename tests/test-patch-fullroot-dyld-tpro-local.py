#!/usr/bin/env python3
"""Unit-check dyld TPRO local-store patches (no fullroot required)."""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import patch_fullroot_dyld_tpro_local as p  # noqa: E402

assert len(p.CAVE_EXPECT) == 16 and len(p.CAVE_REPL) == 16
assert p.CAVE_EXPECT != p.CAVE_REPL
assert p.CAVE_VA == 0x19E4
# adrp x0 / add / str x20,[x0] / ret
assert struct.unpack_from("<I", p.CAVE_REPL, 0)[0] == 0xF0000580
assert struct.unpack_from("<I", p.CAVE_REPL, 4)[0] == 0x91000C00
assert struct.unpack_from("<I", p.CAVE_REPL, 8)[0] == 0xF9000014
assert struct.unpack_from("<I", p.CAVE_REPL, 12)[0] == 0xD65F03C0

assert p.STORE_EXPECT == bytes.fromhex("f9000014")
assert len(p.STORE_REPL) == 4
store_insn = struct.unpack_from("<I", p.STORE_REPL, 0)[0]
assert (store_insn >> 26) == 0x25  # bl
imm26 = store_insn & 0x3FFFFFF
if imm26 & (1 << 25):
    imm26 -= 1 << 26
assert p.STORE_VA + (imm26 << 2) == p.CAVE_VA

assert p.HELPER_EXPECT == bytes.fromhex("aa0003f8")
helper_insn = struct.unpack_from("<I", p.HELPER_REPL, 0)[0]
assert (helper_insn >> 26) == 0x5  # b
imm26 = helper_insn & 0x3FFFFFF
if imm26 & (1 << 25):
    imm26 -= 1 << 26
assert p.HELPER_VA + (imm26 << 2) == p.HELPER_TARGET
assert p.HELPER_TARGET == 0x4F3C8

print("ok")
