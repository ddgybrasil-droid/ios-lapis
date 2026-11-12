#!/usr/bin/env python3
"""Build a QEMU kernel image that boots past Image4 and mounts lapisfs.

The published qemu/Image maps each Mach-O segment from file offset F+0x175b0
onto the linked virtual address of F. That slide is required for the early
MMU-off handoff. This script leaves the slide alone and:

1. Patches trust_cache_runtime_init so Image4/AMFI NULL branches cannot panic.
2. Skips IOStartIOKit in bsd_init (waits forever without a full IOKit stack).
3. When vfs_mountroot sees a NULL vfstable mount slot, calls slid lapis_mount.
4. Keeps rd=md0 so the root-device waiter does not block; lapisfs mounts md0.
5. Fixes pageout-scan enter: only the first CPU claims the scan count (stock
   leaked the count on non-first CPUs), and the wait-lock store that publishes
   0xfffffe0007d604fc is NOPed (avoids scan-wait / tickets AB-BA).
6. Raises slto_us/tlto_us to 3600s (stock 1–2s is too tight for TCG).
7. Ticket-lock timeout stubs at runtime 0xfffffe0007ba0590..0xfffffe0007ba0620
   return instead of calling panic (TCG still trips them; callers retry).
8. Guards NULL vtable loads at runtime 0xfffffe0007b49968 / 0xfffffe0007b499d0
   (ldr x8,[x8,#0x1c0] with x8==0 → FAR 0x1c0 panic after mount).
9. Guards NULL ops object at runtime 0xfffffe000793a084
   (ldr x8,[x9,#0x1a0] with x9==0 → FAR 0x1a0).
10. Breaks a stock `b .` hang at runtime 0xfffffe0007afdd90 (processor state 2)
   by falling through to the normal state-dispatch path.
11. Fixes cs_enforcement_disable / AMFI boot-arg ADD immediates (stock points
    mid-cstring), skips the CSM gate that blocked clearing cs_enforcement, and
    softens the invalid-page CS kill so init can survive under TCG.

load_init_program lives at RT 0xfffffe00078d7b18 (prints
"attempting to load /sbin/launchd"). SpringBoard is not claimed.

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
    "amfi_get_out_of_my_way=1 "
    "amfi_allow_any_signature=1 "
    # Kernel parses this only when the ADD immediate points at the real
    # cstring (stock is off-by-8); patch_cs_enforcement_disable fixes that.
    "cs_enforcement_disable=1 "
    # Stock Image ships slto_us=2000000 tlto_us=1000000; QEMU TCG blows
    # those ticket/spin timeouts and panics before load_init.
    "slto_us=3600000000 "
    "tlto_us=3600000000"
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
SCAN_LOCK_STR = 0xFFFFFE000758D970  # runtime 0xfffffe00075763c0: str w8, [x9, #0x4fc]
SCAN_LOCK_STR_EXPECT = 0xB904FD28
# Ticket-lock timeout → panic stubs (runtime); file VA = RT + SLIDE.
TICKET_TIMEOUT_STUBS = (
    (0xFFFFFE0007BA0590 + SLIDE, 0xD100C3FF),
    (0xFFFFFE0007BA05C0 + SLIDE, 0xD100C3FF),
    (0xFFFFFE0007BA05F0 + SLIDE, 0xD100C3FF),
    (0xFFFFFE0007BA0620 + SLIDE, 0xD10143FF),
)
RET = 0xD65F03C0
# Free NOPs after lapis_mount trampoline (RT 0xfffffe000756ac58).
VTABLE_CAVE = 0xFFFFFE000756AC58 + SLIDE
# ldr x8,[x9,#0x1a0] with x9==0 (file VA = RT+SLIDE).
NULL_OPS_LDR = 0xFFFFFE000793A084 + SLIDE
NULL_OPS_LDR_EXPECT = 0xF940D128
NULL_OPS_RESUME_RT = 0xFFFFFE000793A088  # existing cbz x8
NULL_OPS_RET_RT = 0xFFFFFE000793A0B0  # mov w0,#1; epilogue
PROC_STATE2_HANG = 0xFFFFFE0007B15340
PROC_STATE2_HANG_EXPECT = 0x14000000  # b .
PROC_STATE2_HANG_REPL = 0x14000003  # b .+12 -> dispatch

# Stock ADD immediates for AMFI/CS boot-arg names are off (into the middle
# of the cstring). File VA = RT + SLIDE.
CS_ENFORCE_DISABLE_ADD = 0xFFFFFE0007BF4684 + SLIDE  # add x0, x0, #0xe67
CS_ENFORCE_DISABLE_ADD_EXPECT = 0x91399C00  # #0xe67
CS_ENFORCE_DISABLE_ADD_REPL = 0x91397C00  # #0xe5f → "cs_enforcement_disable"
# After parse, stock also requires a CSM-available check before clearing
# cs_enforcement; that check fails without the monitor → keep enforcement on.
CS_ENFORCE_CSM_CBZ = 0xFFFFFE0007BF46A4 + SLIDE  # cbz w0, skip_disable
CS_ENFORCE_CSM_CBZ_EXPECT = 0x340000C0
# Boot-arg parse for disable is only reachable via data/sysctl; stock
# __DATA initializes cs_enforcement to 1 and nothing clears it on this path.
CS_ENFORCEMENT_DATA = 0xFFFFFE0007250728 + SLIDE
CS_ENFORCEMENT_DATA_EXPECT = 1
# Soften invalid-page CS kill (RT 0xfffffe00074734a0): always take the
# non-kill path so init can survive unsigned/mis-hashed pages under TCG.
CS_INVALID_PAGE_CBZ = 0xFFFFFE00074734A0 + SLIDE  # cbz w8, soft_path
CS_INVALID_PAGE_CBZ_EXPECT = 0x34001308
CS_INVALID_PAGE_SOFT_RT = 0xFFFFFE0007473700
# Always take the clear path when the disable helper does run.
CS_ENFORCE_BOOTARG_CBZ = 0xFFFFFE0007BF4698 + SLIDE  # cbz w8, skip_disable
CS_ENFORCE_BOOTARG_CBZ_EXPECT = 0x34000128

# AMFI config PE_parse name ADDs (RT 0xfffffe000791a9d8..); stock off-by-N.
AMFI_BOOTARG_ADDS = (
    # (file_va, expect_add, repl_add)
    (0xFFFFFE000791A9D8 + SLIDE, 0x91069C00, 0x91069000),  # #0x1a7→#0x1a4 allow_any
    (0xFFFFFE000791A9EC + SLIDE, 0x9106B000, 0x9106F400),  # #0x1ac→#0x1bd unrestrict
    (0xFFFFFE000791AA00 + SLIDE, 0x91071400, 0x91076800),  # #0x1c5→#0x1da get_out
    (0xFFFFFE000791AA14 + SLIDE, 0x91078800, 0x9107C400),  # #0x1e2→#0x1f1 integrity_skip
    (0xFFFFFE000791AA28 + SLIDE, 0x91399C00, 0x91397C00),  # #0xe67→#0xe5f cs_enforcement_disable
    (0xFFFFFE000791AA3C + SLIDE, 0x9107E400, 0x91080C00),  # #0x1f9→#0x203 relax_profile
    (0xFFFFFE000791AA50 + SLIDE, 0x91082C00, 0x91087000),  # #0x20b→#0x21c dev_mode
)


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
    parts = current.split()
    overrides = {}
    for arg in EXTRA_ARGS.split():
        if "=" in arg:
            overrides[arg.split("=", 1)[0]] = arg
        else:
            overrides[arg] = arg
    kept = []
    seen = set()
    for part in parts:
        name = part.split("=", 1)[0]
        if name in overrides:
            kept.append(overrides[name])
            seen.add(name)
        else:
            kept.append(part)
    for name, arg in overrides.items():
        if name not in seen:
            kept.append(arg)
    current = " ".join(kept)
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
    """First CPU only claims the scan count; do not publish the wait lock.

    Stock enter increments a shared count on every CPU then returns on
    non-first, leaking the count so unlock never clears the wait lock.
    Only the first CPU may set the count to 1. Separately, the store that
    publishes lock 0xfffffe0007d604fc is NOPed: with the lock visible, the
    scan owner deadlocks on a tickets spinlock while other CPUs wait at
    0xfffffe0007576a58 holding that same class of lock. The scan body still
    runs and reaches the unlock store.
    """
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

    off = va_off(SCAN_LOCK_STR)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != SCAN_LOCK_STR_EXPECT:
        raise SystemExit(f"{SCAN_LOCK_STR:#x}: expected {SCAN_LOCK_STR_EXPECT:#x}, found {cur:#x}")
    struct.pack_into("<I", blob, off, NOP)


def patch_proc_state2_hang(blob: bytearray) -> None:
    off = va_off(PROC_STATE2_HANG)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != PROC_STATE2_HANG_EXPECT:
        raise SystemExit(f"{PROC_STATE2_HANG:#x}: expected {PROC_STATE2_HANG_EXPECT:#x}, found {cur:#x}")
    struct.pack_into("<I", blob, off, PROC_STATE2_HANG_REPL)


def patch_ticket_timeout_return(blob: bytearray) -> None:
    """Ticket-lock timeout helpers call panic(); make them RET so waiters retry."""
    for va, expect in TICKET_TIMEOUT_STUBS:
        off = va_off(va)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != expect:
            raise SystemExit(f"{va:#x}: expected {expect:#x}, found {cur:#x}")
        struct.pack_into("<I", blob, off, RET)


def patch_cs_enforcement_disable(blob: bytearray) -> None:
    """Make cs_enforcement_disable=1 actually clear enforcement.

    Stock ADD immediates point into the middle of the boot-arg cstrings, so
    PE_parse_boot_argn never matches. Even with a correct name, a CSM
    availability check then skips the stores that clear cs_enforcement
    (RT 0xfffffe0007250728). Fix the name ADDs, skip the CSM gate, and
    soften the invalid-page CS kill used after launchd pages fault under TCG.
    """
    off = va_off(CS_ENFORCE_DISABLE_ADD)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != CS_ENFORCE_DISABLE_ADD_EXPECT:
        raise SystemExit(
            f"{CS_ENFORCE_DISABLE_ADD:#x}: expected {CS_ENFORCE_DISABLE_ADD_EXPECT:#x}, found {cur:#x}"
        )
    struct.pack_into("<I", blob, off, CS_ENFORCE_DISABLE_ADD_REPL)

    off = va_off(CS_ENFORCE_CSM_CBZ)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != CS_ENFORCE_CSM_CBZ_EXPECT:
        raise SystemExit(
            f"{CS_ENFORCE_CSM_CBZ:#x}: expected {CS_ENFORCE_CSM_CBZ_EXPECT:#x}, found {cur:#x}"
        )
    struct.pack_into("<I", blob, off, NOP)

    off = va_off(CS_ENFORCE_BOOTARG_CBZ)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != CS_ENFORCE_BOOTARG_CBZ_EXPECT:
        raise SystemExit(
            f"{CS_ENFORCE_BOOTARG_CBZ:#x}: expected {CS_ENFORCE_BOOTARG_CBZ_EXPECT:#x}, found {cur:#x}"
        )
    struct.pack_into("<I", blob, off, NOP)

    off = va_off(CS_ENFORCEMENT_DATA)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != CS_ENFORCEMENT_DATA_EXPECT:
        raise SystemExit(
            f"{CS_ENFORCEMENT_DATA:#x}: expected {CS_ENFORCEMENT_DATA_EXPECT}, found {cur}"
        )
    struct.pack_into("<I", blob, off, 0)

    for va, expect, repl in AMFI_BOOTARG_ADDS:
        off = va_off(va)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != expect:
            raise SystemExit(f"{va:#x}: expected {expect:#x}, found {cur:#x}")
        struct.pack_into("<I", blob, off, repl)

    off = va_off(CS_INVALID_PAGE_CBZ)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != CS_INVALID_PAGE_CBZ_EXPECT:
        raise SystemExit(
            f"{CS_INVALID_PAGE_CBZ:#x}: expected {CS_INVALID_PAGE_CBZ_EXPECT:#x}, found {cur:#x}"
        )
    # Unconditional branch to the soft/non-kill path.
    struct.pack_into(
        "<I",
        blob,
        off,
        _b(CS_INVALID_PAGE_CBZ - SLIDE, CS_INVALID_PAGE_SOFT_RT),
    )


def patch_null_vtable_guard(blob: bytearray) -> None:
    """Neutralize NULL vtable/ops loads that panic after mount (skipped IOKit).

    In RT 0xfffffe0007b49870..0xfffffe0007b49d00, stock does
    `ldr x8,[x8,#imm]; …; blr x8` with x8==NULL → FAR imm. NOP the ldr and
    replace blr with `mov w0,#0`. Also guard RT 0xfffffe000793a084 via cave.
    """
    blr_x8 = 0xD63F0100
    # (runtime ldr site, expected ldr encoding)
    bulk = (
        (0xFFFFFE0007B49878, 0xF940E908),
        (0xFFFFFE0007B498F8, 0xF940E908),
        (0xFFFFFE0007B49968, 0xF940E108),
        (0xFFFFFE0007B499D0, 0xF940E108),
        (0xFFFFFE0007B49A70, 0xF940F108),
        (0xFFFFFE0007B49B80, 0xF940F108),
        (0xFFFFFE0007B49CC8, 0xF940E108),
        (0xFFFFFE0007B49CE4, 0xF940F108),
    )
    for rt, expect in bulk:
        file_va = rt + SLIDE
        off = va_off(file_va)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != expect:
            raise SystemExit(f"{file_va:#x}: expected {expect:#x}, found {cur:#x}")
        struct.pack_into("<I", blob, off, NOP)
        patched_blr = False
        for j in range(1, 6):
            off_b = va_off(file_va + 4 * j)
            wb = struct.unpack_from("<I", blob, off_b)[0]
            if wb == blr_x8:
                # Non-zero so local `cbz w0, assert_path` falls through.
                struct.pack_into("<I", blob, off_b, 0x52800020)  # mov w0, #1
                patched_blr = True
                break
        if not patched_blr:
            raise SystemExit(f"{file_va:#x}: no blr x8 within 5 insns")

    cave_rt = VTABLE_CAVE - SLIDE
    ops_tramp_rt = cave_rt
    seq = [
        _cbz(9, ops_tramp_rt, ops_tramp_rt + 12),
        NULL_OPS_LDR_EXPECT,
        _b(ops_tramp_rt + 8, NULL_OPS_RESUME_RT),
        _b(ops_tramp_rt + 12, NULL_OPS_RET_RT),
    ]
    _write_seq(blob, VTABLE_CAVE, seq)
    off = va_off(NULL_OPS_LDR)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != NULL_OPS_LDR_EXPECT:
        raise SystemExit(f"{NULL_OPS_LDR:#x}: expected {NULL_OPS_LDR_EXPECT:#x}, found {cur:#x}")
    struct.pack_into("<I", blob, off, _b(NULL_OPS_LDR - SLIDE, ops_tramp_rt))


def build(source: pathlib.Path = SOURCE, output: pathlib.Path = OUTPUT) -> str:
    blob = bytearray(source.read_bytes())
    patch_cmdline(blob)
    patch_checks(blob)
    patch_skip_iostartiokit(blob)
    patch_lapisfs_mount(blob)
    patch_scan_refcount(blob)
    patch_ticket_timeout_return(blob)
    patch_null_vtable_guard(blob)
    patch_cs_enforcement_disable(blob)
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
        patch_ticket_timeout_return(blob)
        patch_null_vtable_guard(blob)
        patch_cs_enforcement_disable(blob)
        patch_proc_state2_hang(blob)
        print("check ok", len(blob))
        return
    digest = build()
    print(f"wrote {OUTPUT}")
    print(f"sha256 {digest}")
    print(f"bytes {OUTPUT.stat().st_size}")


if __name__ == "__main__":
    main()
