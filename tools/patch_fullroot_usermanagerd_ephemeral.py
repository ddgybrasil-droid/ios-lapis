#!/usr/bin/env python3
"""Patch usermanagerd's j__os_variant_uses_ephemeral_storage stub to return 0.

UMD_INIT skips persona creation when hw.ephemeral_storage=1 (ADT /chosen
ephemeral-storage). That flag must stay set so keybagd also skips MKB_INIT.
Only usermanagerd's import stub is rewritten (mov w0,#0; ret) — not a NOP sled
over unrelated code — so personas can be allocated while keybagd stays safe.

usage: patch_fullroot_usermanagerd_ephemeral.py IMG
"""
from __future__ import annotations

import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions, lapisfs_paths  # noqa: E402

PATH = "/usr/libexec/usermanagerd"
# j__os_variant_uses_ephemeral_storage @ VA 0x1000b1034 (IPSW 23F77 arm64e)
STUB_VA = 0x1000B1034
STUB_FILE = STUB_VA - 0x100000000
# Expected stub: ADRP x17; ADD #0x9d0; LDR; BRAA
EXPECT = (0xF0000131, 0x91274231, 0xF9400230, 0xD71F0A11)
PATCH = (0x2A1F03E0, 0xD65F03C0, 0xD4200020, 0xD4200020)  # mov w0,#0; ret; brk; brk


def extract(img: str, want: str) -> bytes:
    with open(img, "rb") as f:
        root = next(p for p in gpt_partitions(f) if p[0] == "lapis-root")
        f.seek(root[1])
        meta = f.read(32 << 20)
        _, _, paths = lapisfs_paths(meta)
        for p, t, off, sz in paths:
            if p == want and t == 2:
                f.seek(root[1] + off)
                return f.read(sz)
    raise SystemExit(f"missing {want}")


def main() -> None:
    img = sys.argv[1]
    blob = bytearray(extract(img, PATH))
    words = [struct.unpack_from("<I", blob, STUB_FILE + 4 * i)[0] for i in range(4)]
    if tuple(words) == PATCH:
        print("usermanagerd ephemeral stub already patched")
        return
    if tuple(words) != EXPECT:
        raise SystemExit(f"unexpected stub words {[hex(w) for w in words]} (need IPSW 23F77 layout)")
    for i, w in enumerate(PATCH):
        struct.pack_into("<I", blob, STUB_FILE + 4 * i, w)
    tmp = tempfile.NamedTemporaryFile(delete=False)
    tmp.write(blob)
    tmp.close()
    rehash = tmp.name + ".rehash"
    rc = os.system(f"python3 {os.path.dirname(os.path.abspath(__file__))}/macho_cd_rehash.py {tmp.name} {rehash}")
    if rc:
        os.unlink(tmp.name)
        raise SystemExit("cd rehash failed")
    rc = os.system(f"python3 {os.path.dirname(os.path.abspath(__file__))}/lapisfs_replace.py {img} {PATH} {rehash}")
    os.unlink(tmp.name)
    os.unlink(rehash)
    if rc:
        raise SystemExit("replace failed")
    print("patched usermanagerd ephemeral stub -> return 0")


if __name__ == "__main__":
    main()
