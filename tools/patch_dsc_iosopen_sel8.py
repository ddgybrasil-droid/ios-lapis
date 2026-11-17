#!/usr/bin/env python3
"""Revert IOServiceOpen stub wrap; keep Open-wrap sel5 (UCAL).

boot184: identity wrap fixed GMD FAR=0; FDR GMD_RET x0=0; BB IOMFB open
stable but no sel8 (GMD seeds WxH). boot185: fully restore stub to
ADRP/ADD/BR from clean DSC — wrap no longer needed.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SUB44_BASE = 0x225874000
SUB45_BASE = 0x22C66C000
IOSOPEN_STUB = 0x22C6CE800
WRAP_CAVE = 0x228831528
SEL_SITE = 0x22882C5AC
# Original stub from fullroot-clean.img
STUB_ORIG = (0xF0B5BC90, 0x9118B210, 0xD61F0200)  # adrp/add/br → real open
NOP = 0xD503201F


def patch_44(buf: bytearray, *, write: bool) -> bool:
    changed = False
    fo = SEL_SITE - SUB44_BASE
    cur = struct.unpack_from("<I", buf, fo)[0]
    want = 0x528000A1  # movz w1, #5
    if cur == want:
        print(f"{SEL_SITE:#x}: already sel5")
    elif cur == 0x52800101:
        print(f"{SEL_SITE:#x}: movz w1,#8 -> #5")
        if write:
            struct.pack_into("<I", buf, fo, want)
        changed = True
    else:
        print(f"{SEL_SITE:#x}: unexpected {cur:#x}")

    # Scrub wrap cave so a stray branch cannot execute stale code
    fo = WRAP_CAVE - SUB44_BASE
    pad = b"".join(struct.pack("<I", NOP) for _ in range(64))
    print(f"{WRAP_CAVE:#x}: NOP-scrub wrap cave")
    if write:
        buf[fo : fo + len(pad)] = pad
    changed = True
    return changed


def patch_45(buf: bytearray, *, write: bool) -> bool:
    fo = IOSOPEN_STUB - SUB45_BASE
    cur = struct.unpack_from("<III", buf, fo)
    if cur == STUB_ORIG:
        print(f"{IOSOPEN_STUB:#x}: stub already original ADRP/ADD/BR")
        return False
    print(f"{IOSOPEN_STUB:#x}: restore stub {[hex(w) for w in cur]} -> "
          f"{[hex(w) for w in STUB_ORIG]}")
    if write:
        struct.pack_into("<III", buf, fo, *STUB_ORIG)
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
            if off in seen:
                continue
            if pl.endswith("dyld_shared_cache_arm64e.44"):
                seen.add(off)
                base_abs = root_off + off
                f.seek(base_abs)
                buf = bytearray(f.read(size))
                print(f"{pl}:")
                if patch_44(buf, write=write) and write:
                    f.seek(base_abs)
                    f.write(buf)
                n += 1
            elif pl.endswith("dyld_shared_cache_arm64e.45"):
                seen.add(off)
                base_abs = root_off + off
                f.seek(base_abs)
                buf = bytearray(f.read(size))
                print(f"{pl}:")
                if patch_45(buf, write=write) and write:
                    f.seek(base_abs)
                    f.write(buf)
                n += 1
        if n < 2:
            raise SystemExit(f"expected .44+.45, got {n}")
        print(f"{'wrote' if write else 'would write'} {n} extent(s)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    if args.write and not args.force:
        raise SystemExit("refusing --write without --force")
    patch_image(args.image, write=args.write)


if __name__ == "__main__":
    main()
