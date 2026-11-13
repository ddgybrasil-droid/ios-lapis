#!/usr/bin/env python3
"""Unit-check the pthread features patch encodings (no fullroot required)."""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import patch_fullroot_pthread_features as p  # noqa: E402

assert p.SITE_OFF == p.SITE_VA - p.TEXT_BASE
assert p.FEATURE_OFF == p.DATA_MAP_FILEOFF + (p.FEATURE_VA - p.DATA_MAP_ADDR)

# movz w0, #0x1e; movk w0, #0x4000, lsl #16; ret; nop
words = struct.unpack("<4I", p.REPL_CODE)
assert words == (0x528003C0, 0x72A80000, 0xD65F03C0, 0xD503201F)
assert struct.unpack("<I", p.REPL_CODE[:4])[0] & 0x1F == 0  # Rd = w0
assert p.REQUIRED_FEATURES == 0x4000001E
# Required bits from libpthread kern_internal.h
assert p.REQUIRED_FEATURES & 0x02  # FINEPRIO
assert p.REQUIRED_FEATURES & 0x04  # BSDTHREADCTL
assert p.REQUIRED_FEATURES & 0x08  # SETSELF
assert p.REQUIRED_FEATURES & 0x10  # QOS_MAINTENANCE
assert p.REQUIRED_FEATURES & 0x40000000  # QOS_DEFAULT

# Expect matches live .31 (first 16 bytes of the gate)
assert len(p.EXPECT_CODE) == 16 and len(p.REPL_CODE) == 16
print("PASS: pthread features patch encodings")
