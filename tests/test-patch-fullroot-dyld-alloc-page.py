#!/usr/bin/env python3
"""Unit test for patch_fullroot_dyld_alloc_page encodings."""

from __future__ import annotations

import pathlib
import struct
import sys
import unittest

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import patch_fullroot_dyld_alloc_page as patch  # noqa: E402


class AllocPagePatchTests(unittest.TestCase):
    def test_branch_lands_on_epilogue(self) -> None:
        w = struct.unpack_from("<I", patch.STP_REPL, 0)[0]
        self.assertEqual(w & 0xFC000000, 0x14000000)
        imm = w & 0x3FFFFFF
        if imm & 0x2000000:
            imm -= 0x4000000
        self.assertEqual(patch.STP_VA + imm * 4, patch.EPILOGUE_VA)

    def test_expect_is_stp_x8_x9_x5(self) -> None:
        md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        ins = list(md.disasm(patch.STP_EXPECT, patch.STP_VA))[0]
        self.assertEqual(ins.mnemonic, "stp")
        self.assertIn("x8", ins.op_str)
        self.assertIn("x9", ins.op_str)
        self.assertIn("[x5]", ins.op_str)

    def test_targets_standalone_dyld(self) -> None:
        self.assertEqual(patch.MACHO_SUFFIX, "/usr/lib/dyld")
        self.assertEqual(patch.STP_VA, 0x7BF44)


if __name__ == "__main__":
    unittest.main()
