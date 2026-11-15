#!/usr/bin/env python3
"""Stub +[CSMainPageContentViewController _phoneListWidth] in DSC .42.

SpringBoard CoverSheet abort loop (boot71): during dyld init, _phoneListWidth
dispatch_once block does UIScreen.mainScreen._referenceBounds width. With no
real display, UIScreen is nil/zero and later CoverSheet/UIKit paths die in
SIGABRT. Replace the method with a constant return (iPad portrait points).

Unslid: 0x223511cb8 in dyld_shared_cache_arm64e.42
Returns d0 = 834.0 (iPad Pro 11" logical width).

Usage:
  python3 tools/patch_dsc_coversheet_phonelist.py --image IMG [--write]
Then dsc_cd_rehash.py IMG --write (and reseal FIPS if .31 was also touched).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

FUNC_VA = 0x223511CB8
# Keep patch small; original fast-path is ~16 bytes before cold path.
WIDTH = 834.0  # IEEE754: 0x40A1400000000000


def build_stub() -> bytes:
    bits = struct.unpack("<Q", struct.pack("<d", WIDTH))[0]
    out = bytearray()
    # movz x0, #(bits & 0xffff)
    out += struct.pack("<I", 0xD2800000 | ((bits & 0xFFFF) << 5) | 0)
    # movk x0, #((bits>>16)&0xffff), lsl #16
    out += struct.pack("<I", 0xF2A00000 | (((bits >> 16) & 0xFFFF) << 5) | 0)
    # movk x0, #((bits>>32)&0xffff), lsl #32
    out += struct.pack("<I", 0xF2C00000 | (((bits >> 32) & 0xFFFF) << 5) | 0)
    # movk x0, #((bits>>48)&0xffff), lsl #48
    out += struct.pack("<I", 0xF2E00000 | (((bits >> 48) & 0xFFFF) << 5) | 0)
    # fmov d0, x0
    out += struct.pack("<I", 0x9E670000)
    # ret
    out += struct.pack("<I", 0xD65F03C0)
    return bytes(out)


def patch_image(img: Path, *, write: bool) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parents[0]))
    from lapisfs_names import gpt_partitions, lapisfs_paths

    stub = build_stub()
    with img.open("r+b" if write else "rb") as f:
        parts = gpt_partitions(f)
        root_off = next(p[1] for p in parts if p[0] == "lapis-root")
        f.seek(root_off)
        _, _, paths = lapisfs_paths(f.read(8 << 20))
        n = 0
        seen = set()
        for path, typ, off, size in paths:
            if typ != 2:
                continue
            pl = path if isinstance(path, str) else path.decode()
            if "dyld_shared_cache_arm64e.42" not in pl:
                continue
            if off in seen:
                continue
            seen.add(off)
            f.seek(root_off + off + 0x10)
            moff, cnt = struct.unpack("<II", f.read(8))
            f.seek(root_off + off + moff)
            fo = None
            for _ in range(cnt):
                addr, msize, fileoff, maxp, initp = struct.unpack("<QQQII", f.read(32))
                if addr <= FUNC_VA < addr + msize:
                    fo = fileoff + (FUNC_VA - addr)
                    break
            if fo is None:
                continue
            abs_off = root_off + off + fo
            f.seek(abs_off)
            old = f.read(len(stub))
            print(f"{pl}: site @ {abs_off:#x} old={old.hex()}")
            print(f"  new={stub.hex()} width={WIDTH}")
            if old == stub:
                print("  already patched")
            elif write:
                f.seek(abs_off)
                f.write(stub)
                print("  wrote")
            n += 1
        if not n:
            raise SystemExit("no .42 extent with CoverSheet site")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    patch_image(args.image, write=args.write)


if __name__ == "__main__":
    main()
