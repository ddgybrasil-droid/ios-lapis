#!/usr/bin/env python3
"""Stub _container_system_path_for_identifier in DSC subcache .31.

lsd aborts in -[_LSDefaults systemContainerBaseURL] when
container_system_path_for_identifier → __container_legacy_lookup(class=12)
returns NULL (MCM cannot create systemData containers without APFS/AKS
userclient). Same bring-up class as patch_fullroot_usermanagerd_ephemeral.py:
replace only this function with a small stub that strndup()'s a fixed path.

Unslid: 0x1e276a4e8..0x1e276a5f4 in dyld_shared_cache_arm64e.31
Path returned: /private/var/containers/Data/System/com.apple.lsd

Usage:
  python3 tools/patch_dsc_container_syspath.py /path/to/dyld_shared_cache_arm64e.31 [--write]
Then re-run dsc_cd_rehash on the image (or the subcache in-place).
"""
from __future__ import annotations

import argparse
import struct
import sys

FUNC_VA = 0x1E276A4E8
FUNC_END = 0x1E276A5F4
STRNDUP_STUB = 0x1E5433140
PATH = b"/private/var/containers/Data/System/com.apple.lsd\x00"
# Path placed at end of function window
PATH_OFF = 0xC0  # within function; VA = FUNC_VA + PATH_OFF


def enc_adrp(rd: int, pc: int, target: int) -> int:
    page = target & ~0xFFF
    pc_page = pc & ~0xFFF
    imm = (page - pc_page) >> 12
    imm = imm & 0x1FFFFF
    immlo = imm & 0x3
    immhi = imm >> 2
    return 0x90000000 | (immhi << 5) | (rd) | (immlo << 29)


def enc_add_imm(rd: int, rn: int, imm12: int) -> int:
    return 0x91000000 | ((imm12 & 0xFFF) << 10) | (rn << 5) | rd


def enc_movz_w(rd: int, imm16: int) -> int:
    return 0x52800000 | ((imm16 & 0xFFFF) << 5) | rd


def enc_bl(pc: int, target: int) -> int:
    imm = (target - pc) >> 2
    return 0x94000000 | (imm & 0x3FFFFFF)


def enc_stp_pre(rt: int, rt2: int, rn: int, imm: int) -> int:
    # stp rt, rt2, [rn, #imm]!  (signed offset /8 for 64-bit)
    assert imm % 8 == 0
    imm7 = (imm // 8) & 0x7F
    return 0xA9800000 | (imm7 << 15) | (rt2 << 10) | (rn << 5) | rt


def enc_ldp_post(rt: int, rt2: int, rn: int, imm: int) -> int:
    assert imm % 8 == 0
    imm7 = (imm // 8) & 0x7F
    return 0xA8C00000 | (imm7 << 15) | (rt2 << 10) | (rn << 5) | rt


def build_stub() -> bytes:
    """PAC-aware stub: strndup(PATH, 0x100); return."""
    out = bytearray()
    va = FUNC_VA

    def emit(w: int):
        nonlocal va
        out.extend(struct.pack("<I", w))
        va += 4

    # pacibsp
    emit(0xD503237F)
    # stp x29, x30, [sp, #-0x20]!
    emit(enc_stp_pre(29, 30, 31, -0x20))
    # mov x29, sp
    emit(0x910003FD)
    # adrp x0, path_page; add x0, x0, #off
    path_va = FUNC_VA + PATH_OFF
    emit(enc_adrp(0, va, path_va))
    emit(enc_add_imm(0, 0, path_va & 0xFFF))
    # mov w1, #0x100
    emit(enc_movz_w(1, 0x100))
    # bl strndup_stub
    emit(enc_bl(va, STRNDUP_STUB))
    # ldp x29, x30, [sp], #0x20
    emit(enc_ldp_post(29, 30, 31, 0x20))
    # retab
    emit(0xD65F0FFF)

    stub_len = len(out)
    assert stub_len <= PATH_OFF, f"stub {stub_len} overlaps path at {PATH_OFF}"
    # Fill with BRK until path
    while len(out) < PATH_OFF:
        out.extend(struct.pack("<I", 0xD4200000))  # brk #0
    out.extend(PATH)
    # Pad to original function size with brk
    while len(out) < (FUNC_END - FUNC_VA):
        out.extend(struct.pack("<I", 0xD4200000))
    out = out[: FUNC_END - FUNC_VA]
    return bytes(out)


def find_file_offset(path: str) -> int:
    """Map FUNC_VA → file offset using the subcache header mapping."""
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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("subcache31")
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    fo = find_file_offset(args.subcache31)
    stub = build_stub()
    print(f"FUNC {FUNC_VA:#x}..{FUNC_END:#x} fileoff {fo:#x} stub {len(stub)} bytes")
    print(f"path @ {FUNC_VA + PATH_OFF:#x}: {PATH[:-1]!r}")
    with open(args.subcache31, "rb") as f:
        f.seek(fo)
        old = f.read(len(stub))
    if old == stub:
        print("already patched")
        return
    print(f"old head: {old[:16].hex()}")
    print(f"new head: {stub[:16].hex()}")
    if not args.write:
        print("dry-run (pass --write)")
        return
    with open(args.subcache31, "r+b") as f:
        f.seek(fo)
        f.write(stub)
    print("wrote stub")


if __name__ == "__main__":
    main()
