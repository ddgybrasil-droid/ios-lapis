#!/usr/bin/env python3
"""Unit test for patch_fullroot_dyld_alloc_pool trampoline."""

from __future__ import annotations

import pathlib
import struct
import sys
import unittest

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import patch_fullroot_dyld_alloc_pool as patch  # noqa: E402


class AllocPoolPatchTests(unittest.TestCase):
    def test_hook_branches_to_cave(self) -> None:
        w = struct.unpack_from("<I", patch.HOOK_REPL, 0)[0]
        self.assertEqual(w & 0xFC000000, 0x14000000)
        imm = w & 0x3FFFFFF
        if imm & 0x2000000:
            imm -= 0x4000000
        self.assertEqual(patch.HOOK_VA + imm * 4, patch.CAVE_VA)

    def test_cave_redirects_pool_and_calls_alloc(self) -> None:
        md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        ins = list(md.disasm(patch.CAVE_BLOB, patch.CAVE_VA))
        texts = [f"{i.mnemonic} {i.op_str}" for i in ins]
        self.assertEqual(texts[0], "mov x19, x0")
        self.assertTrue(any("x5" in t and "#0xb9000" in t for t in texts[:3]))
        self.assertTrue(any(t.startswith("stp x8, x9, [x5]") for t in texts))
        self.assertTrue(any(t.startswith("bl ") and "0x86a8" in t for t in texts))
        self.assertTrue(texts[-1].startswith("b ") and "0x7bf6c" in texts[-1])

    def test_pool_past_dirty_refs(self) -> None:
        self.assertGreaterEqual(patch.POOL_VA, 0xB8C00)
        self.assertEqual(patch.POOL_VA + patch.POOL_SIZE, 0xBC000)


if __name__ == "__main__":
    unittest.main()
