#!/usr/bin/env python3
"""Neutralize CoverSheet +[CSMainPageContentViewController _phoneListWidth].

SpringBoard CoverSheet abort loop (boot71+): _phoneListWidth dispatch_once
block does UIScreen.mainScreen._referenceBounds; without a real display that
path dies in SIGABRT during dyld init.

Patches (in-image):
  1. TEXT .42: replace whole method [0x223511cb8, 0x223511cf0) with
     `return 834.0` (iPad portrait points), NOP-pad so the old `bl .cold.1`
     is overwritten.
  2. TEXT .42: replace `.cold.1` with store-834 + onceToken=-1 + ret.
  3. DATA .70.dylddata: seed width=834.0 and onceToken=-1.

Usage:
  python3 tools/patch_dsc_coversheet_phonelist.py --image IMG [--write]
  python3 tools/dsc_cd_rehash.py IMG --write
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

FUNC_VA = 0x223511CB8
BLOCK_VA = 0x223511CF0  # ___block_invoke starts here
COLD_VA = 0x22360B0C4
WIDTH_VA = 0x27C7760C8
TOKEN_VA = 0x27C7760D0
WIDTH = 834.0


def _bits() -> int:
    return struct.unpack("<Q", struct.pack("<d", WIDTH))[0]


def width_ret_stub(fill_to: int) -> bytes:
    b = _bits()
    out = bytearray()
    out += struct.pack("<I", 0xD2800000 | ((b & 0xFFFF) << 5))
    out += struct.pack("<I", 0xF2A00000 | (((b >> 16) & 0xFFFF) << 5))
    out += struct.pack("<I", 0xF2C00000 | (((b >> 32) & 0xFFFF) << 5))
    out += struct.pack("<I", 0xF2E00000 | (((b >> 48) & 0xFFFF) << 5))
    out += struct.pack("<I", 0x9E670000)  # fmov d0, x0
    out += struct.pack("<I", 0xD65F03C0)  # ret
    while len(out) < fill_to:
        out += struct.pack("<I", 0xD503201F)
    return bytes(out)


def cold_stub() -> bytes:
    b = _bits()
    out = bytearray()
    va = COLD_VA

    def emit(w: int) -> None:
        nonlocal va
        out.extend(struct.pack("<I", w))
        va += 4

    emit(0xD503201F)
    emit(0xD2800000 | ((b & 0xFFFF) << 5))
    emit(0xF2A00000 | (((b >> 16) & 0xFFFF) << 5))
    emit(0xF2C00000 | (((b >> 32) & 0xFFFF) << 5))
    emit(0xF2E00000 | (((b >> 48) & 0xFFFF) << 5))
    emit(0x9E670000)
    pc = va
    page = WIDTH_VA & ~0xFFF
    imm = ((page - (pc & ~0xFFF)) >> 12) & 0x1FFFFF
    emit(0x90000000 | ((imm >> 2) << 5) | 8 | ((imm & 3) << 29))
    emit(0xFD000000 | (((WIDTH_VA & 0xFFF) // 8) << 10) | (8 << 5) | 0)
    emit(0x92800009)  # movn x9, #0
    emit(0xF9000000 | (((TOKEN_VA & 0xFFF) // 8) << 10) | (8 << 5) | 9)
    emit(0xD65F03C0)
    return bytes(out)


def _patch_va(f, root_off: int, paths, va: int, blob: bytes, suffix: str) -> int:
    n = 0
    seen = set()
    for path, typ, off, size in paths:
        if typ != 2:
            continue
        pl = path if isinstance(path, str) else path.decode()
        if suffix not in pl or "Caches" not in pl:
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
            if addr <= va < addr + msize:
                fo = fileoff + (va - addr)
                break
        if fo is None:
            continue
        abs_off = root_off + off + fo
        f.seek(abs_off)
        old = f.read(len(blob))
        print(f"{pl}: {va:#x} @ {abs_off:#x} {'same' if old == blob else 'patch'}")
        if old != blob:
            f.seek(abs_off)
            f.write(blob)
        n += 1
    return n


def patch_image(img: Path, *, write: bool) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parents[0]))
    from lapisfs_names import gpt_partitions, lapisfs_paths

    func_blob = width_ret_stub(BLOCK_VA - FUNC_VA)
    cold_blob = cold_stub()
    data_blob = struct.pack("<dQ", WIDTH, 0xFFFFFFFFFFFFFFFF)

    mode = "r+b" if write else "rb"
    with img.open(mode) as f:
        parts = gpt_partitions(f)
        root_off = next(p[1] for p in parts if p[0] == "lapis-root")
        f.seek(root_off)
        _, _, paths = lapisfs_paths(f.read(8 << 20))
        if not write:
            print(f"dry-run func={len(func_blob)} cold={len(cold_blob)} width={WIDTH}")
            return
        n1 = _patch_va(f, root_off, paths, FUNC_VA, func_blob, "arm64e.42")
        n2 = _patch_va(f, root_off, paths, COLD_VA, cold_blob, "arm64e.42")
        n3 = _patch_va(f, root_off, paths, WIDTH_VA, data_blob, "70.dylddata")
        if not (n1 and n2 and n3):
            raise SystemExit(f"incomplete patch func={n1} cold={n2} data={n3}")
        print(f"patched func={n1} cold={n2} data={n3}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    patch_image(args.image, write=args.write)


if __name__ == "__main__":
    main()
