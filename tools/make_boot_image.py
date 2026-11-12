#!/usr/bin/env python3
"""Build a QEMU kernel image that boots past Image4 and mounts lapisfs.

The published qemu/Image maps each Mach-O segment from file offset F+0x175b0
onto the linked virtual address of F. That slide is required for the early
MMU-off handoff. This script leaves the slide alone and:

1. Patches trust_cache_runtime_init so Image4/AMFI NULL branches cannot panic.
2. Skips IOStartIOKit in bsd_init (waits forever without a full IOKit stack).
3. When vfs_mountroot sees a NULL vfstable mount slot, calls slid lapis_mount.
4. Keeps rd=md0 so the root-device waiter does not block; lapisfs mounts md0.
5. Fixes a refcount leak in the VM pageout-scan enter path: non-first CPUs
   used to increment the scan count and return without decrementing, so the
   lock at runtime 0xfffffe0007d604fc stayed set (count stuck at ncpus-1).
6. Breaks a stock `b .` hang at runtime 0xfffffe0007afdd90 (processor state 2)
   by falling through to the normal state-dispatch path.

After root mount the boot CPU can still stall in the pageout-scan wait at
0xfffffe0007576a58 if the scan owner never reaches the unlock store; launchd
is not claimed yet. SpringBoard is not claimed.

Trampolines sit in a NOP sled at runtime 0xfffffe000756ac30, not inside
ExceptionVectorsBase.

QEMU needs ~43G guest RAM (memory-backend reserve=off). 8G hits an early
stub exception.
"""

from __future__ import annotations

import hashlib
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "qemu" / "Image"
OUTPUT = ROOT / "qemu" / "Image.ipados"

TEXT_BASE = 0xFFFFFE0007004000
SLIDE = 0x175B0
CMDLINE_OFF = 0x17048
CMDLINE_LEN = 1024
EXTRA_ARGS = (
    "interrupt-masked-debug-mode=0 "
    "sched_preemption_disable_debug_mode=0 "
    "unentitled_ios_sim_launch=1 "
    "amfi_get_out_of_my_way=1"
)

NOP = 0xD503201F
MOVZ_X8_1 = 0xD2800028
MOVZ_X9_1 = 0xD2800029
MOVZ_W9_2 = 0x52800049
MOVZ_X8_0 = 0xD2800008
MOVZ_W0_0 = 0x52800000

PATCHES = (
    (0xFFFFFE0007930F14, 0xF9400108, MOVZ_X8_1),
    (0xFFFFFE0007930F18, 0xB40003A8, NOP),
    (0xFFFFFE0007930F24, 0xF9400129, MOVZ_X9_1),
    (0xFFFFFE0007930F28, 0xB4000349, NOP),
    (0xFFFFFE0007930F2C, 0xB9411929, MOVZ_W9_2),
    (0xFFFFFE0007930F34, 0x54000309, NOP),
    (0xFFFFFE0007930F48, 0xF940C908, MOVZ_X8_0),
)

LAPIS_MOUNT_RT = 0xFFFFFE000743D4E8
IOSTARTIOKIT_BL_RT = 0xFFFFFE00078A58F4
MOUNTROOT_CBZ = 0xFFFFFE0007606258
MOUNTROOT_CAVE = 0xFFFFFE00075821E0
MOUNT_SUCCESS = 0xFFFFFE00076062F4
MOUNT_FAIL = 0xFFFFFE0007606270
IOSTARTIOKIT_BL = IOSTARTIOKIT_BL_RT + SLIDE

SCAN_ENTER_LDR = 0xFFFFFE000758D948
SCAN_BUSY_RET_RT = 0xFFFFFE000757675C
PROC_STATE2_HANG = 0xFFFFFE0007B15340
PROC_STATE2_HANG_EXPECT = 0x14000000  # b .
PROC_STATE2_HANG_REPL = 0x14000003  # b .+12 -> dispatch


def _movz_x(rd: int, imm16: int, shift: int = 0) -> int:
    hw = {0: 0, 16: 1, 32: 2, 48: 3}[shift]
    return 0xD2800000 | (hw << 21) | ((imm16 & 0xFFFF) << 5) | rd


def _movk_x(rd: int, imm16: int, shift: int = 0) -> int:
    hw = {0: 0, 16: 1, 32: 2, 48: 3}[shift]
    return 0xF2800000 | (hw << 21) | ((imm16 & 0xFFFF) << 5) | rd


def _b(from_va: int, to_va: int, link: bool = False) -> int:
    off = (to_va - from_va) >> 2
    assert -(1 << 25) <= off < (1 << 25)
    return (0x94000000 if link else 0x14000000) | (off & 0x3FFFFFF)


def _cbz(rt: int, from_va: int, to_va: int, width64: bool = True) -> int:
    off = (to_va - from_va) >> 2
    assert -(1 << 18) <= off < (1 << 18)
    op = 0xB4000000 if width64 else 0x34000000
    return op | ((off & 0x7FFFF) << 5) | rt


def _cbnz(rt: int, from_va: int, to_va: int, width64: bool = False) -> int:
    off = (to_va - from_va) >> 2
    assert -(1 << 18) <= off < (1 << 18)
    op = 0xB5000000 if width64 else 0x35000000
    return op | ((off & 0x7FFFF) << 5) | rt


def va_off(va: int) -> int:
    return va - TEXT_BASE


def _write_seq(blob: bytearray, cave: int, seq: list[int], require_nop: bool = True) -> None:
    for i, w in enumerate(seq):
        off = va_off(cave + 4 * i)
        cur = struct.unpack_from("<I", blob, off)[0]
        if require_nop and cur != NOP:
            raise SystemExit(f"cave {cave + 4 * i:#x}: expected NOP, found {cur:#x}")
        struct.pack_into("<I", blob, off, w)


