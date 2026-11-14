#!/usr/bin/env python3
"""Unit test for patch_fullroot_dyld_dataconst_str encodings."""

from __future__ import annotations

import pathlib
import sys
import unittest

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import patch_fullroot_dyld_dataconst_str as patch  # noqa: E402


class DataConstStrPatchTests(unittest.TestCase):
    def test_expect_is_str_x21_x19(self) -> None:
        md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        ins = list(md.disasm(patch.STR_EXPECT, patch.STR_VA))[0]
        self.assertEqual(ins.mnemonic, "str")
        self.assertIn("x21", ins.op_str)
        self.assertIn("[x19]", ins.op_str)

    def test_repl_is_nop(self) -> None:
        md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
        ins = list(md.disasm(patch.STR_REPL, patch.STR_VA))[0]
        self.assertEqual(ins.mnemonic, "nop")

    def test_offsets(self) -> None:
        self.assertEqual(patch.STR_VA, 0x7960)
        self.assertEqual(patch.DYLD_FILE_BASE, 0x89000)


if __name__ == "__main__":
    unittest.main()
