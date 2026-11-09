
#!/usr/bin/env python3
"""Minimal Android boot image v4 writer (no GKI certificate).

Layout (AOSP bootimg.h v4): page-aligned header, kernel, ramdisk, then
optional signature (we write signature_size=0).
"""
import argparse, os, struct

BOOT_MAGIC = b'ANDROID!'
BOOT_ARGS_SIZE = 512
BOOT_EXTRA_ARGS_SIZE = 1024
PAGE_SIZE = 4096


def encode_os_version(ver: str, patch: str) -> int:
    # ver = "A.B.C", patch = "YYYY-MM"
    a, b, c = (int(x) for x in ver.split('.'))
    y, m = (int(x) for x in patch.split('-'))
    return ((a & 0x7f) << 25) | ((b & 0x7f) << 18) | ((c & 0x7f) << 11) | (((y - 2000) & 0x7f) << 4) | (m & 0xf)


def pad(buf: bytes, page: int) -> bytes:
    rem = (-len(buf)) % page
    return buf if rem == 0 else buf + b'\0' * rem


def build(kernel: bytes, ramdisk: bytes, cmdline: str, os_version: int) -> bytes:
    # Struct size through signature_size (see AOSP bootimg.h v4).
    header_struct_size = 8 + 4 + 4 + 4 + 4 + 16 + 4 + (BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE) + 4
    assert header_struct_size == 1584, header_struct_size
    cmdline_b = cmdline.encode()[: BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE - 1]
    hdr = bytearray(header_struct_size)
    struct.pack_into('<8sII', hdr, 0, BOOT_MAGIC, len(kernel), len(ramdisk))
    struct.pack_into('<I', hdr, 16, os_version)
    struct.pack_into('<I', hdr, 20, header_struct_size)
    # reserved[4] at 24..39 remain zero
    struct.pack_into('<I', hdr, 40, 4)  # header_version
    hdr[44:44 + len(cmdline_b)] = cmdline_b
    struct.pack_into('<I', hdr, 44 + BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE, 0)  # signature_size

    out = pad(bytes(hdr), PAGE_SIZE)
    out += pad(kernel, PAGE_SIZE)
    out += pad(ramdisk, PAGE_SIZE)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kernel', required=True)
    ap.add_argument('--ramdisk', required=True)
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--cmdline', default='')
    ap.add_argument('--os-version', default='15.0.0')
    ap.add_argument('--os-patch-level', default='2025-01')
    a = ap.parse_args()
    blob = build(open(a.kernel, 'rb').read(), open(a.ramdisk, 'rb').read(),
                 a.cmdline, encode_os_version(a.os_version, a.os_patch_level))
    open(a.output, 'wb').write(blob)
    print(f'{a.output}: {len(blob)} bytes (boot v4)')


if __name__ == '__main__':
    main()

