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
    boot.patch_proc_state2_hang(blob)
    cmd = blob[boot.CMDLINE_OFF : boot.CMDLINE_OFF + boot.CMDLINE_LEN]
    text = cmd.split(b"\x00", 1)[0].decode("ascii")
    assert "rd=md0" in text
    assert "interrupt-masked-debug-mode=0" in text
    assert "amfi_get_out_of_my_way=1" in text
    assert cmd[len(text) + 1 :].strip(b"\x00") == b""
    for va, _expect, repl in boot.PATCHES:
        word = struct.unpack_from("<I", blob, boot.va_off(va))[0]
        assert word == repl
    iokit = struct.unpack_from("<I", blob, boot.va_off(boot.IOSTARTIOKIT_BL))[0]
    assert iokit == boot.MOVZ_W0_0
    cbz = struct.unpack_from("<I", blob, boot.va_off(boot.MOUNTROOT_CBZ))[0]
    assert cbz == boot._cbz(8, boot.MOUNTROOT_CBZ, boot.MOUNTROOT_CAVE)
    first = struct.unpack_from("<I", blob, boot.va_off(boot.MOUNTROOT_CAVE))[0]
    assert first == boot._movz_x(8, boot.LAPIS_MOUNT_RT & 0xFFFF, 0)
    cave_rt = boot.MOUNTROOT_CAVE - boot.SLIDE
    assert cave_rt >= 0xFFFFFE000756AC00
    enter_rt = boot.SCAN_ENTER_LDR - boot.SLIDE
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SCAN_ENTER_LDR + 4))[0] == boot._cbnz(
        8, enter_rt + 4, boot.SCAN_BUSY_RET_RT, width64=False
    )
    assert struct.unpack_from("<I", blob, boot.va_off(boot.SCAN_ENTER_LDR + 8))[0] == 0x52800028
    assert struct.unpack_from("<I", blob, boot.va_off(boot.PROC_STATE2_HANG))[0] == boot.PROC_STATE2_HANG_REPL


if __name__ == "__main__":
    test_source_untouched_and_patches_apply()
    subprocess.check_call([sys.executable, str(ROOT / "tools" / "make_boot_image.py"), "--check"])
    print("ok")
