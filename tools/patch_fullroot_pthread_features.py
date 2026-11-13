#!/usr/bin/env python3
"""Force libpthread feature bits so launchd survives bsdthread_register ≤0.

After _container_init is skipped, launchd dies with SIGTRAP (OS_REASON_SIGNAL
ns=2 / 0x5). GDB on the BRK shows:

  pc in dyld_shared_cache_arm64e.31 @ _pthread_workqueue_supported
  brk #0xb001 with message "BUG IN LIBPTHREAD: libpthread has not been initialized"

That crash fires when `__pthread_supported_features` is still 0.
`_pthread_bsdthread_init` only stores the feature mask when
`__bsdthread_register` returns >0 with the required bits; under TCG that
syscall returns ≤0 (pthread.kext register path does not satisfy userspace),
so the mask stays 0 and the first libdispatch workqueue probe BRKs.

Required bits (libpthread kern_internal.h):

  FINEPRIO|BSDTHREADCTL|SETSELF|QOS_MAINTENANCE|QOS_DEFAULT = 0x4000001e

This patch:
  1. Rewrites `_pthread_workqueue_supported` to return that mask (no BRK).
  2. Seeds the `__pthread_supported_features` word in `.33.dylddata` so other
     readers see the same value when register leaves it untouched.

Patches the LAPISFS GPT fullroot image (local artifact; not committed).
Aliased cryptex/preboot paths that share extents are patched once per extent.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lapisfs_names import gpt_partitions, lapisfs_paths  # noqa: E402

# _pthread_workqueue_supported in dyld_shared_cache_arm64e.31 (TEXT).
TEXT_SUBCACHE = "dyld_shared_cache_arm64e.31"
TEXT_BASE = 0x1E101C000
SITE_VA = 0x1E26248D0
SITE_OFF = SITE_VA - TEXT_BASE

# adrp x8, page; ldr w0, [x8, #28]; cbz w0, crash; ret
EXPECT_CODE = bytes.fromhex("c8ed0490001d40b940000034c0035fd6")
# movz w0, #0x1e; movk w0, #0x4000, lsl #16; ret; nop
REPL_CODE = bytes.fromhex("c00380520000a872c0035fd61f2003d5")

REQUIRED_FEATURES = 0x4000001E

# __pthread_supported_features in .33.dylddata (map addr 0x1ec0b4000).
DATA_SUBCACHE = "dyld_shared_cache_arm64e.33.dylddata"
DATA_MAP_ADDR = 0x1EC0B4000
DATA_MAP_FILEOFF = 0x4C50000
FEATURE_VA = 0x1EC3DC01C
FEATURE_OFF = DATA_MAP_FILEOFF + (FEATURE_VA - DATA_MAP_ADDR)


def _patch_code(f, root_off: int, paths: list, *, dry_run: bool) -> int:
    seen: set[int] = set()
    patched = 0
    for path, typ, off, size in paths:
        if typ != 2:
            continue
        pl = path if isinstance(path, str) else path.decode("utf-8", "replace")
        if not pl.endswith(TEXT_SUBCACHE):
            continue
        if off in seen:
            continue
        seen.add(off)
        if SITE_OFF + 16 > size:
            raise SystemExit(f"{pl}: code site past EOF ({size:#x})")
        abs_off = root_off + off + SITE_OFF
        f.seek(abs_off)
        cur = f.read(16)
        if cur == REPL_CODE:
            print(f"{pl}: code already patched @ {abs_off:#x}")
            patched += 1
            continue
        if cur != EXPECT_CODE:
            raise SystemExit(
                f"{pl}: unexpected code at {abs_off:#x}: {cur.hex()} "
                f"(want {EXPECT_CODE.hex()})"
            )
        print(f"{pl}: code patch @ {abs_off:#x}")
        if not dry_run:
            f.seek(abs_off)
            f.write(REPL_CODE)
        patched += 1
    if not patched:
        raise SystemExit(f"no {TEXT_SUBCACHE} extent found")
    return patched


def _patch_data(f, root_off: int, paths: list, *, dry_run: bool) -> int:
    seen: set[int] = set()
    patched = 0
    want = struct.pack("<I", REQUIRED_FEATURES)
    for path, typ, off, size in paths:
        if typ != 2:
            continue
        pl = path if isinstance(path, str) else path.decode("utf-8", "replace")
        if not pl.endswith(DATA_SUBCACHE):
            continue
        if off in seen:
            continue
        seen.add(off)
        if FEATURE_OFF + 4 > size:
            raise SystemExit(f"{pl}: feature word past EOF ({size:#x})")
        abs_off = root_off + off + FEATURE_OFF
        f.seek(abs_off)
        cur = f.read(4)
        if cur == want:
            print(f"{pl}: features already {REQUIRED_FEATURES:#x} @ {abs_off:#x}")
            patched += 1
            continue
        cur_i = struct.unpack("<I", cur)[0]
        # Fresh image has 0; tolerate re-entry if someone else set bits.
        if cur_i not in (0, REQUIRED_FEATURES):
            raise SystemExit(
                f"{pl}: unexpected features at {abs_off:#x}: {cur_i:#x}"
            )
        print(f"{pl}: features {cur_i:#x} -> {REQUIRED_FEATURES:#x} @ {abs_off:#x}")
        if not dry_run:
            f.seek(abs_off)
            f.write(want)
        patched += 1
    if not patched:
        raise SystemExit(f"no {DATA_SUBCACHE} extent found")
    return patched


def patch_image(img_path: pathlib.Path, *, dry_run: bool = False) -> tuple[int, int]:
    with img_path.open("r+b") as f:
        parts = gpt_partitions(f)
        root = next(p for p in parts if p[0] == "lapis-root")
        root_off = root[1]
        f.seek(root_off)
        directory = f.read(8 << 20)
        _, _, paths = lapisfs_paths(directory)
        n_code = _patch_code(f, root_off, paths, dry_run=dry_run)
        n_data = _patch_data(f, root_off, paths, dry_run=dry_run)
        return n_code, n_data


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
    n_code, n_data = patch_image(args.image, dry_run=args.dry_run)
    verb = "would patch" if args.dry_run else "patched"
    print(f"{verb} code={n_code} data={n_data} extent(s)")


if __name__ == "__main__":
    main()
