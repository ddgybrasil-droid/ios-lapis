#!/usr/bin/env python3
"""Sanity-check the _container_init DSC patch encodings."""

import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import patch_fullroot_container_init as p  # noqa: E402


def test_patch_bytes():
    assert len(p.EXPECT) == 16 and len(p.REPL) == 16
    expect_words = struct.unpack("<4I", p.EXPECT)
    repl_words = struct.unpack("<4I", p.REPL)
    assert expect_words[0] == 0x91005FE0  # add x0, sp, #0x17
    assert expect_words[1] == 0x91005BE1  # add x1, sp, #0x16
    assert (expect_words[2] & 0xFC000000) == 0x94000000  # bl
    assert (expect_words[3] & 0xFF00001F) == 0x35000000  # cbnz w0
    assert repl_words == (0x39005FFF, 0x39005BFF, 0x52800000, 0xD503201F)
    assert p.SITE_OFF == p.SITE_VA - p.SUBCACHE_BASE


if __name__ == "__main__":
    test_patch_bytes()
    print("ok")
