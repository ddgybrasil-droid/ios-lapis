#!/usr/bin/env python3
"""Unit-check dyld SEGV skip patches (no fullroot required)."""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import patch_fullroot_dyld_skip_chained as p  # noqa: E402

assert len(p.WALKER_EXPECT) == 12 and len(p.WALKER_REPL) == 12
assert p.WALKER_EXPECT != p.WALKER_REPL
assert struct.unpack_from("<I", p.WALKER_REPL, 0)[0] == 0x52800000
assert struct.unpack_from("<I", p.WALKER_REPL, 4)[0] == 0xD65F03C0
assert struct.unpack_from("<I", p.WALKER_EXPECT, 0)[0] == 0xD503201F
assert p.WALKER_VA == 0x2B564

assert len(p.HEADER_EXPECT) == 8 and len(p.HEADER_REPL) == 8
assert p.HEADER_EXPECT != p.HEADER_REPL
# b.cc → b, same displacement
assert struct.unpack_from("<I", p.HEADER_EXPECT, 0)[0] == 0x7108451F
assert struct.unpack_from("<I", p.HEADER_EXPECT, 4)[0] == 0x54000863
assert struct.unpack_from("<I", p.HEADER_REPL, 4)[0] == 0x14000043
assert p.HEADER_VA == 0x652F0
print("ok")
