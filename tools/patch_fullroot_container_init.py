#!/usr/bin/env python3
"""Skip fatal sandbox_get_container_expected in libSystem _container_init.

On fullroot, launchd's libSystem initializer calls _container_init, which
invokes sandbox_get_container_expected via __mac_syscall("Sandbox", 0x43).
That returns -1 under QEMU (no working sandbox/containermanagerd), and
_container_init aborts with OS_REASON_LIBSYSTEM subcode 0x2:

  _container_init(): sandbox_get_container_expected() failed: -1

When the expected-flag out-param is 0, _container_init skips containerization
and returns successfully. This patch replaces the call site:

  add x0, sp, #0x17
  add x1, sp, #0x16
  bl  sandbox_get_container_expected
  cbnz w0, fatal

with:

  strb wzr, [sp, #23]   ; expected = 0
  strb wzr, [sp, #22]
  mov  w0, #0
  nop

so the following `cmp expected, #1` / `b.ne epilogue` takes the success path.

Patches dyld_shared_cache_arm64e.31 inside a LAPISFS GPT fullroot image
(local artifact; not committed). Aliased cryptex/preboot paths share the
same extent.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lapisfs_names import gpt_partitions, lapisfs_paths  # noqa: E402

# VA of the 4-insn call site in dyld_shared_cache_arm64e.31 (TEXT map).
SITE_VA = 0x1E2776EA0
SUBCACHE_BASE = 0x1E101C000
SITE_OFF = SITE_VA - SUBCACHE_BASE

EXPECT = bytes.fromhex("e05f0091e15b009186f0b29400100035")
REPL = bytes.fromhex("ff5f0039ff5b0039000080521f2003d5")

SUBCACHE_SUFFIX = "dyld_shared_cache_arm64e.31"


def patch_image(img_path: pathlib.Path, *, dry_run: bool = False) -> int:
    with img_path.open("r+b") as f:
        parts = gpt_partitions(f)
        root = next(p for p in parts if p[0] == "lapis-root")
        root_off = root[1]
        f.seek(root_off)
        directory = f.read(8 << 20)
        _, _, paths = lapisfs_paths(directory)

        seen_extents: set[int] = set()
        patched = 0
        for path, typ, off, size in paths:
            if typ != 2:
                continue
            pl = path if isinstance(path, str) else path.decode("utf-8", "replace")
            if not pl.endswith(SUBCACHE_SUFFIX):
                continue
            if off in seen_extents:
                continue
            seen_extents.add(off)
            if SITE_OFF + 16 > size:
                raise SystemExit(f"{pl}: site past EOF ({size:#x})")
            abs_off = root_off + off + SITE_OFF
            f.seek(abs_off)
            cur = f.read(16)
            if cur == REPL:
                print(f"{pl}: already patched @ {abs_off:#x}")
                patched += 1
                continue
            if cur != EXPECT:
                raise SystemExit(
                    f"{pl}: unexpected bytes at {abs_off:#x}: {cur.hex()} "
                    f"(want {EXPECT.hex()})"
                )
            print(f"{pl}: patch @ {abs_off:#x}")
            if not dry_run:
                f.seek(abs_off)
                f.write(REPL)
            patched += 1
        if not patched:
            raise SystemExit(f"no {SUBCACHE_SUFFIX} extent found")
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
    print(f"{'would patch' if args.dry_run else 'patched'} {n} extent(s)")


if __name__ == "__main__":
    main()
