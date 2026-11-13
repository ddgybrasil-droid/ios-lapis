#!/usr/bin/env python3
"""The boot image keeps the published kernel and only applies the labeled bypass."""

import hashlib
import pathlib
import struct
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_boot_image as boot  # noqa: E402

SOURCE_SHA = "52ba8b831abdf7b3436f0ed3e1934383bcb41e0f6f41603eaa70523d85364202"


def test_source_untouched_and_patches_apply():
    raw = (ROOT / "qemu" / "Image").read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SOURCE_SHA
    blob = bytearray(raw)
    boot.patch_cmdline(blob)
    boot.patch_checks(blob)
    boot.patch_skip_iostartiokit(blob)
    boot.patch_lapisfs_mount(blob)
    boot.patch_scan_refcount(blob)
    boot.patch_ticket_timeout_return(blob)
    boot.patch_null_vtable_guard(blob)
    boot.patch_cs_enforcement_disable(blob)
    boot.patch_slide_auth_pointers(blob)
    boot.patch_proc_state2_hang(blob)
    cmd = blob[boot.CMDLINE_OFF : boot.CMDLINE_OFF + boot.CMDLINE_LEN]
    text = cmd.split(b"\x00", 1)[0].decode("ascii")
    assert "rd=md0" in text
    assert "interrupt-masked-debug-mode=0" in text
    assert "amfi_get_out_of_my_way=1" in text
    assert "amfi_allow_any_signature=1" in text
    assert "cs_enforcement_disable=1" in text
    assert "slto_us=3600000000" in text
    assert "tlto_us=3600000000" in text
    assert "slto_us=2000000" not in text
    assert "tlto_us=1000000" not in text
    assert cmd[len(text) + 1 :].strip(b"\x00") == b""
    for va, _expect, repl in boot.PATCHES:
        word = struct.unpack_from("<I", blob, boot.va_off(va))[0]
        assert word == repl
    iokit = struct.unpack_from("<I", blob, boot.va_off(boot.IOSTARTIOKIT_BL))[0]
    assert iokit == boot.MOVZ_W0_0
    cbz = struct.unpack_from("<I", blob, boot.va_off(boot.MOUNTROOT_CBZ))[0]
    assert cbz == boot._cbz(8, boot.MOUNTROOT_CBZ, boot.MOUNTROOT_CAVE)
    assert struct.unpack_from("<I", blob, boot.va_off(boot.MOUNTROOT_CAVE))[0] == boot._b(
        boot.MOUNTROOT_CAVE, boot.LAPIS_MOUNT_CAVE
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_MOUNT_CAVE + 12))[0] == boot._movz_x(
        8, boot.LAPIS_MOUNT_RT & 0xFFFF, 0
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_KICK_CAVE))[0] == 0xA9BF7BFD
    cave_rt = boot.LAPIS_MOUNT_CAVE - boot.SLIDE
    assert cave_rt >= 0xFFFFFE000756AC00
    enter_rt = boot.SCAN_ENTER_LDR - boot.SLIDE
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SCAN_ENTER_LDR + 4))[0] == boot._cbnz(
        8, enter_rt + 4, boot.SCAN_BUSY_RET_RT, width64=False
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SCAN_ENTER_LDR + 8))[0] == 0x52800028
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SCAN_LOCK_STR))[0] == boot.NOP
    for va, _expect in boot.TICKET_TIMEOUT_STUBS:
        assert struct.unpack_from("<I", blob, boot.va_off(va))[0] == boot.RET
    assert struct.unpack_from("<I", blob, boot.va_off(0xFFFFFE0007B49968 + boot.SLIDE))[0] == boot.NOP
    assert struct.unpack_from("<I", blob, boot.va_off(0xFFFFFE0007B49B80 + boot.SLIDE))[0] == boot.NOP
    assert struct.unpack_from("<I", blob, boot.va_off(boot.NULL_OPS_LDR))[0] != boot.NULL_OPS_LDR_EXPECT
    assert struct.unpack_from("<I", blob, boot.va_off(boot.VTABLE_CAVE))[0] != boot.NOP
    assert struct.unpack_from("<I", blob, boot.va_off(boot.NULL_OPS_ZERO_CAVE))[0] == boot.MOVZ_W0_0
    assert struct.unpack_from("<I", blob, boot.va_off(boot.NULL_OPS_ZERO_CAVE + 4))[0] == boot._b(
        boot.NULL_OPS_ZERO_CAVE - boot.SLIDE + 4, boot.NULL_OPS_RET_RT
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.PROC_STATE2_HANG))[0] == boot.PROC_STATE2_HANG_REPL
    assert struct.unpack_from("<I", blob, boot.va_off(boot.CS_ENFORCE_DISABLE_ADD))[0] == boot.CS_ENFORCE_DISABLE_ADD_REPL
    assert struct.unpack_from("<I", blob, boot.va_off(boot.CS_ENFORCE_CSM_CBZ))[0] == boot.NOP
    assert struct.unpack_from("<I", blob, boot.va_off(boot.CS_ENFORCE_BOOTARG_CBZ))[0] == boot.NOP
    assert struct.unpack_from("<I", blob, boot.va_off(boot.CS_ENFORCEMENT_DATA))[0] == 0
    assert struct.unpack_from("<I", blob, boot.va_off(boot.CS_INVALID_PAGE_CBZ))[0] == boot._b(
        boot.CS_INVALID_PAGE_CBZ - boot.SLIDE, boot.CS_INVALID_PAGE_SOFT_RT
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.UBC_CS_NONPLATFORM_TBZ))[0] == boot._b(
        boot.UBC_CS_NONPLATFORM_TBZ - boot.SLIDE, boot.UBC_CS_NONPLATFORM_CONT_RT
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_PAGE_V5_TBNZ))[0] == boot._b(
        boot.SLIDE_PAGE_V5_TBNZ - boot.SLIDE, boot.SLIDE_AUTH_V5_CAVE - boot.SLIDE
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_PAGE_V3_TBNZ))[0] == boot._b(
        boot.SLIDE_PAGE_V3_TBNZ - boot.SLIDE, boot.SLIDE_AUTH_V3_CAVE - boot.SLIDE
    )
    v5_rt = boot.SLIDE_AUTH_V5_CAVE - boot.SLIDE
    v3_rt = boot.SLIDE_AUTH_V3_CAVE - boot.SLIDE
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_AUTH_V5_CAVE))[0] == boot._tbz(
        9, 0x3F, v5_rt, v5_rt + 8, nz=True
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_AUTH_V5_CAVE + 8))[0] == boot.AND_X10_X9_34
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_AUTH_V3_CAVE + 8))[0] == boot.MOV_W10_W9
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_AUTH_V3_CAVE + 12))[0] == boot.LDR_X11_X20_16
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SLIDE_AUTH_V3_CAVE + 28))[0] == boot._b(
        v3_rt + 28, boot.SLIDE_PAGE_V3_NEXT_RT
    )
    for va, _expect, repl in boot.AMFI_BOOTARG_ADDS:
        assert struct.unpack_from("<I", blob, boot.va_off(va))[0] == repl
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_WAIT_ENTRY))[0] == boot._b(
        boot.LAPIS_WAIT_ENTRY - boot.SLIDE, boot.LAPIS_WAIT_TARGET_RT
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_WAIT_TIMEOUT))[0] == boot.LAPIS_WAIT_TIMEOUT_REPL
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_FALLBACK_BNE))[0] == boot._b_cond(
        boot.LAPIS_FALLBACK_BNE - boot.SLIDE, boot.LAPIS_WAIT_TARGET_RT, 1
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_FALLBACK_CBZ))[0] == boot._cbz(
        0, boot.LAPIS_FALLBACK_CBZ - boot.SLIDE, boot.LAPIS_WAIT_TARGET_RT, width64=True
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_FALLBACK_BLS))[0] == boot._b_cond(
        boot.LAPIS_FALLBACK_BLS - boot.SLIDE, boot.LAPIS_WAIT_TARGET_RT, 9
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_WAIT_SPIN))[0] == 0x52800140
    assert struct.unpack_from("<I", blob, boot.va_off(boot.LAPIS_WAIT_SPIN + 4))[0] == boot._b(
        boot.LAPIS_WAIT_SPIN - boot.SLIDE + 4, boot.LAPIS_DELAY_RT, link=True
    )


if __name__ == "__main__":
    test_source_untouched_and_patches_apply()
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "make_boot_image.py"), "--check"])
    print("ok")
