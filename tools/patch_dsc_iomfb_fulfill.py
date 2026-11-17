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
# boot124: direct IOServiceOpen reached FDR IOSOPEN+nuc (x0=0); raw connect in
# *out then crashed FDR (FAR=0x111f). boot125: full ALLOC helper @0x2288065e4
# never reached IOSOPEN (CFPrefs side-work / typed-malloc path). boot126:
# lightweight typed-malloc 0xf30 + minimal field init, skip ALLOC prefs.
IOSOPEN = 0x22C6CE800
# boot224: real IOMobileFramebufferOpen post-path calls this with w1=#5
# (UCAL) right after IOServiceOpen — cave was skipping it (0 post-GMD IOEXT).
CALLMETHOD = 0x22C6CE650
WRAP_SEL8 = 0x228831528  # boot226/227: sel8 helper (no main-cave growth)
TYPE_INIT = 0x22880686C  # fills malloc-type slot at TYPE_SLOT
TYPED_MALLOC = 0x22C6CE8B0  # malloc_type_malloc(zone=0, type, size, 0)
TYPE_SLOT_ADRP, TYPE_SLOT_OFF = 0x2801A3000, 0x520
OBJ_SIZE = 0xF30
# Const blob copied by real ALLOC into obj+0x880 (16 bytes).
OBJ_CONST_ADRP, OBJ_CONST_OFF = 0x228832000, 0xF40
OBJ_SUBINIT = 0x22C6CEBA0  # ALLOC calls this on (obj+0x840)
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
    """Matching → GetMatchingServices(&it) → Next → light object → Open.

    boot125 called the full IOMFB ALLOC helper (typed malloc + CFPrefs init).
    That never reached IOServiceOpen. boot126: resolve malloc-type, typed-malloc
    0xf30, zero+minimal fields, IOServiceOpen into +0x14, *out=object. If the
    light alloc fails, raw IOServiceOpen into *out (boot124) so FDR still opens.
    """
    code: list[int] = []
    pc = CAVE

    def emit(w: int) -> None:
        nonlocal pc
        code.append(w)
        pc += 4

    # Frame: [sp,#0x8]=iterator out, [sp,#0x20]=x22/x21, [sp,#0x30]=x20/x19,
    # [sp,#0x40]=x29/x30. Keep sp 16-byte aligned. (boot202: no x23 — that
    # path nulled *out in boot201 even without cave insn breakpoints.)
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
    emit(0)  # cbnz w0 → fail
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

    # --- light typed-malloc object (skip ALLOC CFPrefs) ---
    emit(enc_adrp(8, pc, TYPE_SLOT_ADRP))
    emit(enc_ldr_x(1, 8, TYPE_SLOT_OFF))  # type id
    cbnz_type = len(code)
    emit(0)  # cbnz x1 → have_type
    emit(enc_bl(pc, TYPE_INIT))
    emit(enc_adrp(8, pc, TYPE_SLOT_ADRP))
    emit(enc_ldr_x(1, 8, TYPE_SLOT_OFF))
    have_type = pc
    cbz_type = len(code)
    emit(0)  # cbz x1 → raw_fallback
    emit(0xD2800000)  # movz x0, #0
    emit(0x5281E602)  # movz w2, #0xf30
    emit(0xD2800003)  # movz x3, #0
    emit(enc_bl(pc, TYPED_MALLOC))
    emit(0xAA0003F5)  # mov x21, x0  (obj)
    cbz_obj = len(code)
    emit(0)  # cbz x21 → raw_fallback

    # boot202: wipe from +0x10 so typed_malloc isa at [x21] survives.
    # boot204: also zero +0x08 (first ivar) — leftover typed_malloc qword
    # beside isa may break objc once isa is live (boot203: 0 SURFACE).
    emit(0x6F00E400)  # movi v0.2d, #0
    emit(0x910042A8)  # add x8, x21, #0x10
    emit(0x52801E49)  # movz w9, #0xf2  ((0xf30-0x10)/16)
    zloop = pc
    emit(0x3C810500)  # str q0, [x8], #16
    emit(0x71000529)  # subs w9, w9, #1
    bne_z = len(code)
    emit(0)  # b.ne zloop
    emit(0xF90006BF)  # str xzr, [x21, #0x8]
    emit(0xB90012B6)  # str w22, [x21, #0x10]  (service)
    emit(0x52800028)  # movz w8, #1
    emit(0xB90C0AA8)  # str w8, [x21, #0xc08]
    emit(0xB9083AA8)  # str w8, [x21, #0x838]
    emit(0x12800008)  # movn w8, #0
    emit(0xB90DCAA8)  # str w8, [x21, #0xdc8]
    emit(enc_adrp(8, pc, OBJ_CONST_ADRP))
    emit(0x3DC3D101)  # ldr q1, [x8, #0xf40]
    emit(0x3D8222A1)  # str q1, [x21, #0x880]
    emit(0x912102A0)  # add x0, x21, #0x840
    emit(0xD2800001)  # movz x1, #0
    emit(enc_bl(pc, OBJ_SUBINIT))
    b_have = len(code)
    emit(0)  # b have_obj

    # --- raw fallback (boot124) ---
    raw_fallback = pc
    emit(enc_adrp(8, pc, TASK_ADRP))
    emit(enc_ldr_x(8, 8, TASK_OFF))
    emit(enc_ldr_w(1, 8, 0))
    emit(0xAA1603E0)  # mov x0, x22
    emit(0x52800002)  # movz w2, #0
    emit(0xAA1303E3)  # mov x3, x19
    emit(enc_bl(pc, IOSOPEN))
    emit(0xAA0003F4)  # mov x20, x0
    emit(0xAA1603E0)
    emit(enc_bl(pc, REL))
    emit(0xAA1403E0)
    b_epi0 = len(code)
    emit(0)  # b epi

    # --- have_obj: IOServiceOpen into +0x14 ---
    have_obj = pc
    emit(enc_adrp(8, pc, TASK_ADRP))
    emit(enc_ldr_x(8, 8, TASK_OFF))
    emit(enc_ldr_w(1, 8, 0))
    emit(0xAA1603E0)
    emit(0x52800002)
    emit(0x910052A3)  # add x3, x21, #0x14
    emit(enc_bl(pc, IOSOPEN))
    emit(0xAA0003F4)  # mov x20, x0 (kr)
    # boot165: on Open success, seed panel size into the light object so
    # GetMainDisplay callers see 2420×1668 without a separate sel8 round-trip.
    cbnz_sz = len(code)
    emit(0)  # cbnz w20 → skip_size
    # boot224: mirror Open post-path IOConnectCallMethod(conn, 5, …)
    # (site 0x22882c5ac). Selector 5 is UCAL; stub @CALLMETHOD.
    emit(0xB94016A0)  # ldr w0, [x21, #0x14]  connection
    emit(0xA9007FFF)  # stp xzr, xzr, [sp]
    emit(0x910043E2)  # add x2, sp, #0x10
    emit(0xF90007FF)  # str xzr, [sp, #0x10]  one zero input scalar
    emit(0x528000A1)  # movz w1, #5
    emit(0x52800023)  # movz w3, #1
    emit(0xD2800004)  # movz x4, #0
    emit(0xD2800005)  # movz x5, #0
    emit(0xD2800006)  # movz x6, #0
    emit(0xD2800007)  # movz x7, #0
    emit(enc_bl(pc, CALLMETHOD))
    # boot227: sel=8 via WRAP_SEL8 (one BL) — avoids growing main cave.
    emit(enc_bl(pc, WRAP_SEL8))
    emit(0x52812E88)  # movz w8, #2420
    emit(0xB90032A8)  # str w8, [x21, #0x30]
    emit(0x5280D088)  # movz w8, #1668
    emit(0xB90036A8)  # str w8, [x21, #0x34]
    emit(0x52812E88)  # movz w8, #2420
    emit(0xB9003AA8)  # str w8, [x21, #0x38]
    emit(0x5280D088)  # movz w8, #1668
    emit(0xB9003EA8)  # str w8, [x21, #0x3c]
    # boot222: seed fulfilled/main @+0x98 and active flags @+0x838/+0xc08
    # (boot220 GDB FLAG98 / f838 / fc08) so CA sees them without RetBP races.
    emit(0x52800028)  # movz w8, #1
    emit(0xB9009AA8)  # str w8, [x21, #0x98]
    emit(0x52800028)  # movz w8, #1
    emit(0xB9083AA8)  # str w8, [x21, #0x838]
    emit(0x52800028)  # movz w8, #1
    emit(0xB90C0AA8)  # str w8, [x21, #0xc08]
    skip_size = pc
    emit(0xF9000275)  # str x21, [x19]
    emit(0xAA1603E0)
    emit(enc_bl(pc, REL))
    emit(0xAA1403E0)
    epi = pc
    emit(0xA9447BFD)  # ldp x29, x30, [sp, #0x40]
    emit(0xA9434FF4)  # ldp x20, x19, [sp, #0x30]
    emit(0xA94257F6)  # ldp x22, x21, [sp, #0x20]
    emit(0x910143FF)  # add sp, sp, #0x50
    emit(0xD65F03C0)  # ret
    fail = pc
    emit(0x52805800)
    emit(0x72BC0000)
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

    def patch_cbnz_x(idx: int, rt: int, tgt: int) -> None:
        p = CAVE + idx * 4
        imm19 = ((tgt - p) >> 2) & 0x7FFFF
        code[idx] = 0xB5000000 | (imm19 << 5) | rt

    def patch_b(idx: int, tgt: int) -> None:
        p = CAVE + idx * 4
        code[idx] = enc_b(p, tgt)

    def patch_bne(idx: int, tgt: int) -> None:
        p = CAVE + idx * 4
        imm19 = ((tgt - p) >> 2) & 0x7FFFF
        code[idx] = 0x54000000 | (imm19 << 5) | 0x1  # b.ne

    patch_cbz_x(cbz0, 19, fail)
    patch_cbz_x(cbz1, 21, fail)
    patch_cbnz_w(cbnz_kr, 0, fail)
    patch_cbz_w(cbz2, 20, fail)
    patch_cbz_w(cbz3, 22, fail)
    patch_cbnz_x(cbnz_type, 1, have_type)
    patch_cbz_x(cbz_type, 1, raw_fallback)
    patch_cbz_x(cbz_obj, 21, raw_fallback)
    patch_bne(bne_z, zloop)
    patch_b(b_have, have_obj)
    patch_b(b_epi0, epi)
    patch_cbnz_w(cbnz_sz, 20, skip_size)
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

    # boot227/228: sel=8 helper. boot227 reused cave SP and CallMethod
    # smashed saved x21/x30 → no GMD_BODY_RET. Allocate a private frame.
    wrap: list[int] = []
    wpc = WRAP_SEL8

    def wemit(insn: int) -> None:
        nonlocal wpc
        wrap.append(insn)
        wpc += 4

    # boot229: must save x30 across BL CALLMETHOD or ret loops forever
    # (boot227/228: GMD thread stuck, no GMD_BODY_RET).
    wemit(0xD10083FF)  # sub sp, sp, #0x20
    wemit(0xF9000BFE)  # str x30, [sp, #0x10]
    wemit(0xB94016A0)  # ldr w0, [x21, #0x14]
    wemit(0x52800101)  # movz w1, #8
    wemit(0xD2800002)  # movz x2, #0
    wemit(0x52800003)  # movz w3, #0
    wemit(0xD2800004)  # movz x4, #0
    wemit(0xD2800005)  # movz x5, #0
    wemit(0x910023E6)  # add x6, sp, #0x8  output scalars
    wemit(0x52800048)  # movz w8, #2
    wemit(0xB90003E8)  # str w8, [sp]  outputCnt
    wemit(0x910003E7)  # mov x7, sp
    wemit(enc_bl(wpc, CALLMETHOD))
    wemit(0xF9400BFE)  # ldr x30, [sp, #0x10]
    wemit(0x910083FF)  # add sp, sp, #0x20
    wemit(0xD65F03C0)  # ret
    wrap_blob = b"".join(struct.pack("<I", w) for w in wrap)
    fo_wrap = WRAP_SEL8 - SUBCACHE_BASE
    if buf[fo_wrap : fo_wrap + len(wrap_blob)] != wrap_blob:
        print(f"{WRAP_SEL8:#x}: write sel8 wrap ({len(wrap_blob)} bytes)")
        if write:
            buf[fo_wrap : fo_wrap + len(wrap_blob)] = wrap_blob
        changed = True
    else:
        print(f"{WRAP_SEL8:#x}: sel8 wrap already present")
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
