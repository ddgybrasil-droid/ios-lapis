#!/usr/bin/env python3
"""Force CFPrefsDaemon shmem to use O_CREAT|O_RDWR.

-[CFPrefsDaemon shmem] at 0x1835f32d4 does:
  ldrb w8, [flag]
  tbz  w8, #0, extension_path   ; flag clear → sandbox_extension
  movz w1, #0x202               ; O_CREAT|O_RDWR
  b    shm_open

Under Lapis the extension call fails, so daemonv1 shm is never created
(clients then ENOENT on apple.cfprefs.*.daemonv1). NOP the tbz so the
daemon always takes the O_CREAT path (this selector only runs in cfprefsd).

Unslid site in dyld_shared_cache_arm64e.01 (map va=0x180094000).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SUBCACHE_BASE = 0x180094000
TBZ_SITE = 0x1835F32D4
TBZ_EXPECT = 0x36000068  # tbz w8, #0, +0xc → 0x1835f32e0
NOP = 0xD503201F


def patch_bytes(buf: bytearray, *, write: bool) -> bool:
    fo = TBZ_SITE - SUBCACHE_BASE
    cur = struct.unpack_from("<I", buf, fo)[0]
    if cur == NOP:
        print(f"{TBZ_SITE:#x}: already NOP")
        return False
    if cur != TBZ_EXPECT:
        # Accept any tbz w8,#0 to the extension path (+3 insns)
        if (cur & 0xFF00001F) == 0x36000008:
            print(f"{TBZ_SITE:#x}: tbz variant {cur:#x} -> NOP")
        else:
            raise SystemExit(f"{TBZ_SITE:#x}: unexpected insn {cur:#x}")
    else:
        print(f"{TBZ_SITE:#x}: tbz -> NOP")
    if write:
        struct.pack_into("<I", buf, fo, NOP)
    return True


def patch_image(img: Path, *, write: bool) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from lapisfs_names import gpt_partitions, lapisfs_paths

    with img.open("r+b" if write else "rb") as f:
        parts = gpt_partitions(f)
        root_off = next(p[1] for p in parts if p[0] == "lapis-root")
        f.seek(root_off)
        directory = f.read(8 << 20)
        _, _, paths = lapisfs_paths(directory)
        n = 0
        seen = set()
        for path, typ, off, size in paths:
            if typ != 2:
                continue
            pl = path if isinstance(path, str) else path.decode()
            if not pl.endswith("dyld_shared_cache_arm64e.01"):
                continue
            if off in seen:
                continue
            seen.add(off)
            base_abs = root_off + off
            f.seek(base_abs)
            buf = bytearray(f.read(size))
            print(f"{pl}:")
            if patch_bytes(buf, write=write) and write:
                f.seek(base_abs)
                f.write(buf)
            n += 1
        if not n:
            raise SystemExit("no .01 extent")
        print(f"{'wrote' if write else 'would write'} {n} extent(s)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    patch_image(args.image, write=args.write)


if __name__ == "__main__":
    main()
