#!/usr/bin/env python3
"""Re-apply the intentional binary patches that turn
lapis-xnu-entry-debug.img into the code portion of
lapis-xnu-post-s3-debug.img.

Recovered by diffing the two real images. Does NOT invent offsets.
Does NOT recompute trailing digests (those remain unknown/packaging).
"""
from __future__ import annotations
import argparse
import pathlib
import struct

# boot.img v4: kernel Image at file offset 0x1000
KOFF = 0x1000

PATCHES = [
    # (image_off, old, new, purpose)
    (0x578330, bytes.fromhex("e80e0037"), bytes.fromhex("1f2003d5"),
     "early_disabled tbnz -> NOP"),
    (0x57D594, bytes.fromhex("1f2003d5") * 20,
     bytes.fromhex(
         "e007bfa9020094d262c8baf20200c0f20200e0f2"
         "03e09fd2e3ffbff2040880d2054080d2e60302aa"
         "c34400b8a50400f1c1ffff5407c085d24200078b"
         "840400f101ffff549f3f03d5e007c1a84cebff17"
     ),
     "yellow C-beacon trampoline (80 bytes over NOP sled)"),
    (0xBDE7A0, bytes.fromhex("dc66e697"), bytes.fromhex("7d7be697"),
     "arm_init bl mark -> bl beacon"),
    (0x57F988, bytes.fromhex("00c018d5"), bytes.fromhex("1f2003d5"),
     "msr vbar_el1,x0 -> NOP"),
]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("entry_img", type=pathlib.Path)
    ap.add_argument("-o", "--output", type=pathlib.Path, required=True)
    ap.add_argument("--check-only", action="store_true")
    args = ap.parse_args()
    data = bytearray(args.entry_img.read_bytes())
    for img_off, old, new, purpose in PATCHES:
        off = KOFF + img_off
        have = bytes(data[off : off + len(old)])
        if have != old:
            raise SystemExit(
                f"mismatch at Image+{img_off:#x} ({purpose}): "
                f"have {have.hex()} want {old.hex()}"
            )
        if not args.check_only:
            data[off : off + len(new)] = new
        print(f"OK Image+{img_off:#x} boot {off:#x}: {purpose}")
    if args.check_only:
        return
    args.output.write_bytes(data)
    print(f"wrote {args.output} ({len(data)} bytes)")
    print("NOTE: trailing 64+2 hash/trailer bytes are NOT reproduced.")


if __name__ == "__main__":
    main()
