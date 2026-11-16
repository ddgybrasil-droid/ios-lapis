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

SUBCACHE_BASE = 0x225874000
NOP = 0xD503201F

# (unslid VA, expected stock insn or already-patched, patched insn, label)
PATCHES = (
    # match_callback: fulfill even when Act != Ref
    (0x2288058C8, (0x54000241, NOP), NOP, "fulfill b.ne -> NOP"),
    # match_callback: always take Act_FB++ path (ignore w22 gate)
    (0x2288057B4, (0x34000B96, NOP), NOP, "Act++ cbz w22 -> NOP"),
    # GetMainDisplay: first Disp slot is always "main" (skip +0x98==1 test)
    # stock: cmp w9,#1; b.eq +0x24 → open. Replace b.eq with b.
    (0x2288047DC, (0x54000120, 0x14000009), 0x14000009, "main check b.eq -> b"),
)


def patch_bytes(buf: bytearray, *, write: bool) -> bool:
    changed = False
    for va, expect, want, label in PATCHES:
        fo = va - SUBCACHE_BASE
        cur = struct.unpack_from("<I", buf, fo)[0]
        if cur == want:
            print(f"{va:#x}: already patched ({label})")
            continue
        if cur not in expect:
            raise SystemExit(
                f"{va:#x}: unexpected insn {cur:#x} (want one of "
                f"{[hex(x) for x in expect]}) for {label}"
            )
        print(f"{va:#x}: {label} (fileoff {fo:#x}) {cur:#x} -> {want:#x}")
        if write:
            struct.pack_into("<I", buf, fo, want)
        changed = True
    return changed


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
            # Read a window covering all patch sites.
            base_abs = root_off + off
            f.seek(base_abs)
            buf = bytearray(f.read(size))
            print(f"{pl}:")
            if patch_bytes(buf, write=write) and write:
                f.seek(base_abs)
                f.write(buf)
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
