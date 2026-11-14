#!/usr/bin/env python3
"""Skip dyld Allocator pool freelist write into unmapped vm_map VAs.

After TPRO-local (#16), launchd still dies with OS_REASON_SIGNAL ns=2 / 0xb
(SIGSEGV) inside dyld's Allocator:

  /usr/lib/dyld  VA 0x7bf44  stp x8, x9, [x5]
  x3/x5 = pool base from MemoryManager::vm_map, size x4/x6 (often 0x20000)
  FAR = pool base; IFSC=7 (translation fault level 3)

The same bytes also exist in the shared-cache embedded dyld
(dyld_shared_cache_arm64e.01 @ preferred 0x180198f44), but QEMU+GDB at the
sleh BP shows the faulting RX page is the standalone /usr/lib/dyld vnode
(stock `stp`), not the .01 copy — matching TPRO-local / skip-chained, which
also patch /usr/lib/dyld.

Diagnosis (QEMU ptable walk at the sleh BP):
  - mach_vm_map returns KERN_SUCCESS with a VA near the 0x100000000 hint
  - that VA has no L3 PTE (also true when size is forced to one 16 KiB page
    and when the hint / VM_MEMORY_DYLD flags are stripped)
  - every RW shared-cache mappingWithSlide (.33/.70/.81 dylddata, etc.)
    is L2-miss at fault time — only RX TEXT subcaches are mapped
  - so a bump pool cannot live in DSC DATA

This patch branches from the faulting `stp` to the function epilogue
(`mov x0, x19`), skipping the freelist store, the in-pool metadata
allocation, and the zeroing that all need a mapped pool.

Patches /usr/lib/dyld inside a LAPISFS GPT fullroot image (local artifact;
not committed). Idempotent. Apply after pac_strip + skip_chained + tpro_local.
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

# Faulting freelist store (Mach-O VA == file offset in dyld TEXT).
STP_VA = 0x7BF44
STP_EXPECT = struct.pack("<I", 0xA90024A8)  # stp x8, x9, [x5]
EPILOGUE_VA = 0x7BF68  # mov x0, x19
_IMM = (EPILOGUE_VA - STP_VA) // 4
STP_REPL = struct.pack("<I", 0x14000000 | (_IMM & 0x3FFFFFF))  # b epilogue

# Back-compat aliases used by unit tests / callers.
STP_DYLD_VA = STP_VA
EPILOGUE_DYLD_VA = EPILOGUE_VA


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
                va=STP_VA,
                expect=STP_EXPECT,
                repl=STP_REPL,
                label="alloc-pool-stp-skip",
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
