#!/usr/bin/env python3
"""Build a QEMU kernel image that boots past Image4 and mounts lapisfs.

The published qemu/Image maps each Mach-O segment from file offset F+0x175b0
onto the linked virtual address of F. That slide is required for the early
MMU-off handoff. This script leaves the slide alone and:

1. Patches trust_cache_runtime_init so Image4/AMFI NULL branches cannot panic.
2. Skips IOStartIOKit in bsd_init (waits forever without a full IOKit stack).
3. When vfs_mountroot sees a NULL vfstable mount slot, calls slid lapis_mount.
4. Keeps rd=md0 so the root-device waiter does not block; lapis_mount still
   prefers LapisBlk ROOT (fullroot/UFS) once published.
5. Before lapis_mount, kicks LapisBlk/UFS (stock starts them after
   mountroot, so a plain wait deadlocks). Then lapis_mount waits forever
   for LapisBlk status ROOT=1 instead of racing to md0.
6. Fixes pageout-scan enter: only the first CPU claims the scan count (stock
   leaked the count on non-first CPUs), and the wait-lock store that publishes
   0xfffffe0007d604fc is NOPed (avoids scan-wait / tickets AB-BA).
7. Raises slto_us/tlto_us to 3600s (stock 1–2s is too tight for TCG).
8. Ticket-lock timeout stubs at runtime 0xfffffe0007ba0590..0xfffffe0007ba0620
   return instead of calling panic (TCG still trips them; callers retry).
9. Guards NULL vtable loads at runtime 0xfffffe0007b49968 / 0xfffffe0007b499d0
   (ldr x8,[x8,#0x1c0] with x8==0 → FAR 0x1c0 panic after mount).
10. Guards NULL ops object at runtime 0xfffffe000793a084
   (ldr x8,[x9,#0x1a0] with x9==0 → FAR 0x1a0). The null path returns w0=0
   (success) so ubc_cs_blob_add / F_ADDFILESIGS for the dyld shared cache
   does not treat the leftover blob pointer as EPERM.
11. Breaks a stock `b .` hang at runtime 0xfffffe0007afdd90 (processor state 2)
    by falling through to the normal state-dispatch path.
12. Fixes cs_enforcement_disable / AMFI boot-arg ADD immediates (stock points
    mid-cstring), skips the CSM gate that blocked clearing cs_enforcement, and
    softens the invalid-page CS kill so init can survive under TCG.
13. Softens ubc_cs_blob_add's non-platform-binary EPERM gate (RT
    0xfffffe0007938ff0) so other F_ADDFILESIGS callers still succeed.
14. Converts arm64e authenticated rebase encodings in
    vm_shared_region_slide_page (formats 3/5): stock skips bit63 entries and
    leaves chain words like 0x86be… in DATA; QEMU then SIGSEGVs on them
    (OS_REASON_SIGNAL / 0xb). Format 5: 34-bit runtimeOffset + value_add +
    slide (no high8). Format 3: 32-bit offset + auth_value_add + slide.
    Pointers are left unsigned (kernel JOP key not wired under TCG); with
    pauth-impdef this clears the raw-encoding SEGV and advances to a later
    SIGBUS (0xa) in dyld.

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
# lapis_mount wait-for-LapisBlk-ROOT sites (file VA = RT + SLIDE).
# Stock: b.eq wait only if magic==LKBL; else md0. Outer wait uses movz
# w0,#0x1388 then falls back to md0 on state!=1 / null disk / tiny size.
LAPIS_WAIT_ENTRY = 0xFFFFFE000743D544 + SLIDE  # b.eq #743d590
LAPIS_WAIT_ENTRY_EXPECT = 0x54000260
LAPIS_WAIT_TARGET_RT = 0xFFFFFE000743D590
LAPIS_WAIT_TIMEOUT = 0xFFFFFE000743D594 + SLIDE  # movz w0,#0x1388
LAPIS_WAIT_TIMEOUT_EXPECT = 0x52827100
LAPIS_WAIT_TIMEOUT_REPL = 0x12800000  # movn w0,#0 → outer count ~forever
LAPIS_FALLBACK_BNE = 0xFFFFFE000743D5E0 + SLIDE  # b.ne #743d548 (md0)
LAPIS_FALLBACK_BNE_EXPECT = 0x54FFFB41
LAPIS_FALLBACK_CBZ = 0xFFFFFE000743D5E8 + SLIDE  # cbz x0, #743d548
LAPIS_FALLBACK_CBZ_EXPECT = 0xB4FFFB00
LAPIS_FALLBACK_BLS = 0xFFFFFE000743D5F8 + SLIDE  # b.ls #743d548
LAPIS_FALLBACK_BLS_EXPECT = 0x54FFFA89
# Tight inner spin (RT 0xfffffe000743d580) — replace with delay so
# LapisBlk/UFS kernel threads can run on the same CPU.
LAPIS_WAIT_SPIN = 0xFFFFFE000743D580 + SLIDE
LAPIS_WAIT_SPIN_EXPECT = (
    0xF9404FE1,  # ldr x1, [sp, #0x98]
    0x91000421,  # add x1, x1, #1
    0xF9004FE1,  # str x1, [sp, #0x98]
    0x14000007,  # b #743d5a8
)
LAPIS_DELAY_RT = 0xFFFFFE0007AA6FB4  # delay_ms(w0)
# Free NOPs after null-ops trampoline (RT 0xfffffe000756ac68).
# Kick subroutine then mount trampoline live here so they do not collide
# with the 4-insn null-ops guard at RT 0xfffffe000756ac58.
LAPIS_KICK_CAVE = 0xFFFFFE000756AC68 + SLIDE
LAPIS_MOUNT_CAVE = 0xFFFFFE000756ACA0 + SLIDE
LAPIS_STATUS_RT = 0xFFFFFE0007DAF000
LAPIS_EARLY_INIT_RT = 0xFFFFFE00075144F0
LAPIS_EARLY_INIT2_RT = 0xFFFFFE00075118AC
LAPIS_BLK_INIT_RT = 0xFFFFFE000750D5C0
LAPIS_GIC_START_RT = 0xFFFFFE000750E8A4
LAPIS_UFS_START_RT = 0xFFFFFE000750B89C
IOSTARTIOKIT_BL_RT = 0xFFFFFE00078A58F4
MOUNTROOT_CBZ = 0xFFFFFE0007606258
# Legacy short cave (RT 0xfffffe000756ac30); kept for the cbz landing
# bounce into LAPIS_MOUNT_CAVE so older docs still match the sled base.
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
# Free NOPs: 4 insns at RT 0xfffffe000756ac58 (before LAPIS_KICK at +0x10),
# then more after LAPIS_MOUNT_CAVE (14 insns from RT 0xfffffe000756aca0).
VTABLE_CAVE = 0xFFFFFE000756AC58 + SLIDE
NULL_OPS_ZERO_CAVE = 0xFFFFFE000756ACD8 + SLIDE  # after mount trampoline
# Auth-rebase convert caves for vm_shared_region_slide_page (after zero cave).
SLIDE_AUTH_V5_CAVE = 0xFFFFFE000756ACE0 + SLIDE
SLIDE_AUTH_V3_CAVE = 0xFFFFFE000756ACF8 + SLIDE
# ldr x8,[x9,#0x1a0] with x9==0 (file VA = RT+SLIDE).
NULL_OPS_LDR = 0xFFFFFE000793A084 + SLIDE
NULL_OPS_LDR_EXPECT = 0xF940D128
NULL_OPS_RESUME_RT = 0xFFFFFE000793A088  # existing cbz x8
NULL_OPS_RET_RT = 0xFFFFFE000793A0B0  # epilogue (success when w0==0)
# slide_page: tbnz x9,#0x3f skips authenticated chain entries (leave raw).
SLIDE_PAGE_V5_TBNZ = 0xFFFFFE00074E2988 + SLIDE  # tbnz x9,#0x3f,#2974
SLIDE_PAGE_V5_TBNZ_EXPECT = 0xB7FFFF69
SLIDE_PAGE_V5_PLAIN_RT = 0xFFFFFE00074E298C
SLIDE_PAGE_V5_NEXT_RT = 0xFFFFFE00074E2974
SLIDE_PAGE_V3_TBNZ = 0xFFFFFE00074E2A50 + SLIDE  # tbnz x9,#0x3f,#2a38
SLIDE_PAGE_V3_TBNZ_EXPECT = 0xB7FFFF49
SLIDE_PAGE_V3_PLAIN_RT = 0xFFFFFE00074E2A54
SLIDE_PAGE_V3_NEXT_RT = 0xFFFFFE00074E2A38
MOV_W10_W9 = 0x2A0903EA
AND_X10_X9_34 = 0x9240852A  # and x10, x9, #0x3ffffffff (v5 runtimeOffset)
LDR_X11_X20_16 = 0xF9400A8B  # ldr x11, [x20, #16] (auth_value_add / value_add)
ADD_X10_X10_X11 = 0x8B0B014A  # add x10, x10, x11
ADD_X10_X10_X8 = 0x8B08014A  # add x10, x10, x8
ADD_X10_X8_X10 = 0x8B0A010A
STR_X10_X19 = 0xF900026A
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

# ubc_cs_blob_add (RT 0xfffffe0007938e8c): after mac_vnode_check_signature
# succeeds, a non-platform blob with stack-flag bit0 set takes an unconditional
# EPERM path (tbz w24,#0,cont → else mov w22,#1). Dyld's F_ADDFILESIGS_RETURN
# for dyld_shared_cache_arm64e hits this on fullroot → "code signature
# registration for shared cache failed". Branch straight to the continue path.
UBC_CS_NONPLATFORM_TBZ = 0xFFFFFE0007938FF0 + SLIDE  # tbz w24,#0,#9020
UBC_CS_NONPLATFORM_TBZ_EXPECT = 0x36000198
UBC_CS_NONPLATFORM_CONT_RT = 0xFFFFFE0007939020

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


def _movn_x(rd: int, imm16: int, shift: int = 0) -> int:
    hw = {0: 0, 16: 1, 32: 2, 48: 3}[shift]
    return 0x92800000 | (hw << 21) | ((imm16 & 0xFFFF) << 5) | rd


def _b(from_va: int, to_va: int, link: bool = False) -> int:
    off = (to_va - from_va) >> 2
    assert -(1 << 25) <= off < (1 << 25)
    return (0x94000000 if link else 0x14000000) | (off & 0x3FFFFFF)


def _b_cond(from_va: int, to_va: int, cond: int) -> int:
    off = (to_va - from_va) >> 2
    assert -(1 << 18) <= off < (1 << 18)
    return 0x54000000 | ((off & 0x7FFFF) << 5) | (cond & 0xF)


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


def _tbz(rt: int, bit: int, from_va: int, to_va: int, nz: bool = False) -> int:
    off = (to_va - from_va) >> 2
    assert -(1 << 13) <= off < (1 << 13)
    assert 0 <= bit < 64
    b5 = (bit >> 5) & 1
    b40 = bit & 0x1F
    op = 0x37000000 if nz else 0x36000000
    return op | (b5 << 31) | (b40 << 19) | ((off & 0x3FFF) << 5) | rt


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


def patch_lapis_driver_kick(blob: bytearray) -> None:
    """Start LapisBlk/UFS before mountroot waits for ROOT.

    Stock Lapis platform init runs after vfs_mountroot returns. Waiting in
    lapis_mount for ROOT without kicking drivers deadlocks (status stays 0).
    If LapisBlk magic is already set, skip. Functions take no PE object.
    """
    cave_rt = LAPIS_KICK_CAVE - SLIDE
    # Indices: 0 stp, 1 mov, 2-4 status ptr, 5 ldr, 6 cbnz, 7-11 bls, 12 ldp, 13 ret
    epilogue_rt = cave_rt + 4 * 12
    seq = [
        0xA9BF7BFD,  # stp x29, x30, [sp, #-0x10]!
        0x910003FD,  # mov x29, sp
        _movn_x(8, 0xFFF, 0),
        _movk_x(8, 0x7DA, 16),
        _movk_x(8, 0xFE00, 32),
        0xB9400108,  # ldr w8, [x8]
        _cbnz(8, cave_rt + 4 * 6, epilogue_rt, width64=False),
        _b(cave_rt + 4 * 7, LAPIS_EARLY_INIT_RT, link=True),
        _b(cave_rt + 4 * 8, LAPIS_EARLY_INIT2_RT, link=True),
        _b(cave_rt + 4 * 9, LAPIS_BLK_INIT_RT, link=True),
        _b(cave_rt + 4 * 10, LAPIS_GIC_START_RT, link=True),
        _b(cave_rt + 4 * 11, LAPIS_UFS_START_RT, link=True),
        0xA8C17BFD,  # ldp x29, x30, [sp], #0x10
        0xD65F03C0,  # ret
    ]
    assert len(seq) == 14
    # Kick is 14 insns (ends at RT +0x38); mount cave sits at +0x38.
    assert (LAPIS_MOUNT_CAVE - LAPIS_KICK_CAVE) // 4 == 14
    _write_seq(blob, LAPIS_KICK_CAVE, seq)


def patch_lapisfs_mount(blob: bytearray) -> None:
    patch_lapis_driver_kick(blob)
    # Short bounce at the historical sled base → real trampoline after kick.
    bounce = [_b(MOUNTROOT_CAVE, LAPIS_MOUNT_CAVE)]
    _write_seq(blob, MOUNTROOT_CAVE, bounce)
    cave = LAPIS_MOUNT_CAVE
    seq = [
        0xA9BF7BFD,  # stp x29, x30, [sp, #-0x10]!
        0x910003FD,  # mov x29, sp
        _b(cave + 4 * 2, LAPIS_KICK_CAVE, link=True),
        _movz_x(8, LAPIS_MOUNT_RT & 0xFFFF, 0),
        _movk_x(8, (LAPIS_MOUNT_RT >> 16) & 0xFFFF, 16),
        _movk_x(8, (LAPIS_MOUNT_RT >> 32) & 0xFFFF, 32),
        _movk_x(8, (LAPIS_MOUNT_RT >> 48) & 0xFFFF, 48),
        0xAA1C03E0,  # mov x0, x28
        0xAA1303E2,  # mov x2, x19
        0xD63F0100,  # blr x8
        0xAA0003F8,  # mov x24, x0
        0xA8C17BFD,  # ldp x29, x30, [sp], #0x10
        _cbz(0, cave + 4 * 12, MOUNT_SUCCESS, width64=False),
        _b(cave + 4 * 13, MOUNT_FAIL),
    ]
    _write_seq(blob, cave, seq)
    cbz_off = va_off(MOUNTROOT_CBZ)
    cur = struct.unpack_from("<I", blob, cbz_off)[0]
    if (cur & 0xFF00001F) != 0xB4000008:
        raise SystemExit(f"{MOUNTROOT_CBZ:#x}: expected cbz x8, found {cur:#x}")
    struct.pack_into("<I", blob, cbz_off, _cbz(8, MOUNTROOT_CBZ, MOUNTROOT_CAVE))
    patch_lapis_mount_wait_root(blob)


def patch_lapis_mount_wait_root(blob: bytearray) -> None:
    """Wait forever for LapisBlk ROOT=1 instead of racing to md0.

    Under QEMU TCG, vfs_mountroot calls lapis_mount before LapisBlk publishes
    magic LKBL / state==1. Stock then mounts the tiny md0 ramdisk (no dyld
    cache / libSystem). Always enter the wait loop, stretch the outer
    timeout, and on state!=1 / missing disk / tiny size retry the wait
    rather than falling back to md0.
    """
    wait_rt = LAPIS_WAIT_TARGET_RT
    patches = (
        (
            LAPIS_WAIT_ENTRY,
            LAPIS_WAIT_ENTRY_EXPECT,
            _b(LAPIS_WAIT_ENTRY - SLIDE, wait_rt),
        ),
        (
            LAPIS_WAIT_TIMEOUT,
            LAPIS_WAIT_TIMEOUT_EXPECT,
            LAPIS_WAIT_TIMEOUT_REPL,
        ),
        (
            LAPIS_FALLBACK_BNE,
            LAPIS_FALLBACK_BNE_EXPECT,
            _b_cond(LAPIS_FALLBACK_BNE - SLIDE, wait_rt, 1),  # ne
        ),
        (
            LAPIS_FALLBACK_CBZ,
            LAPIS_FALLBACK_CBZ_EXPECT,
            _cbz(0, LAPIS_FALLBACK_CBZ - SLIDE, wait_rt, width64=True),
        ),
        (
            LAPIS_FALLBACK_BLS,
            LAPIS_FALLBACK_BLS_EXPECT,
            _b_cond(LAPIS_FALLBACK_BLS - SLIDE, wait_rt, 9),  # ls
        ),
    )
    for va, expect, repl in patches:
        off = va_off(va)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != expect:
            raise SystemExit(f"{va:#x}: expected {expect:#x}, found {cur:#x}")
        struct.pack_into("<I", blob, off, repl)

    # Replace the inner counter spin with delay(10ms) + recheck state so
    # kicked LapisBlk/UFS threads are scheduled instead of starving on CPU0.
    spin_rt = LAPIS_WAIT_SPIN - SLIDE
    spin_repl = (
        0x52800140,  # movz w0, #0xa
        _b(spin_rt + 4, LAPIS_DELAY_RT, link=True),
        _b(spin_rt + 8, 0xFFFFFE000743D59C),  # b wait_recheck
        NOP,
    )
    for i, (exp, word) in enumerate(zip(LAPIS_WAIT_SPIN_EXPECT, spin_repl)):
        off = va_off(LAPIS_WAIT_SPIN + 4 * i)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != exp:
            raise SystemExit(
                f"{LAPIS_WAIT_SPIN + 4 * i:#x}: expected {exp:#x}, found {cur:#x}"
            )
        struct.pack_into("<I", blob, off, word)


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

    off = va_off(UBC_CS_NONPLATFORM_TBZ)
    cur = struct.unpack_from("<I", blob, off)[0]
    if cur != UBC_CS_NONPLATFORM_TBZ_EXPECT:
        raise SystemExit(
            f"{UBC_CS_NONPLATFORM_TBZ:#x}: expected {UBC_CS_NONPLATFORM_TBZ_EXPECT:#x}, found {cur:#x}"
        )
    struct.pack_into(
        "<I",
        blob,
        off,
        _b(UBC_CS_NONPLATFORM_TBZ - SLIDE, UBC_CS_NONPLATFORM_CONT_RT),
    )


def patch_slide_auth_pointers(blob: bytearray) -> None:
    """Convert arm64e auth rebase chain words instead of skipping them.

    vm_shared_region_slide_page formats 3/5 do `tbnz x9,#0x3f,next` and leave
    authenticated encodings (bit63) untouched. On fullroot that surfaces as
    FAR=0x86be… (chain encoding) → EXC_BAD_ACCESS → initproc signal 0xb.

    dyld_cache_slide_pointer5 auth: runtimeOffset:34 + value_add + slide
    (x8 already holds value_add+slide on the v5 path; do not OR high8).
    dyld_cache_slide_pointer3 auth: offset32 + auth_value_add + slide
    (x8 is slide only; load auth_value_add from [x20,#16]).

    Values are stored unsigned: signing needs the shared-region JOP key, which
    TCG does not provide the same way as hardware.
    """
    v5_rt = SLIDE_AUTH_V5_CAVE - SLIDE
    v3_rt = SLIDE_AUTH_V3_CAVE - SLIDE
    # tbnz auth → convert (local); else long-b to stock plain path.
    v5_seq = [
        _tbz(9, 0x3F, v5_rt, v5_rt + 8, nz=True),
        _b(v5_rt + 4, SLIDE_PAGE_V5_PLAIN_RT),
        AND_X10_X9_34,
        ADD_X10_X8_X10,
        STR_X10_X19,
        _b(v5_rt + 20, SLIDE_PAGE_V5_NEXT_RT),
    ]
    v3_seq = [
        _tbz(9, 0x3F, v3_rt, v3_rt + 8, nz=True),
        _b(v3_rt + 4, SLIDE_PAGE_V3_PLAIN_RT),
        MOV_W10_W9,
        LDR_X11_X20_16,
        ADD_X10_X10_X11,
        ADD_X10_X10_X8,
        STR_X10_X19,
        _b(v3_rt + 28, SLIDE_PAGE_V3_NEXT_RT),
    ]
    assert len(v5_seq) == 6 and len(v3_seq) == 8
    _write_seq(blob, SLIDE_AUTH_V5_CAVE, v5_seq)
    _write_seq(blob, SLIDE_AUTH_V3_CAVE, v3_seq)

    for site, expect, cave in (
        (SLIDE_PAGE_V5_TBNZ, SLIDE_PAGE_V5_TBNZ_EXPECT, v5_rt),
        (SLIDE_PAGE_V3_TBNZ, SLIDE_PAGE_V3_TBNZ_EXPECT, v3_rt),
    ):
        off = va_off(site)
        cur = struct.unpack_from("<I", blob, off)[0]
        if cur != expect:
            raise SystemExit(f"{site:#x}: expected {expect:#x}, found {cur:#x}")
        struct.pack_into("<I", blob, off, _b(site - SLIDE, cave))


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
    zero_rt = NULL_OPS_ZERO_CAVE - SLIDE
    # x9==0 → zero cave (mov w0,#0; b epilogue). Must not enter the epilogue
    # with the blob pointer still in w0 (non-zero → EPERM in ubc_cs_blob_add).
    seq = [
        _cbz(9, ops_tramp_rt, zero_rt),
        NULL_OPS_LDR_EXPECT,
        _b(ops_tramp_rt + 8, NULL_OPS_RESUME_RT),
        NOP,  # unused (kick cave starts at +0x10)
    ]
    _write_seq(blob, VTABLE_CAVE, seq)
    _write_seq(
        blob,
        NULL_OPS_ZERO_CAVE,
        [MOVZ_W0_0, _b(zero_rt + 4, NULL_OPS_RET_RT)],
    )
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
    patch_slide_auth_pointers(blob)
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
        patch_slide_auth_pointers(blob)
        patch_proc_state2_hang(blob)
        print("check ok", len(blob))
        return
    digest = build()
    print(f"wrote {OUTPUT}")
    print(f"sha256 {digest}")
    print(f"bytes {OUTPUT.stat().st_size}")


if __name__ == "__main__":
    main()
