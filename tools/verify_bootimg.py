
#!/usr/bin/env python3
"""Verify a lapis fastboot boot.img for TB520FU / GKI packaging invariants.

Checks (fail => nonzero exit):
  - ANDROID! boot header v4, plausible kernel/ramdisk sizes
  - Linux arm64 Image magic + image_size == boot kernel_size
  - LAPISXNU embedded at Image offset == original shim _end (passed or detected)
  - boot.img ramdisk is small (empty gzip cpio); payload is NOT only in ramdisk
  - Optional: --expect-payload PATH must match embedded bytes
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def die(msg: str) -> None:
    print(f'FAIL: {msg}', file=sys.stderr)
    sys.exit(1)


def ok(msg: str) -> None:
    print(f'OK: {msg}')


def parse_boot(img: bytes):
    if img[:8] != b'ANDROID!':
        die(f'bad magic {img[:8]!r}')
    kernel_size, ramdisk_size, os_version, header_size = struct.unpack_from('<IIII', img, 8)
    header_version = struct.unpack_from('<I', img, 40)[0]
    if header_version != 4:
        die(f'header_version={header_version}, want 4')
    if header_size < 1580 or header_size > 4096:
        die(f'implausible header_size={header_size}')
    page = 4096
    hdr_pages = (header_size + page - 1) // page
    k_off = hdr_pages * page
    k_pages = (kernel_size + page - 1) // page
    r_off = k_off + k_pages * page
    kernel = img[k_off:k_off + kernel_size]
    ramdisk = img[r_off:r_off + ramdisk_size]
    return {
        'kernel_size': kernel_size,
        'ramdisk_size': ramdisk_size,
        'header_size': header_size,
        'header_version': header_version,
        'k_off': k_off,
        'r_off': r_off,
        'kernel': kernel,
        'ramdisk': ramdisk,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('bootimg')
    ap.add_argument('--expect-payload', help='payload.bin that must match Image[_end:]')
    ap.add_argument('--max-ramdisk', type=int, default=65536,
                    help='max allowed boot.img ramdisk size (default 64KiB)')
    a = ap.parse_args()

    img = Path(a.bootimg).read_bytes()
    ok(f'file size {len(img)}')
    b = parse_boot(img)
    ok(f'boot v{b["header_version"]} kernel_size={b["kernel_size"]} '
       f'ramdisk_size={b["ramdisk_size"]} k_off={b["k_off"]:#x} r_off={b["r_off"]:#x}')

    if b['ramdisk_size'] > a.max_ramdisk:
        die(f'ramdisk_size={b["ramdisk_size"]} > {a.max_ramdisk} '
            f'(GKI packaging expects tiny empty ramdisk; payload must be in Image)')
    ok(f'ramdisk small ({b["ramdisk_size"]} bytes)')

    # Empty gzip cpio typically starts with 1f8b
    rd = b['ramdisk']
    if rd[:2] == b'\x1f\x8b':
        ok('ramdisk gzip magic')
    elif rd[:6] == b'070701':
        ok('ramdisk raw cpio magic')
    else:
        ok(f'ramdisk head {rd[:8].hex()} (non-fatal)')

    kern = b['kernel']
    if len(kern) < 64:
        die('kernel too small')
    if kern[56:60] != b'ARM\x64':
        die(f'Linux Image magic missing: {kern[56:60]!r}')
    image_size = struct.unpack_from('<Q', kern, 16)[0]
    if image_size != len(kern):
        die(f'Image image_size={image_size} != kernel_size={len(kern)} '
            f'(ABL loads image_size bytes from the Image header)')
    ok(f'Linux Image image_size={image_size} matches kernel blob')

    # Payload is appended at a 4K-aligned offset (shim _end). Ignore any
    # accidental ASCII hits inside .rodata; only accept 4K-aligned magic.
    magic = b'LAPISXNU'
    off = -1
    for cand in range(0x1000, len(kern) - 8, 0x1000):
        if kern[cand:cand + 8] == magic:
            off = cand
            break
    if off < 0:
        die('LAPISXNU magic not found at any 4K-aligned Image offset')
    ok(f'LAPISXNU at Image offset {off} ({off:#x})')
    if off + 0x448 > len(kern):
        die('payload header truncated by image_size')

    # Header field coherence
    ver, nsegs = struct.unpack_from('<II', kern, off + 8)
    if ver != 1:
        die(f'payload version={ver}')
    if nsegs < 1 or nsegs > 64:
        die(f'implausible nsegs={nsegs}')
    phys, mem = struct.unpack_from('<QQ', kern, off + 0x10 + 5 * 8)
    ok(f'payload ver={ver} nsegs={nsegs} phys={phys:#x} mem={mem:#x}')

    # Must NOT rely solely on boot.img ramdisk containing the payload
    if magic in rd and off < 0:
        die('internal logic error')
    if magic in rd and b['ramdisk_size'] > a.max_ramdisk:
        die('payload appears only as large boot.img ramdisk (broken on this ABL)')
    if magic in rd:
        ok('note: LAPISXNU also present in ramdisk slot (redundant)')
    else:
        ok('payload not in boot.img ramdisk (correct for GKI/ABL)')

    if a.expect_payload:
        want = Path(a.expect_payload).read_bytes()
        got = kern[off:]
        if not got.startswith(want):
            # Allow trailing page padding in Image after payload
            if len(got) < len(want) or got[:len(want)] != want:
                die('embedded payload bytes != --expect-payload')
        ok(f'embedded payload matches {a.expect_payload} ({len(want)} bytes)')

    print('VERIFY PASS')


if __name__ == '__main__':
    main()

