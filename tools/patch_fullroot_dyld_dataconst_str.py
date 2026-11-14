#!/usr/bin/env python3
"""Nop dyld store into read-only __DATA_CONST (post-alloc-page).

After Allocator freelist skip (#17), launchd still dies with OS_REASON
ns=2 / 0xb. Live guest phys at the first write-permission fault (esr …04f)
is not the previously suspected 0x7f960 site — QEMU pmemsave shows:

  /usr/lib/dyld VA 0x7960  str x21, [x19]
  x19 = slid address in dyld __DATA_CONST (RO after relocation)
  FAR matches x19; IFSC permission fault level 3

Same bytes exist in the shared-cache embedded dyld (.01 at file
0x89000+0x7960). Patch both. Idempotent. Apply after tpro_local /
alloc_page (and optionally alloc_pool).
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
SUBCACHE_SUFFIX = "dyld_shared_cache_arm64e.01"

# Standalone dyld Mach-O VA (== file offset in TEXT).
STR_VA = 0x7960
# Embedded dyld in .01: TEXT file base for dyld VA 0.
DYLD_IN_CACHE = 0x18011D000
DYLD_FILE_BASE = DYLD_IN_CACHE - 0x180094000  # 0x89000

STR_EXPECT = struct.pack("<I", 0xF9000275)  # str x21, [x19]
STR_REPL = struct.pack("<I", 0xD503201F)  # nop


def _patch(
    f,
    *,
    root_off: int,
    file_off: int,
    size: int,
    pl: str,
    abs_file_off: int,
    dry_run: bool,
) -> bool:
    if abs_file_off + 4 > size:
        raise SystemExit(f"{pl}: dataconst-str past EOF ({size:#x})")
    abs_off = root_off + file_off + abs_file_off
    f.seek(abs_off)
    cur = f.read(4)
    if cur == STR_REPL:
        print(f"{pl}: dataconst-str already patched @ {abs_off:#x}")
        return True
    if cur != STR_EXPECT:
        raise SystemExit(
            f"{pl}: unexpected bytes at dataconst-str {abs_off:#x}: "
            f"{cur.hex()} (want {STR_EXPECT.hex()})"
        )
    print(f"{pl}: dataconst-str @ {abs_off:#x}")
    if not dry_run:
        f.seek(abs_off)
        f.write(STR_REPL)
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
            if pl.endswith(MACHO_SUFFIX) or pl == MACHO_SUFFIX.lstrip("/"):
                if off in seen:
                    continue
                seen.add(off)
                _patch(
                    f,
                    root_off=root_off,
                    file_off=off,
                    size=size,
                    pl=pl,
                    abs_file_off=STR_VA,
                    dry_run=dry_run,
                )
                patched += 1
            elif pl.endswith(SUBCACHE_SUFFIX):
                if off in seen:
                    continue
                seen.add(off)
                _patch(
                    f,
                    root_off=root_off,
                    file_off=off,
                    size=size,
                    pl=pl,
                    abs_file_off=DYLD_FILE_BASE + STR_VA,
                    dry_run=dry_run,
                )
                patched += 1
        if not patched:
            raise SystemExit("no dyld / .01 extent found")
        return patched


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "image",
        nargs="?",
        default="/tmp/ufs/fullroot.img",
        type=pathlib.Path,
    )
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    n = patch_image(args.image, dry_run=args.dry_run)
    verb = "would patch" if args.dry_run else "patched"
    print(f"{verb} {n} extent(s)")


if __name__ == "__main__":
    main()