def patch_cmdline(blob: bytearray) -> None:
    field = blob[CMDLINE_OFF : CMDLINE_OFF + CMDLINE_LEN]
    end = field.find(b"\x00")
    if end < 0:
        raise SystemExit("boot command line has no NUL")
    current = field[:end].decode("ascii")
    if "rd=md0" not in current and "rd=" not in current:
        current = (current + " rd=md0").strip()
    extra = []
    for arg in EXTRA_ARGS.split():
        name = arg.split("=", 1)[0]
        if name + "=" not in current and name + " " not in current + " ":
            extra.append(arg)
    if extra:
        addition = (" " if current and not current.endswith(" ") else "") + " ".join(extra)
        current = current + addition
    raw = current.encode("ascii")
    if len(raw) + 1 > CMDLINE_LEN:
        raise SystemExit("boot command line does not fit in 1024 bytes")
    blob[CMDLINE_OFF : CMDLINE_OFF + CMDLINE_LEN] = raw + b"\x00" * (CMDLINE_LEN - len(raw))


def patch_checks(blob: bytearray) -> None:
    for va, expect, repl in PATCHES:
        off = va_off(va)
        word = struct.unpack_from("<I", blob, off)[0]
        if word != expect:
            raise SystemExit(f"{va:#x}: expected {expect:#x}, found {word:#x}")
        struct.pack_into("<I", blob, off, repl)


def patch_skip_iostartiokit(blob: bytearray) -> None:
    off = va_off(IOSTARTIOKIT_BL)
    cur = struct.unpack_from("<I", blob, off)[0]
    if (cur & 0xFC000000) != 0x94000000:
        raise SystemExit(f"{IOSTARTIOKIT_BL:#x}: expected bl, found {cur:#x}")
    struct.pack_into("<I", blob, off, MOVZ_W0_0)


def patch_lapisfs_mount(blob: bytearray) -> None:
    cave = MOUNTROOT_CAVE
    seq = [
        _movz_x(8, LAPIS_MOUNT_RT & 0xFFFF, 0),
        _movk_x(8, (LAPIS_MOUNT_RT >> 16) & 0xFFFF, 16),
        _movk_x(8, (LAPIS_MOUNT_RT >> 32) & 0xFFFF, 32),
        _movk_x(8, (LAPIS_MOUNT_RT >> 48) & 0xFFFF, 48),
        0xAA1C03E0,
        0xAA1303E2,
        0xD63F0100,
        0xAA0003F8,
        _cbz(0, cave + 4 * 8, MOUNT_SUCCESS, width64=False),
        _b(cave + 4 * 9, MOUNT_FAIL),
        NOP,
        NOP,
    ]
    _write_seq(blob, cave, seq)
    cbz_off = va_off(MOUNTROOT_CBZ)
    cur = struct.unpack_from("<I", blob, cbz_off)[0]
    if (cur & 0xFF00001F) != 0xB4000008:
        raise SystemExit(f"{MOUNTROOT_CBZ:#x}: expected cbz x8, found {cur:#x}")
    struct.pack_into("<I", blob, cbz_off, _cbz(8, MOUNTROOT_CBZ, cave))


def patch_scan_refcount(blob: bytearray) -> None:
    """Only the first CPU claims the pageout scan; non-first must not bump the count."""
    expect = (
        0xB9417AE8,
        0x11000508,
        0xB9017AE8,
        0xB9417AE8,
        0x7100051F,
        0x54001D81,
    )
    enter_rt = SCAN_ENTER_LDR - SLIDE
    busy_rt = SCAN_BUSY_RET_RT
    repl = (
        0xB9417AE8,
        _cbnz(8, enter_rt + 4, busy_rt, width64=False),
        0x52800028,
        0xB9017AE8,
        NOP,
        NOP,
    )
    for i, (exp, word) in enumerate(zip(expect, repl)):
        off = va_off(SCAN_ENTER_LDR + 4 * i)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != exp:
            raise SystemExit(f"{SCAN_ENTER_LDR + 4 * i:#x}: expected {exp:#x}, found {cur:#x}")
        struct.pack_into("<I", blob, off, word)


def patch_proc_state2_hang(blob: bytearray) -> None:
    off = va_off(PROC_STATE2_HANG)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != PROC_STATE2_HANG_EXPECT:
        raise SystemExit(f"{PROC_STATE2_HANG:#x}: expected {PROC_STATE2_HANG_EXPECT:#x}, found {cur:#x}")
    struct.pack_into("<I", blob, off, PROC_STATE2_HANG_REPL)


def build(source: pathlib.Path = SOURCE, output: pathlib.Path = OUTPUT) -> str:
    blob = bytearray(source.read_bytes())
    patch_cmdline(blob)
    patch_checks(blob)
    patch_skip_iostartiokit(blob)
    patch_lapisfs_mount(blob)
    patch_scan_refcount(blob)
    patch_proc_state2_hang(blob)
    output.write_bytes(blob)
    return hashlib.sha256(blob).hexdigest()


def main() -> None:
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        blob = bytearray(SOURCE.read_bytes())
        patch_cmdline(blob)
        patch_checks(blob)
        patch_skip_iostartiokit(blob)
        patch_lapisfs_mount(blob)
        patch_scan_refcount(blob)
        patch_proc_state2_hang(blob)
        print("check ok", len(blob))
        return
    digest = build()
    print(f"wrote {OUTPUT}")
    print(f"sha256 {digest}")
    print(f"bytes {OUTPUT.stat().st_size}")


if __name__ == "__main__":
    main()
