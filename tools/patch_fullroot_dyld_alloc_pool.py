#!/usr/bin/env python3
"""Redirect dyld Allocator bump-pool into writable __DATA_DIRTY.

After alloc-page skip (#17), launchd still dies with OS_REASON_SIGNAL
ns=2 / 0xb. Skipping the freelist `stp` left the Allocator half-initialized
(pool ptr at +0x18 never set), and later dyld paths then take write
permission faults into `__DATA_CONST` (esr …04f).

Root cause of the original wall remains: MemoryManager::vm_map returns a
VA with no L3 PTE under QEMU, so a 0x20000 bump pool cannot live there.
This patch keeps the Allocator constructor's success path, but forces the
pool into a reserved zero run in dyld `__DATA_DIRTY` (0xb9000, 0x3000
bytes — past every ADRP+ADD ref into that segment).

Replaces `mov x19, x0` at VA 0x7bf24 with `b` into a TEXT cave that:
  1. Sets x3/x5 = pool, x4/x6 = size, re-stores [x0,#0x20]
  2. Builds the freelist tagged pointers and `stp`s them into the cave
  3. Stores the pool at [x0,#0x18], runs the 0x40-byte metadata alloc
  4. Returns via the original epilogue at 0x7bf6c

Compatible with a prior alloc-page skip at 0x7bf44 (that site becomes
unreachable). Idempotent. Apply after tpro_local (+ optional alloc_page).
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

# Constructor success-path entry (stock: mov x19, x0).
HOOK_VA = 0x7BF24
HOOK_EXPECT = struct.pack("<I", 0xAA0003F3)  # mov x19, x0

# Original epilogue after metadata install.
EPILOGUE_VA = 0x7BF6C  # ldp x29, x30, [sp, #0x10]

# TEXT cave (zeros at end of __TEXT).
CAVE_VA = 0xA5180

# Writable pool in __DATA_DIRTY past known ADRP refs (max 0xb8bf8).
POOL_VA = 0xB9000
POOL_SIZE = 0x3000

# Metadata bump inside the pool (same as stock).
POOL_ALLOC_VA = 0x86A8


def _enc_b(from_va: int, to_va: int) -> bytes:
    imm = (to_va - from_va) // 4
    return struct.pack("<I", 0x14000000 | (imm & 0x3FFFFFF))


def _enc_bl(from_va: int, to_va: int) -> bytes:
    imm = (to_va - from_va) // 4
    return struct.pack("<I", 0x94000000 | (imm & 0x3FFFFFF))


def _enc_adrp(rd: int, from_va: int, target_va: int) -> bytes:
    page = target_va & ~0xFFF
    pc_page = from_va & ~0xFFF
    imm = (page - pc_page) >> 12
    immlo = imm & 3
    immhi = (imm >> 2) & 0x7FFFF
    w = 0x90000000 | (immlo << 29) | (immhi << 5) | (rd & 0x1F)
    return struct.pack("<I", w)


def _enc_add_imm(rd: int, rn: int, imm12: int) -> bytes:
    return struct.pack("<I", 0x91000000 | ((imm12 & 0xFFF) << 10) | ((rn & 0x1F) << 5) | (rd & 0x1F))


def _enc_movz_x(rd: int, imm16: int) -> bytes:
    return struct.pack("<I", 0xD2800000 | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F))


def _enc_movz_w(rd: int, imm16: int) -> bytes:
    return struct.pack("<I", 0x52800000 | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F))


def build_cave() -> bytes:
    """Assemble the pool-redirect trampoline at CAVE_VA.

    Instruction encodings for the freelist/metadata sequence match stock
    dyld at 0x7bf28..0x7bf68 (verified against the fullroot image).
    """
    parts: list[bytes] = []
    va = CAVE_VA

    def emit(b: bytes) -> None:
        nonlocal va
        parts.append(b)
        va += len(b)

    # mov x19, x0
    emit(struct.pack("<I", 0xAA0003F3))
    # adrp x5, POOL_VA ; add x5, x5, #0
    emit(_enc_adrp(5, va, POOL_VA))
    emit(_enc_add_imm(5, 5, POOL_VA & 0xFFF))
    # movz x4, #POOL_SIZE ; mov x3, x5 ; mov x6, x4
    emit(_enc_movz_x(4, POOL_SIZE))
    emit(struct.pack("<I", 0xAA0503E3))
    emit(struct.pack("<I", 0xAA0403E6))
    # stp x3, x4, [x0, #0x20]
    emit(struct.pack("<I", 0xA9021003))
    # stock success-path body (pool in x5/x6); words are LE host values
    for w in (
        0x394043A8,  # ldrb w8, [x29, #0x10]
        0x927CECC9,  # and x9, x6, #~0xf
        0x3900E807,  # strb w7, [x0, #0x3a]
        0x3900E408,  # strb w8, [x0, #0x39]
        0xB2400008,  # orr x8, x0, #1
        0x8B050129,  # add x9, x9, x5
        0xB27F0129,  # orr x9, x9, #2
        0xA90024A8,  # stp x8, x9, [x5]
        0xF9000C05,  # str x5, [x0, #0x18]
        0x52800201,  # movz w1, #0x10
        0x52800802,  # movz w2, #0x40
    ):
        emit(struct.pack("<I", w))
    emit(_enc_bl(va, POOL_ALLOC_VA))
    for w in (
        0x6F00E400,  # movi v0.2d, #0
        0xAD000000,  # stp q0, q0, [x0]
        0xAD010000,  # stp q0, q0, [x0, #0x20]
        0xF9000660,  # str x0, [x19, #8]
        0xAA1303E0,  # mov x0, x19
    ):
        emit(struct.pack("<I", w))
    emit(_enc_b(va, EPILOGUE_VA))
    return b"".join(parts)


CAVE_BLOB = build_cave()
HOOK_REPL = _enc_b(HOOK_VA, CAVE_VA)


def _patch_bytes(
    f,
    *,
    root_off: int,
    file_off: int,
    size: int,
    pl: str,
    va: int,
    expect: bytes | None,
    repl: bytes,
    label: str,
    dry_run: bool,
    alt_expect: bytes | None = None,
) -> bool:
    if va + len(repl) > size:
        raise SystemExit(f"{pl}: {label} past EOF ({size:#x})")
    abs_off = root_off + file_off + va
    f.seek(abs_off)
    cur = f.read(max(len(repl), len(expect or b""), len(alt_expect or b"")))
    if cur[: len(repl)] == repl:
        print(f"{pl}: {label} already patched @ {abs_off:#x}")
        return True
    ok = False
    if expect is not None and cur[: len(expect)] == expect:
        ok = True
    if alt_expect is not None and cur[: len(alt_expect)] == alt_expect:
        ok = True
    if not ok:
        raise SystemExit(
            f"{pl}: unexpected bytes at {label} {abs_off:#x}: "
            f"{cur[: len(repl)].hex()}"
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
            # Cave first (zeros, or already our blob).
            _patch_bytes(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=CAVE_VA,
                expect=bytes(len(CAVE_BLOB)),
                repl=CAVE_BLOB,
                label="alloc-pool cave",
                dry_run=dry_run,
                alt_expect=CAVE_BLOB,
            )
            _patch_bytes(
                f,
                root_off=root_off,
                file_off=off,
                size=size,
                pl=pl,
                va=HOOK_VA,
                expect=HOOK_EXPECT,
                repl=HOOK_REPL,
                label="alloc-pool redirect",
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
