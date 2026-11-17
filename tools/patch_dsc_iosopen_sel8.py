#!/usr/bin/env python3
"""IOServiceOpen stub wrap — boot184: identity open only (no nested sel8).

boot181–183: nested GetDisplaySize after open reached kernel sel=8, but GMD
then FAR=0 at `str x21,[x19]` (out* clobbered). GMD already seeds 2420×1668
into the light object after open; BB also calls sel=8 itself (scalars).

Also restores Open-wrap movz w1,#8 → #5 ('UCAL' FourCC input was sel 5).

Sites (unslid):
  IOSOPEN stub @0x22c6ce800 in .45 → br wrap cave
  wrap cave @0x228831528 in .44
  real IOServiceOpen @0x197e6162c
  Open-wrap sel @0x22882c5ac
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SUB44_BASE = 0x225874000
SUB45_BASE = 0x22C66C000
IOSOPEN_STUB = 0x22C6CE800
REAL_OPEN = 0x197E6162C
WRAP_CAVE = 0x228831528
SEL_SITE = 0x22882C5AC
NOP = 0xD503201F


def enc_b(pc: int, tgt: int) -> int:
    return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)


def enc_adrp(rd: int, pc: int, tgt: int) -> int:
    page = tgt & ~0xFFF
    pc_page = pc & ~0xFFF
    imm = ((page - pc_page) >> 12) & 0x1FFFFF
    return 0x90000000 | ((imm >> 2) << 5) | rd | ((imm & 3) << 29)


def enc_add_imm(rd: int, rn: int, imm12: int) -> int:
    return 0x91000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd


def build_wrap() -> list[int]:
    """Identity wrap: save args, blr real open, restore, plain ret."""
    code: list[int] = []
    pc = WRAP_CAVE

    def emit(w: int) -> None:
        nonlocal pc
        code.append(w)
        pc += 4

    # Minimal frame — do not touch more than we must (boot183 x19 clobber).
    emit(0xD10083FF)  # sub sp, sp, #0x20
    emit(0xA9007BFD)  # stp x29, x30, [sp]
    emit(0x910003FD)  # add x29, sp, #0
    emit(enc_adrp(16, pc, REAL_OPEN))
    emit(enc_add_imm(16, 16, REAL_OPEN & 0xFFF))
    emit(0xD63F0200)  # blr x16
    emit(0xA9407BFD)  # ldp x29, x30, [sp]
    emit(0x910083FF)  # add sp, sp, #0x20
    emit(0xD65F03C0)  # ret
    return code


def patch_44(buf: bytearray, *, write: bool) -> bool:
    changed = False
    fo = SEL_SITE - SUB44_BASE
    cur = struct.unpack_from("<I", buf, fo)[0]
    want = 0x528000A1  # movz w1, #5 (UCAL path)
    if cur == want:
        print(f"{SEL_SITE:#x}: already sel5")
    elif cur == 0x52800101:  # movz w1, #8
        print(f"{SEL_SITE:#x}: movz w1,#8 -> #5 (restore UCAL)")
        if write:
            struct.pack_into("<I", buf, fo, want)
        changed = True
    else:
        print(f"{SEL_SITE:#x}: unexpected {cur:#x}")

    cave = build_wrap()
    blob = b"".join(struct.pack("<I", w) for w in cave)
    # NOP-pad old larger cave so stale sel8 probe bytes cannot execute
    pad = blob + bytes([0x1F, 0x20, 0x03, 0xD5]) * 48
    fo = WRAP_CAVE - SUB44_BASE
    print(f"{WRAP_CAVE:#x}: write identity wrap ({len(blob)} bytes) + NOP pad")
    if write:
        buf[fo : fo + len(pad)] = pad
    changed = True
    return changed


def patch_45(buf: bytearray, *, write: bool) -> bool:
    fo = IOSOPEN_STUB - SUB45_BASE
    want = enc_b(IOSOPEN_STUB, WRAP_CAVE)
    cur = struct.unpack_from("<I", buf, fo)[0]
    if cur == want:
        print(f"{IOSOPEN_STUB:#x}: stub already branches to wrap")
        return False
    print(f"{IOSOPEN_STUB:#x}: stub {cur:#x} -> b wrap {want:#x}")
    if write:
        struct.pack_into("<I", buf, fo, want)
        struct.pack_into("<I", buf, fo + 4, NOP)
        struct.pack_into("<I", buf, fo + 8, NOP)
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
