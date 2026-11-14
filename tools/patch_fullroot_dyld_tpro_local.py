#!/usr/bin/env python3
"""Redirect dyld __dyld_apis store off the unmapped shared-cache TPRO page.

After skip-chained, launchd still dies with OS_REASON_SIGNAL ns=2 / 0xb
(SIGSEGV) on:

  /usr/lib/dyld VA 0x4f2fc  str x20, [x0]
  x0 = section addr of __DATA_CONST,__dyld_apis  (preferred 0x1ec090040)
  FAR inside .33.dylddata map[3] (flags CONST|TPRO=0x44) — never mapped
  under QEMU (vm_shared_region_start_address stays 0x1; clearing TPRO/CONST
  flags or relocating the Mach-O section into map[4] still leaves no VME).

Map merge of map[3] into map[2] regresses to "no dyld cache" / libSystem
missing. Userspace must not depend on that page being present.

This patch:
  1. Plants a 4-insn cave at VA 0x19e4 (TEXT padding):
       adrp x0, #0xb4000 ; add x0, x0, #0x300 ; str x20, [x0] ; ret
     → stores the Runtime pointer into dyld __DATA.__common (0xb4300).
  2. Replaces the 0x4f2fc store with `bl 0x19e4`.
  3. At VA 0x4f32c (start of the __helper PAC install that reads the
     also-unmapped .33 map[5] slot) branches to 0x4f3c8, skipping the
     helper object call and the NULL-pointer field copies that need it.

Clears the __dyld_apis write SEGV. Next wall is still SIGSEGV 0xb at later
dyld/shared-cache sites (see docs/evidence/gdb-shared-region-tpro.txt).

Patches /usr/lib/dyld inside a LAPISFS GPT fullroot image (local artifact;
not committed). Idempotent. Apply after pac_strip + dyld_skip_chained.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lapisfs_names import gpt_partitions, lapisfs_paths  # noqa: E402

MACHO_SUFFIX = "/usr/lib/dyld"

# TEXT padding cave (zeros in stock dyld after pac_strip).
CAVE_VA = 0x19E4
CAVE_EXPECT = bytes(16)
# adrp x0, #0xb4000 ; add x0, x0, #0x300 ; str x20, [x0] ; ret
CAVE_REPL = bytes.fromhex("800500f0000c0091140000f9c0035fd6")

# __dyld_apis install store.
STORE_VA = 0x4F2FC
STORE_EXPECT = bytes.fromhex("f9000014")  # str x20, [x0]
# bl 0x19e4
_STORE_IMM = (CAVE_VA - STORE_VA) // 4
STORE_REPL = struct.pack("<I", 0x94000000 | (_STORE_IMM & 0x3FFFFFF))

# __helper PAC install prologue → skip to post-field-copy.
HELPER_VA = 0x4F32C
HELPER_EXPECT = bytes.fromhex("aa0003f8")  # mov x24, x0
HELPER_TARGET = 0x4F3C8
_HELPER_IMM = (HELPER_TARGET - HELPER_VA) // 4
HELPER_REPL = struct.pack("<I", 0x14000000 | (_HELPER_IMM & 0x3FFFFFF))


def _patch_site(
    f,
    *,
    root_off: int,
    file_off: int,
    size: int,
    pl: str,
    va: int,
    expect: bytes,
    repl: bytes,
    label: str,
    dry_run: bool,
) -> bool:
    if va + len(expect) > size:
        raise SystemExit(f"{pl}: {label} past EOF ({size:#x})")
    abs_off = root_off + file_off + va
    f.seek(abs_off)
    cur = f.read(max(len(expect), len(repl)))
    if cur[: len(repl)] == repl:
        print(f"{pl}: {label} already patched @ {abs_off:#x}")
        return True
    if cur[: len(expect)] != expect:
        raise SystemExit(
            f"{pl}: unexpected bytes at {label} {abs_off:#x}: "
            f"{cur[: len(expect)].hex()} (want {expect.hex()})"
        )
    print(f"{pl}: {label} @ {abs_off:#x}")
    if not dry_run:
        f.seek(abs_off)
        f.write(repl)
    return True


def patch_image(img_path: pathlib.Path, *, dry_run: bool = False) -> int:
    with img_path.open("r+b") as f:
        parts = gpt_partitions(f)
        root = next(p for p in parts if p[0] == "lapis-root")
        root_off = root[1]
        f.seek(root_off)
        directory = f.read(8 << 20)
        _, _, paths = lapisfs_paths(directory)

        seen: set[int] = set()
        patched = 0
        for path, typ, off, size in paths:
            if typ != 2:
                continue
            pl = path if isinstance(path, str) else path.decode("utf-8", "replace")
            if not (pl.endswith(MACHO_SUFFIX) or pl == MACHO_SUFFIX.lstrip("/")):
                continue
            if off in seen:
                continue
            seen.add(off)
            _patch_site(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=CAVE_VA,
                expect=CAVE_EXPECT,
                repl=CAVE_REPL,
                label="TPRO local-store cave",
                dry_run=dry_run,
            )
            _patch_site(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=STORE_VA,
                expect=STORE_EXPECT,
                repl=STORE_REPL,
                label="redirect __dyld_apis store",
                dry_run=dry_run,
            )
            _patch_site(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=HELPER_VA,
                expect=HELPER_EXPECT,
                repl=HELPER_REPL,
                label="skip __helper PAC install",
                dry_run=dry_run,
            )
            patched += 1
        if not patched:
            raise SystemExit(f"no {MACHO_SUFFIX} extent found")
        return patched


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "image",
        nargs="?",
        default="/tmp/ufs/fullroot.img",
        type=pathlib.Path,
        help="LAPISFS GPT fullroot image (default: /tmp/ufs/fullroot.img)",
    )
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    n = patch_image(args.image, dry_run=args.dry_run)
    verb = "would patch" if args.dry_run else "patched"
    print(f"{verb} {n} dyld extent(s)")


if __name__ == "__main__":
    main()
