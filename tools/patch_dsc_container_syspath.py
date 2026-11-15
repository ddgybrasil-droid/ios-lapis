#!/usr/bin/env python3
"""Stub _container_system_path_for_identifier in DSC subcache .31.

lsd / backboardd abort when container_system_path_for_identifier →
__container_legacy_lookup(class=12) returns NULL (MCM cannot create
systemData containers without APFS/AKS userclient).

Replace the function with a PAC-stripped stub that returns
  strdup("/private/var/containers/Data/System/" + identifier)
via strlcat into a stack buffer + strndup. Seeds must exist on disk
(see apply_lsd_seeds.sh).

Unslid: 0x1e276a4e8..0x1e276a5f4 in dyld_shared_cache_arm64e.31
strndup stub: 0x1e5433140  (libsystem_c)

IMPORTANT: this edits TEXT in .31. After any full .31 replace you MUST also
re-apply patch_fullroot_container_init.py + patch_fullroot_pac_strip.py
--nop-brk1, then dsc_cd_rehash.py --write. Otherwise launchd dies in
_container_init (boot69 regression).

Usage:
  python3 tools/patch_dsc_container_syspath.py /path/to/dyld_shared_cache_arm64e.31 [--write]
  # or patch in-image via --image /tmp/fullroot-mnt2.img
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

FUNC_VA = 0x1E276A4E8
FUNC_END = 0x1E276A5F4
STRNDUP_STUB = 0x1E5433140
# Prefix only; identifier from x0 is appended at runtime.
PREFIX = b"/private/var/containers/Data/System/"
PREFIX_OFF = 0xA0  # within function window
BUF_SIZE = 0x100


def enc_adrp(rd: int, pc: int, target: int) -> int:
    page = target & ~0xFFF
    pc_page = pc & ~0xFFF
    imm = ((page - pc_page) >> 12) & 0x1FFFFF
    immlo = imm & 0x3
    immhi = imm >> 2
    return 0x90000000 | (immhi << 5) | rd | (immlo << 29)


def enc_add_imm(rd: int, rn: int, imm12: int) -> int:
    return 0x91000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd


def enc_movz_w(rd: int, imm16: int) -> int:
    return 0x52800000 | ((imm16 & 0xFFFF) << 5) | rd


def enc_movz_x(rd: int, imm16: int) -> int:
    return 0xD2800000 | ((imm16 & 0xFFFF) << 5) | rd


def enc_bl(pc: int, target: int) -> int:
    imm = (target - pc) >> 2
    return 0x94000000 | (imm & 0x3FFFFFF)


def enc_stp_pre(rt: int, rt2: int, rn: int, imm: int) -> int:
    assert imm % 8 == 0
    imm7 = (imm // 8) & 0x7F
    return 0xA9800000 | (imm7 << 15) | (rt2 << 10) | (rn << 5) | rt


def enc_ldp_post(rt: int, rt2: int, rn: int, imm: int) -> int:
    assert imm % 8 == 0
    imm7 = (imm // 8) & 0x7F
    return 0xA8C00000 | (imm7 << 15) | (rt2 << 10) | (rn << 5) | rt


def enc_strb_imm(rt: int, rn: int, imm12: int) -> int:
    return 0x39000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rt


def enc_orr_x(rd: int, rn: int, rm: int) -> int:
    """mov xd, xm  when rn=xzr (31): ORR Xd, XZR, Xm"""
    return 0xAA000000 | (rm << 16) | (rn << 5) | rd


def build_stub() -> bytes:
    """PAC-stripped: strdup(prefix + identifier).

    Frame (0x120 bytes):
      [sp+0x00] x29,x30
      [sp+0x10] x19 (saved id), x20
      [sp+0x20] buf[0x100]
    """
    out = bytearray()
    va = FUNC_VA

    def emit(w: int) -> None:
        nonlocal va
        out.extend(struct.pack("<I", w))
        va += 4

    # NOP (was pacibsp); keep slot for pac_strip idempotency
    emit(0xD503201F)
    # stp x29, x30, [sp, #-0x120]!
    emit(enc_stp_pre(29, 30, 31, -0x120))
    # mov x29, sp
    emit(0x910003FD)
    # stp x19, x20, [sp, #0x10]
    emit(0xA90153F3)  # stp x19, x20, [sp, #0x10]
    # mov x19, x0  (identifier)
    emit(enc_orr_x(19, 31, 0))
    # null id → return NULL
    # cbz x19, ret_null
    cbz_off = len(out)
    emit(0xB4000000 | 19)  # placeholder; fix imm later
    # zero buf[0]
    emit(enc_strb_imm(31, 31, 0x20))  # strb wzr, [sp, #0x20]
    # memcpy-ish: copy prefix into buf via byte loop is heavy;
    # use simple: adrp/add src; then call __strcpy_chk or manual.
    # Manual unrolled copy of PREFIX (36 bytes) via LDR/STR pairs.
    prefix_va = FUNC_VA + PREFIX_OFF
    emit(enc_adrp(0, va, prefix_va))
    emit(enc_add_imm(0, 0, prefix_va & 0xFFF))  # x0 = prefix
    emit(0x910083E1)  # add x1, sp, #0x20  → dest buf
    # strcpy: keep it tiny — ldp/stp 32B + ldrb/strb tail
    emit(0xA9400C02)  # ldp x2, x3, [x0]
    emit(0xA9000C22)  # stp x2, x3, [x1]
    emit(0xA9410C02)  # ldp x2, x3, [x0, #0x10]
    emit(0xA9010C22)  # stp x2, x3, [x1, #0x10]
    emit(0xF9401002)  # ldr x2, [x0, #0x20]
    emit(0xF9001022)  # str x2, [x1, #0x20]
    # append identifier: find end (PREFIX len=36=0x24)
    emit(0x91009021)  # add x1, x1, #0x24  → end of prefix in buf
    # strcpy identifier
    emit(enc_orr_x(0, 31, 19))  # mov x0, x19
    # byte copy loop:
    # 1: ldrb w2, [x0], #1
    #    strb w2, [x1], #1
    #    cbnz w2, 1b
    loop_va = va
    emit(0x38401402)  # ldrb w2, [x0], #1
    emit(0x38001422)  # strb w2, [x1], #1
    # cbnz w2, loop — imm19 relative to this insn
    emit(0x35000000 | 2)  # placeholder
    pc_cbnz = va - 4
    imm19 = (loop_va - pc_cbnz) >> 2
    out[-4:] = struct.pack(
        "<I", 0x35000000 | ((imm19 & 0x7FFFF) << 5) | 2
    )

    # strndup(buf, 0x100)
    emit(0x910083E0)  # add x0, sp, #0x20
    emit(enc_movz_w(1, BUF_SIZE))
    emit(enc_bl(va, STRNDUP_STUB))
    # epilogue
    emit(0xA94153F3)  # ldp x19, x20, [sp, #0x10]
    emit(enc_ldp_post(29, 30, 31, 0x120))
    emit(0xD65F03C0)  # ret

    ret_null_va = va
    # fix cbz to land here
    pc_cbz = FUNC_VA + cbz_off
    imm19 = (ret_null_va - pc_cbz) >> 2
    struct.pack_into("<I", out, cbz_off, 0xB4000000 | ((imm19 & 0x7FFFF) << 5) | 19)
    # ret_null: mov x0,#0; restore; ret
    emit(enc_movz_x(0, 0))
    emit(0xA94153F3)  # ldp x19, x20, [sp, #0x10]
    emit(enc_ldp_post(29, 30, 31, 0x120))
    emit(0xD65F03C0)  # ret

    stub_len = len(out)
    assert stub_len <= PREFIX_OFF, f"stub {stub_len} overlaps prefix at {PREFIX_OFF}"
    while len(out) < PREFIX_OFF:
        out.extend(struct.pack("<I", 0xD503201F))  # nop padding
    out.extend(PREFIX + b"\x00")
    while len(out) < (FUNC_END - FUNC_VA):
        out.extend(struct.pack("<I", 0xD503201F))
    return bytes(out[: FUNC_END - FUNC_VA])


def find_file_offset(path: str) -> int:
    with open(path, "rb") as f:
        mag = f.read(16)
        if not mag.startswith(b"dyld_v1"):
            raise SystemExit(f"not a dyld cache: {path}")
        f.seek(0x10)
        off, cnt = struct.unpack("<II", f.read(8))
        f.seek(off)
        for _ in range(cnt):
            addr, size, fileoff, maxp, initp = struct.unpack("<QQQII", f.read(32))
            if addr <= FUNC_VA < addr + size:
                return fileoff + (FUNC_VA - addr)
    raise SystemExit(f"VA {FUNC_VA:#x} not in {path}")


def patch_file(subcache31: Path, *, write: bool) -> None:
    fo = find_file_offset(str(subcache31))
    stub = build_stub()
    print(f"FUNC {FUNC_VA:#x}..{FUNC_END:#x} fileoff {fo:#x} stub {len(stub)} bytes")
    print(f"prefix @ {FUNC_VA + PREFIX_OFF:#x}: {PREFIX!r}")
    with subcache31.open("rb") as f:
        f.seek(fo)
        old = f.read(len(stub))
    if old == stub:
        print("already patched")
        return
    print(f"old head: {old[:16].hex()}")
    print(f"new head: {stub[:16].hex()}")
    if not write:
        print("dry-run (pass --write)")
        return
    with subcache31.open("r+b") as f:
        f.seek(fo)
        f.write(stub)
    print("wrote stub")


def patch_image(img: Path, *, write: bool) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parents[0]))
    from lapisfs_names import gpt_partitions, lapisfs_paths

    stub = build_stub()
    fo = FUNC_VA - 0x1E101C000
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
            if not pl.endswith("dyld_shared_cache_arm64e.31"):
                continue
            if off in seen:
                continue
            seen.add(off)
            abs_off = root_off + off + fo
            f.seek(abs_off)
            old = f.read(len(stub))
            if old == stub:
                print(f"{pl}: already patched")
            else:
                print(f"{pl}: patch @ {abs_off:#x}")
                if write:
                    f.seek(abs_off)
                    f.write(stub)
            n += 1
        if not n:
            raise SystemExit("no .31 extent")
        print(f"{'wrote' if write else 'would write'} {n} extent(s)")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("subcache31", nargs="?", type=Path)
    ap.add_argument("--image", type=Path, help="patch .31 inside LAPISFS GPT image")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    if args.image:
        patch_image(args.image, write=args.write)
    elif args.subcache31:
        patch_file(args.subcache31, write=args.write)
    else:
        ap.error("pass subcache31 or --image")


if __name__ == "__main__":
    main()
