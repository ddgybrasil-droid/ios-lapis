#!/usr/bin/env python3
"""NOP iomfb_match_callback's Act_FB==Ref_FB gate in DSC subcache .44.

RE (IOMobileFramebuffer @0x2288058c0): after a matched FB increments Act_FB,
fulfill of pthread_dependency only runs when Act_FB == Ref_FB. If
AppleParavirtGPU.DisplayPortCount fails to deserialize as a 32-bit CFNumber,
Ref_FB stays 0 while Act_FB becomes 1 → wait never fulfills →
IOMobileFramebufferGetMainDisplay never reaches IOServiceOpen (boot112–115).

NOP the `b.ne` at unslid 0x2288058c8 so any successful Act_FB increment
fulfills. Single-display bring-up only; multi-display would fulfill early.

Unslid: 0x2288058c8 in dyld_shared_cache_arm64e.44
  (subcache map va=0x225874000 → fileoff 0x2f918c8)

Usage:
  python3 tools/patch_dsc_iomfb_fulfill.py /path/to/dyld_shared_cache_arm64e.44 [--write]
  python3 tools/patch_dsc_iomfb_fulfill.py --image /tmp/fullroot-mnt2.img [--write]
Then: python3 tools/dsc_cd_rehash.py IMG --write
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

INSN_VA = 0x2288058C8
SUBCACHE_BASE = 0x225874000
FILE_OFF = INSN_VA - SUBCACHE_BASE  # 0x2f918c8
EXPECT_BNE = 0x54000241  # b.ne #+0x48
NOP = 0xD503201F


def patch_bytes(buf: bytearray, *, write: bool) -> bool:
    cur = struct.unpack_from("<I", buf, FILE_OFF)[0]
    if cur == NOP:
        print(f"{INSN_VA:#x}: already NOP")
        return False
    if cur != EXPECT_BNE:
        raise SystemExit(
            f"{INSN_VA:#x}: unexpected insn {cur:#x} (want bne {EXPECT_BNE:#x})"
        )
    print(f"{INSN_VA:#x}: bne -> NOP (fileoff {FILE_OFF:#x})")
    if write:
        struct.pack_into("<I", buf, FILE_OFF, NOP)
    return True


def patch_file(path: Path, *, write: bool) -> None:
    data = bytearray(path.read_bytes())
    changed = patch_bytes(data, write=write)
    if write and changed:
        path.write_bytes(data)
        print(f"wrote {path}")
    elif not write:
        print("dry-run (pass --write)")


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
            if not pl.endswith("dyld_shared_cache_arm64e.44"):
                continue
            if off in seen:
                continue
            seen.add(off)
            abs_off = root_off + off + FILE_OFF
            f.seek(abs_off)
            word = f.read(4)
            cur = struct.unpack("<I", word)[0]
            if cur == NOP:
                print(f"{pl}: already NOP")
            elif cur != EXPECT_BNE:
                raise SystemExit(f"{pl}: insn {cur:#x} want {EXPECT_BNE:#x}")
            else:
                print(f"{pl}: patch @ {abs_off:#x}")
                if write:
                    f.seek(abs_off)
                    f.write(struct.pack("<I", NOP))
            n += 1
        if not n:
            raise SystemExit("no .44 extent")
        print(f"{'wrote' if write else 'would write'} {n} extent(s)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("subcache44", nargs="?", type=Path)
    ap.add_argument("--image", type=Path)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    if args.image:
        patch_image(args.image, write=args.write)
    elif args.subcache44:
        patch_file(args.subcache44, write=args.write)
    else:
        ap.error("pass subcache44 or --image")


if __name__ == "__main__":
    main()
