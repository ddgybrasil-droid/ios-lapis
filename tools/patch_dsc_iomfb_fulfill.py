#!/usr/bin/env python3
"""Patch IOMobileFramebuffer GetMainDisplay in DSC subcache .44.

boot112–117: populate/AMN/Next can deliver our IOMFB stub, but
IOMobileFramebufferGetMainDisplay still never reaches IOServiceOpen
(Act/Ref wait + main-flag gates). Replace GetMainDisplay with a direct
Matching → GetMatchingServices → Next → IOMobileFramebufferOpen path.

Also keep the match_callback Act/Ref helpers (harmless once GetMainDisplay
no longer waits on them).

Unslid sites in dyld_shared_cache_arm64e.44 (map va=0x225874000):
  GetMainDisplay @0x2288047a8 → b cave
  cave @0x22880511c (was FigInstallVirtualDisplay helper; MediaToolbox absent)
  fulfill b.ne @0x2288058c8, Act++ cbz @0x2288057b4

Usage:
  python3 tools/patch_dsc_iomfb_fulfill.py --image /tmp/fullroot-mnt2.img --write
  python3 tools/dsc_cd_rehash.py IMG --write
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

SUBCACHE_BASE = 0x225874000
NOP = 0xD503201F
GMD = 0x2288047A8
CAVE = 0x22880511C
STR = 0x22883898F  # "IOMobileFramebuffer"
MATCH = 0x22C6CE7F0
GMS_SINGULAR = 0x22C6CE7D0  # IOServiceGetMatchingService
GMS_PLURAL = 0x22C6CE7E0  # IOServiceGetMatchingServices
NEXT = 0x22C6CE6E0  # IOIteratorNext
REL = 0x22C6CE740  # IOObjectRelease
OPEN = 0x2288059E0
# Master port GOT slot used by GetMatchingService callers (d50, not d48).
MASTER_ADRP, MASTER_OFF = 0x277B6A000, 0xD50
TASK_ADRP, TASK_OFF = 0x277C1E000, 0xCF8

# Small helper NOPs (optional if GetMainDisplay cave is applied).
HELPER_PATCHES = (
    (0x2288058C8, (0x54000241, NOP), NOP, "fulfill b.ne -> NOP"),
    (0x2288057B4, (0x34000B96, NOP), NOP, "Act++ cbz w22 -> NOP"),
)


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


def enc_ldr_x(rt: int, rn: int, imm: int) -> int:
    return 0xF9400000 | ((imm // 8) << 10) | (rn << 5) | rt


def enc_ldr_w(rt: int, rn: int, imm: int) -> int:
    return 0xB9400000 | ((imm // 4) << 10) | (rn << 5) | rt


def build_cave() -> list[int]:
    """Matching → GetMatchingServices(&it) → Next → Open.

    IOServiceGetMatchingServices(master, matching, io_iterator_t *existing)
    returns kern_return_t and writes the iterator through x2 — boot118/120
    wrongly treated x0 as the iterator. Singular GetMatchingService matched
    in-kernel but returned a null userspace port for our publishResource
    stubs; plural+Next is the proven path (boot112+).
    """
    code: list[int] = []
    pc = CAVE

    def emit(w: int) -> None:
        nonlocal pc
        code.append(w)
        pc += 4

    # Frame: [sp,#0x8]=iterator out, [sp,#0x20]=x22/x21, [sp,#0x30]=x20/x19,
    # [sp,#0x40]=x29/x30. Keep sp 16-byte aligned.
    emit(0xD503237F)  # pacibsp
    emit(0xD10143FF)  # sub sp, sp, #0x50
    emit(0xA90257F6)  # stp x22, x21, [sp, #0x20]
    emit(0xA9034FF4)  # stp x20, x19, [sp, #0x30]
    emit(0xA9047BFD)  # stp x29, x30, [sp, #0x40]
    emit(0x910103FD)  # add x29, sp, #0x40
    emit(0xAA0003F3)  # mov x19, x0  (out*)
    cbz0 = len(code)
    emit(0)  # cbz x19 → fail

    emit(enc_adrp(0, pc, STR))
    emit(enc_add_imm(0, 0, STR & 0xFFF))
    emit(enc_bl(pc, MATCH))
    emit(0xAA0003F5)  # mov x21, x0  (matching)
    cbz1 = len(code)
    emit(0)  # cbz x21 → fail

    emit(enc_adrp(8, pc, MASTER_ADRP))
    emit(enc_ldr_x(8, 8, MASTER_OFF))
    emit(enc_ldr_w(0, 8, 0))  # master
    emit(0xAA1503E1)  # mov x1, x21
    emit(0xB9000BFF)  # str wzr, [sp, #0x8]
    emit(0x910023E2)  # add x2, sp, #0x8  (&iterator)
    emit(enc_bl(pc, GMS_PLURAL))
    cbnz_kr = len(code)
    emit(0)  # cbnz w0 → fail (kern_return_t)
    emit(0xB9400BF4)  # ldr w20, [sp, #0x8]  (iterator)
    cbz2 = len(code)
    emit(0)  # cbz w20 → fail

    emit(0xAA1403E0)  # mov x0, x20
    emit(enc_bl(pc, NEXT))
    emit(0xAA0003F6)  # mov x22, x0  (service)
    emit(0xAA1403E0)  # mov x0, x20
    emit(enc_bl(pc, REL))  # release iterator
    cbz3 = len(code)
    emit(0)  # cbz w22 → fail

    emit(enc_adrp(8, pc, TASK_ADRP))
    emit(enc_ldr_x(8, 8, TASK_OFF))
    emit(enc_ldr_w(1, 8, 0))  # task
    emit(0xAA1603E0)  # mov x0, x22
    emit(0x52800002)  # movz w2, #0
    emit(0xAA1303E3)  # mov x3, x19
    emit(enc_bl(pc, OPEN))
    epi = pc
    emit(0xA9447BFD)
    emit(0xA9434FF4)
    emit(0xA94257F6)
    emit(0x910143FF)
    emit(0xD65F03C0)
    fail = pc
    emit(0x52805800)  # movz w0, #0x2c0
    emit(0x72BC0000)  # movk w0, #0xe000, lsl #16
    emit(enc_b(pc, epi))

    def patch_cbz_x(idx: int, rt: int, tgt: int) -> None:
        p = CAVE + idx * 4
        imm19 = ((tgt - p) >> 2) & 0x7FFFF
        code[idx] = 0xB4000000 | (imm19 << 5) | rt

    def patch_cbz_w(idx: int, rt: int, tgt: int) -> None:
        p = CAVE + idx * 4
        imm19 = ((tgt - p) >> 2) & 0x7FFFF
        code[idx] = 0x34000000 | (imm19 << 5) | rt

    def patch_cbnz_w(idx: int, rt: int, tgt: int) -> None:
        p = CAVE + idx * 4
        imm19 = ((tgt - p) >> 2) & 0x7FFFF
        code[idx] = 0x35000000 | (imm19 << 5) | rt

    patch_cbz_x(cbz0, 19, fail)
    patch_cbz_x(cbz1, 21, fail)
    patch_cbnz_w(cbnz_kr, 0, fail)
    patch_cbz_w(cbz2, 20, fail)
    patch_cbz_w(cbz3, 22, fail)
    return code


def patch_bytes(buf: bytearray, *, write: bool) -> bool:
    changed = False
    for va, expect, want, label in HELPER_PATCHES:
        fo = va - SUBCACHE_BASE
        cur = struct.unpack_from("<I", buf, fo)[0]
        if cur == want:
            print(f"{va:#x}: already patched ({label})")
            continue
        if cur not in expect:
            print(f"{va:#x}: skip helper ({label}): insn {cur:#x}")
            continue
        print(f"{va:#x}: {label} {cur:#x} -> {want:#x}")
        if write:
            struct.pack_into("<I", buf, fo, want)
        changed = True

    cave = build_cave()
    gmd_br = enc_b(GMD, CAVE)
    fo_gmd = GMD - SUBCACHE_BASE
    cur = struct.unpack_from("<I", buf, fo_gmd)[0]
    if cur != gmd_br:
        print(f"{GMD:#x}: GetMainDisplay -> b cave ({cur:#x} -> {gmd_br:#x})")
        if write:
            struct.pack_into("<I", buf, fo_gmd, gmd_br)
        changed = True
    else:
        print(f"{GMD:#x}: GetMainDisplay already branches to cave")

    fo_cave = CAVE - SUBCACHE_BASE
    blob = b"".join(struct.pack("<I", w) for w in cave)
    if buf[fo_cave : fo_cave + len(blob)] != blob:
        print(f"{CAVE:#x}: write open cave ({len(blob)} bytes)")
        if write:
            buf[fo_cave : fo_cave + len(blob)] = blob
        changed = True
    else:
        print(f"{CAVE:#x}: open cave already present")
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
