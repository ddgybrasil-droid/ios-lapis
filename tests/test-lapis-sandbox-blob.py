#!/usr/bin/env python3
"""Sanity checks for the lapis_sandbox bring-up MAC policy blob."""
import os, subprocess, unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
RAMFS = os.path.join(ROOT, 'xnu', 'lapis-ramfs')


class LapisSandboxBlobTests(unittest.TestCase):
    def test_elf_exports_sandbox_register(self):
        elf = os.path.join(RAMFS, 'ramfs.elf')
        self.assertTrue(os.path.exists(elf), 'run xnu/lapis-ramfs/build.sh first')
        nm = subprocess.check_output(['aarch64-linux-gnu-nm', elf], text=True)
        self.assertIn(' lapis_sandbox_register\n', nm.replace('\r', ''))
        self.assertIn(' lapis_ramfs_wrap\n', nm.replace('\r', ''))

    def test_bin_contains_policy_name(self):
        blob = open(os.path.join(RAMFS, 'ramfs.bin'), 'rb').read()
        self.assertIn(b'Sandbox', blob)
        self.assertIn(b'Lapis bring-up sandbox', blob)
        self.assertLessEqual(len(blob), 0x74443d8 - 0x743e100)

    def test_build_sh_links_sandbox_obj(self):
        sh = open(os.path.join(RAMFS, 'build.sh')).read()
        self.assertIn('lapis_sandbox.o', sh)
        # Must link $OBJS (includes sandbox), not a hard-coded list that drops it.
        self.assertRegex(sh, r'ld .*\$OBJS')


if __name__ == '__main__':
    unittest.main()
