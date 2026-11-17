#!/usr/bin/env python3
"""Wrap IOServiceOpen stub so type=0 opens also probe GetDisplaySize (sel 8).

boot173: APPLIED — BB EXC (FAR=0) because wrap used inputCnt=1 with NULL
input (IOCM deref). boot181: fixed ABI — inputCnt=0, outputCnt=2 buffer.

Sites (unslid):
  IOSOPEN stub @0x22c6ce800 in .45 → br wrap cave
  wrap cave @0x228831528 in .44 (zero pad)
  real IOServiceOpen @0x197e6162c
  IOConnectCallMethod stub @0x22c6ce650

Also flips Open-wrap movz w1,#5 → #8 at 0x22882c5ac.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SUB44_BASE = 0x225874000
SUB45_BASE = 0x22C66C000
IOSOPEN_STUB = 0x22C6CE800
IOCM_STUB = 0x22C6CE650
REAL_OPEN = 0x197E6162C
WRAP_CAVE = 0x228831528
SEL5_SITE = 0x22882C5AC  # movz w1, #5 → #8
NOP = 0xD503201F


def enc_b(pc: int, tgt: int) -> int:
    return 0x14000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)


def enc_bl(pc: int, tgt: int) -> int:
    return 0x94000000 | (((tgt - pc) >> 2) & 0x3FFFFFF)


def enc_adrp(rd: int, pc: int, tgt: int) -> int:
    page = tgt & ~0xFFF
    pc_page = pc & ~0xFFF
    imm = ((page - pc_page) >> 12) & 0x1FFFFF
    return 0x90000000 | ((imm >> 2) << 5) | rd | ((imm & 3) << 29)


def enc_add_imm(rd: int, rn: int, imm12: int) -> int:
    return 0x91000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd


def enc_cbz_w(pc: int, rt: int, tgt: int) -> int:
    imm19 = ((tgt - pc) >> 2) & 0x7FFFF
    return 0x34000000 | (imm19 << 5) | rt


def enc_cbnz_w(pc: int, rt: int, tgt: int) -> int:
    imm19 = ((tgt - pc) >> 2) & 0x7FFFF
    return 0x35000000 | (imm19 << 5) | rt


def build_wrap() -> list[int]:
    """IOServiceOpen wrap: real open, then sel8 probe if type==0 && kr==0.

    boot181 ABI: IOConnectCallMethod(conn, 8, NULL, 0, NULL, 0, out, &cnt)
    with cnt=2. Prior wrap used inputCnt=1 + NULL input → FAR=0.
    """
    code: list[int] = []
    pc = WRAP_CAVE

    def emit(w: int) -> None:
        nonlocal pc
        code.append(w)
        pc += 4

    # Frame 0x80: save regs + out[2] uint64 @0x60 + outputCnt @0x70.
    # boot182: NO pacibsp/retab — stub is entered via B from an ADRP/BR
    # stub (LR already signed by caller's blr). pacibsp+retab BRK'd BB
    # after HID NOTE64 in boot181 despite sel8 success on GMD open.
    emit(0xD10203FF)  # sub sp, sp, #0x80
    emit(0xA9027BFD)  # stp x29, x30, [sp, #0x20]
    emit(0xA9036FFC)  # stp x28, x27, [sp, #0x30]
    emit(0xA90467FA)  # stp x26, x25, [sp, #0x40]
    emit(0xA9055FF8)  # stp x24, x23, [sp, #0x50]
    emit(0xA90157F6)  # stp x22, x21, [sp, #0x10]
    emit(0xA9004FF4)  # stp x20, x19, [sp]
    emit(0x910083FD)  # add x29, sp, #0x20

    emit(0xAA0003F3)  # mov x19, x0  service
    emit(0xAA0103F4)  # mov x20, x1  owningTask
    emit(0xAA0203F5)  # mov x21, x2  type
    emit(0xAA0303F6)  # mov x22, x3  connect*
    emit(0xAA0403F7)  # mov x23, x4
    emit(0xAA0503F8)  # mov x24, x5
    emit(0xAA0603F9)  # mov x25, x6
    emit(0xAA0703FA)  # mov x26, x7

    emit(0xAA1303E0)
    emit(0xAA1403E1)
    emit(0xAA1503E2)
    emit(0xAA1603E3)
    emit(0xAA1703E4)
    emit(0xAA1803E5)
    emit(0xAA1903E6)
    emit(0xAA1A03E7)
    emit(enc_adrp(16, pc, REAL_OPEN))
    emit(enc_add_imm(16, 16, REAL_OPEN & 0xFFF))
    emit(0xD63F0200)  # blr x16
    emit(0xAA0003FB)  # mov x27, x0  kr

    cbnz_kr = len(code)
    emit(0)
    cbnz_ty = len(code)
    emit(0)

    # conn = *(uint32_t *)connect; if 0 → epi
    emit(0xB94002C0)  # ldr w0, [x22]
    cbz_conn = len(code)
    emit(0)

    # zero out[2]; *outputCnt = 2
    emit(0xA9007FEF)  # stp xzr, xzr, [sp, #0x60]
    emit(0x52800048)  # movz w8, #2
    emit(0xB90073E8)  # str w8, [sp, #0x70]

    # IOConnectCallMethod(conn, 8, NULL, 0, NULL, 0, &out[0], &cnt)
    # w0 already = conn
    emit(0x52800101)  # movz w1, #8
    emit(0xD2800002)  # movz x2, #0   input
    emit(0x52800003)  # movz w3, #0   inputCnt  (boot181 fix)
    emit(0xD2800004)  # movz x4, #0   inputStruct
    emit(0xD2800005)  # movz x5, #0   inputStructCnt
    emit(0x910183E6)  # add x6, sp, #0x60  output
    emit(0x9101C3E7)  # add x7, sp, #0x70  outputCnt*
    emit(enc_bl(pc, IOCM_STUB))
    # ignore sel8 kr — always return original open kr

    epi = pc
    emit(0xAA1B03E0)  # mov x0, x27
    emit(0xA9404FF4)
    emit(0xA94157F6)
    emit(0xA9427BFD)
    emit(0xA9436FFC)
    emit(0xA94467FA)
    emit(0xA9455FF8)
    emit(0x910203FF)  # add sp, sp, #0x80
    emit(0xD65F03C0)  # ret (plain; LR from caller blr)

    def patch(idx: int, word: int) -> None:
        code[idx] = word

    p = WRAP_CAVE + cbnz_kr * 4
    patch(cbnz_kr, enc_cbnz_w(p, 27, epi))
    p = WRAP_CAVE + cbnz_ty * 4
    patch(cbnz_ty, enc_cbnz_w(p, 21, epi))
    p = WRAP_CAVE + cbz_conn * 4
    patch(cbz_conn, enc_cbz_w(p, 0, epi))
    return code


def patch_44(buf: bytearray, *, write: bool) -> bool:
    changed = False
    fo = SEL5_SITE - SUB44_BASE
    cur = struct.unpack_from("<I", buf, fo)[0]
    want = 0x52800101  # movz w1, #8
    if cur == want:
        print(f"{SEL5_SITE:#x}: already sel8")
    elif cur == 0x528000A1:  # movz w1, #5
        print(f"{SEL5_SITE:#x}: movz w1,#5 -> #8")
        if write:
            struct.pack_into("<I", buf, fo, want)
        changed = True
    else:
        print(f"{SEL5_SITE:#x}: unexpected {cur:#x}")

    cave = build_wrap()
    blob = b"".join(struct.pack("<I", w) for w in cave)
    fo = WRAP_CAVE - SUB44_BASE
    if buf[fo : fo + len(blob)] != blob:
        if any(buf[fo : fo + len(blob)]):
            nz = sum(1 for b in buf[fo : fo + len(blob)] if b)
            if nz > 8 and buf[fo : fo + 4] != blob[:4]:
                print(f"{WRAP_CAVE:#x}: non-zero pad ({nz} bytes) — writing anyway")
        print(f"{WRAP_CAVE:#x}: write IOSOPEN→sel8 wrap ({len(blob)} bytes)")
        if write:
            buf[fo : fo + len(blob)] = blob
        changed = True
    else:
        print(f"{WRAP_CAVE:#x}: wrap cave already present")
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
    ap.add_argument("--force", action="store_true",
                    help="required for --write (boot173 EXC'd BB)")
    args = ap.parse_args()
    if args.write and not args.force:
        raise SystemExit("refusing --write without --force (boot173 EXC)")
    patch_image(args.image, write=args.write)


if __name__ == "__main__":
    main()
