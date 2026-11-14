#!/usr/bin/env python3
"""Skip dyld paths that SEGV on unmapped pages under QEMU fullroot.

After PAC strip + brk#1 NOP, launchd dies with OS_REASON_SIGNAL ns=2 / 0xb
(SIGSEGV) inside dyld's chained-fixup page walker:

  /usr/lib/dyld VA 0x2b5d0  ldr x25, [x21]
  FAR = launchd __DATA + page_start (ptrfmt 12 / USERLAND24)

Registers match a legitimate call from VA 0x2b404 (LR 0x2b408) with
x0/x19 = slid __DATA base. IFSC=7 (translation fault level 3).

Launchd's __DATA / __DATA_CONST use DYLD_CHAINED_PTR_ARM64E_USERLAND24 (12).
Image.ipados already page-in-links that format via the Auth64 dyld pager
(nopac). Userspace must not also store through those pages during bring-up.

Patch A — chained walker VA 0x2b564 → `mov w0,#0; ret` (leave fixups to
the kernel pager on first touch).

With A applied, the next wall is another SIGSEGV 0xb in the dyld_cache
header walk at VA 0x65324:

  ldr w9, [x21, #4]
  x21 = *(cache_base + 0x208) + slide   # on-disk preferred 0x2b925fae0
  FAR inside sharedRegion but unmapped (hole / incomplete subcache map)

Patch B — at VA 0x652f4 change `b.cc` early-exit to unconditional `b` so
the extended header walk (mappingOffset >= 0x211) is skipped, matching
the old-header path.

Patches /usr/lib/dyld inside a LAPISFS GPT fullroot image (local artifact;
not committed). Idempotent. Requires pac_strip first (walker prologue NOP).
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

# --- Patch A: chained-fixup page walker ---
WALKER_VA = 0x2B564
# After pac_strip: pacibsp → NOP; keep the distinctive sub/stp tail.
WALKER_EXPECT = bytes.fromhex(
    "1f2003d5"  # nop (was pacibsp)
    "ff8301d1"  # sub sp, sp, #0x60
    "fa6701a9"  # stp x26, x25, [sp, #16]
)
WALKER_REPL = bytes.fromhex(
    "00008052"  # mov w0, #0
    "c0035fd6"  # ret
    "1f2003d5"  # nop (pad; keeps length)
)

# --- Patch B: dyld_cache header extended walk ---
# cmp w8,#0x211 ; b.cc early_ret  →  cmp ; b early_ret
HEADER_VA = 0x652F0
HEADER_EXPECT = bytes.fromhex(
    "1f450871"  # cmp w8, #0x211
    "63080054"  # b.cc 0x65400
)
HEADER_REPL = bytes.fromhex(
    "1f450871"  # cmp w8, #0x211 (kept for signature)
    "43000014"  # b 0x65400
)


def _patch_one(
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
    cur = f.read(len(expect))
    if cur == repl:
        print(f"{pl}: {label} already patched @ {abs_off:#x}")
        return True
    if cur != expect:
        raise SystemExit(
            f"{pl}: unexpected bytes at {label} {abs_off:#x}: {cur.hex()} "
            f"(want {expect.hex()})"
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
            _patch_one(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=WALKER_VA,
                expect=WALKER_EXPECT,
                repl=WALKER_REPL,
                label="skip chained walker",
                dry_run=dry_run,
            )
            _patch_one(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=HEADER_VA,
                expect=HEADER_EXPECT,
                repl=HEADER_REPL,
                label="skip cache header walk",
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
