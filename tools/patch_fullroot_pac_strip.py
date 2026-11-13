#!/usr/bin/env python3
"""Strip arm64e PAC ops in fullroot so unsigned Auth64 pointers can run.

Auth64 pager / slide_page leave pointers unsigned (TCG has no matching JOP
key). Userspace then hits `autda`/`blraa`/`retab` and dies with SIGTRAP
(`brk #0x1` / `#0xc47x`). Bergamota-style: rewrite PAC instructions to their
non-PAC equivalents across dyld shared-cache TEXT (and standalone dyld /
launchd Mach-Os when present).

Rewrites (in place, 4-byte aligned):
  PACIASP/PACIBSP/PACIAZ/PACIBZ → NOP (do not sign)
  AUTIASP/AUTIBSP/AUTIAZ/AUTIBZ → XPACLRI (strip LR)
  PACIA/PACIB/PACDA/PACDB / PACIZA/... → NOP
  AUTIA/AUTIB/AUTIZA/AUTIZB → XPACI <Xd> (strip, no verify)
  AUTDA/AUTDB/AUTDZA/AUTDZB → XPACD <Xd>
  RETAA/RETAB → RET
  BLRAA/BLRAB/BLRAAZ/BLRABZ → BLR <Xn>
  BRAA/BRAB/BRAAZ/BRABZ → BR <Xn>
  BRK #0xc470..#0xc473 (PAC fail traps) → NOP
  BRK #0x1 padding → NOP only if NOP_BRK1=True

AUT must become XPAC*, not NOP: leaving PAC bits in a pointer
makes the next load/branch use a non-canonical address. Signing
ops stay NOP so Auth64's unsigned stores keep working.

Padding `brk #1` is left alone by default. For QEMU bring-up pass
`--nop-brk1` (or set NOP_BRK1=True): that is what clears SIGTRAP 0x5
after PAC rewrite; surgical patches at a guessed death PC did not.
Nopping padding can fall through into the next function (next wall
has been SIGSEGV 0xb).

Patches a LAPISFS GPT fullroot image (local artifact; not committed).
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lapisfs_names import gpt_partitions, lapisfs_paths  # noqa: E402

NOP = 0xD503201F
RET = 0xD65F03C0
XPACLRI = 0xD50320FF
XPACI_BASE = 0xDAC143E0  # | Rd
XPACD_BASE = 0xDAC147E0  # | Rd

# Bring-up: nop TEXT padding `brk #1` (can cause fallthrough). Off by default.
NOP_BRK1 = False

# Sign hints → NOP; auth hints → strip LR.
HINT_PAC_SIGN = {
    0xD503231F,  # paciaz
    0xD503233F,  # paciasp
    0xD503235F,  # pacibz
    0xD503237F,  # pacibsp
}
HINT_AUT_LR = {
    0xD503239F,  # autiaz
    0xD50323BF,  # autiasp
    0xD50323DF,  # autibz
    0xD50323FF,  # autibsp
}

SKIP_NAME_PARTS = (
    ".dylddata",
    ".dyldreadonly",
    ".dyldlinkedit",
    ".atlas",
)

# Standalone Mach-O paths that also execute before/with the cache.
MACHO_SUFFIXES = (
    "/usr/lib/dyld",
    "/sbin/launchd",
)


def rewrite_word(w: int) -> int | None:
    """Return replacement word, or None if unchanged."""
    if w in HINT_PAC_SIGN:
        return NOP
    if w in HINT_AUT_LR:
        return XPACLRI
    if w == XPACLRI:
        return None
    if w in (0xD65F0BFE, 0xD65F0FFF):  # retaa / retab
        return RET

    # BLRAA/BLRAB Xn,Xm — address is in bits[9:5] (matches binutils operand 1).
    if (w & 0xFFFFF800) in (0xD73F0800, 0xD73F0C00):
        return 0xD63F0000 | (w & 0x3E0)
    # BLRAAZ/BLRABZ Xn — address in bits[4:0], Rm=11111.
    if (w & 0xFFFFFC1F) in (0xD73F0BE0, 0xD73F0FE0):
        return 0xD63F0000 | ((w & 0x1F) << 5)

    # BRAA/BRAB Xn,Xm
    if (w & 0xFFFFF800) in (0xD61F0800, 0xD61F0C00):
        return 0xD61F0000 | (w & 0x3E0)
    # BRAAZ/BRABZ Xn
    if (w & 0xFFFFFC1F) in (0xD61F0BE0, 0xD61F0FE0):
        return 0xD61F0000 | ((w & 0x1F) << 5)

    # Two-register PAC/AUT: 0xDAC10xxx / 0xDAC11xxx. Rd in bits[4:0].
    if 0xDAC10000 <= w <= 0xDAC11FFF:
        rd = w & 0x1F
        op = (w >> 10) & 0x7
        # 0 PACIA, 1 PACIB, 2 PACDA, 3 PACDB, 4 AUTIA, 5 AUTIB, 6 AUTDA, 7 AUTDB
        if op <= 3:
            return NOP
        if op in (4, 5):
            return XPACI_BASE | rd
        return XPACD_BASE | rd

    # Zero-modifier PAC/AUT (*ZA/*ZB)
    rd = w & 0x1F
    if (w & 0xFFFFFFE0) in (0xDAC123E0, 0xDAC127E0):  # paciza/pacizb
        return NOP
    if (w & 0xFFFFFFE0) in (0xDAC12BE0, 0xDAC12FE0):  # pacdza/pacdzb
        return NOP
    if (w & 0xFFFFFFE0) in (0xDAC133E0, 0xDAC137E0):  # autiza/autizb
        return XPACI_BASE | rd
    if (w & 0xFFFFFFE0) in (0xDAC13BE0, 0xDAC13FE0):  # autdza/autdzb
        return XPACD_BASE | rd
    # Leave existing XPACI/XPACD alone
    if (w & 0xFFFFFFE0) in (XPACI_BASE, XPACD_BASE):
        return None

    # PAC failure software traps. Optional padding BRK #1 via NOP_BRK1.
    if (w & 0xFFE0001F) == 0xD4200000:
        imm = (w >> 5) & 0xFFFF
        if 0xC470 <= imm <= 0xC473:
            return NOP
        if imm == 1 and NOP_BRK1:
            return NOP

    return None

def _is_movk_rd(w: int, rd: int) -> bool:
    """True if w is MOVK <Xd>, #imm, LSL #(0|16|32|48) writing rd."""
    # MOVK Xd: 111100101 hw imm16 Rd  (sf=1)
    return (w & 0xFF80001F) == (0xF2800000 | (rd & 0x1F))


def strip_blob(data: bytearray) -> dict[str, int]:
    """Rewrite PAC ops in an executable blob. Returns per-class counts."""
    counts: dict[str, int] = {}
    n = len(data) & ~3
    for off in range(0, n, 4):
        w = struct.unpack_from("<I", data, off)[0]
        repl = rewrite_word(w)
        if repl is None or repl == w:
            continue
        # Classify for stats
        if w in HINT_PAC_SIGN or w in HINT_AUT_LR:
            key = "hint"
        elif w in (0xD65F0BFE, 0xD65F0FFF):
            key = "ret_auth"
        elif (w & 0xFFFFF800) in (0xD73F0800, 0xD73F0C00) or (
            w & 0xFFFFFC1F
        ) in (0xD73F0BE0, 0xD73F0FE0):
            key = "blr_auth"
        elif (w & 0xFFFFF800) in (0xD61F0800, 0xD61F0C00) or (
            w & 0xFFFFFC1F
        ) in (0xD61F0BE0, 0xD61F0FE0):
            key = "br_auth"
        elif 0xDAC10000 <= w <= 0xDAC14FFF:
            key = "pac_aut"
        elif (w & 0xFFE0001F) == 0xD4200000 and ((w >> 5) & 0xFFFF) == 1:
            key = "brk1"
        else:
            key = "brk_pac"
        counts[key] = counts.get(key, 0) + 1
        struct.pack_into("<I", data, off, repl)

        # BLRAA/BRAA auth the address reg then branch. After rewriting to
        # BLR/BR the PAC bits must be stripped. The modifier is almost
        # always materialised with MOVK into Xm immediately before; replace
        # that MOVK with XPACI <Xn> (same 4 bytes, keeps the branch).
        if key in ("blr_auth", "br_auth") and off >= 4:
            # Non-Z forms: addr in bits[9:5], modifier in bits[4:0]
            if (w & 0xFFFFF800) in (
                0xD73F0800,
                0xD73F0C00,
                0xD61F0800,
                0xD61F0C00,
            ):
                addr_rd = (w >> 5) & 0x1F
                mod_rd = w & 0x1F
                prev = struct.unpack_from("<I", data, off - 4)[0]
                if _is_movk_rd(prev, mod_rd):
                    struct.pack_into("<I", data, off - 4, XPACI_BASE | addr_rd)
                    counts["xpaci_before_br"] = counts.get("xpaci_before_br", 0) + 1
    return counts

def _is_text_subcache(path: str) -> bool:
    if "dyld_shared_cache_arm64e" not in path:
        return False
    if any(s in path for s in SKIP_NAME_PARTS):
        return False
    # Main header-only cache file is tiny TEXT stub; still strip if present.
    return True


def _macho_text_slices(blob: bytes) -> list[tuple[int, int]]:
    """Return (file_off, size) for executable segments in a Mach-O / FAT."""
    if len(blob) < 0x20:
        return []
    magic = struct.unpack_from("<I", blob, 0)[0]
    slices: list[tuple[int, int]] = []
    if magic in (0xCAFEBABE, 0xBEBAFECA):  # FAT
        # Use big-endian for FAT header
        endian = ">" if magic == 0xCAFEBABE else "<"
        nfat = struct.unpack_from(endian + "I", blob, 4)[0]
        for i in range(nfat):
            off = 8 + i * 20
            cputype, _, offset, size, _ = struct.unpack_from(
                endian + "IIIII", blob, off
            )
            if cputype & 0xFF == 0x0C:  # CPU_TYPE_ARM64 family low byte
                slices.extend(
                    _macho_text_slices_thin(blob[offset : offset + size], offset)
                )
        return slices
    return _macho_text_slices_thin(blob, 0)


def _macho_text_slices_thin(blob: bytes, base: int) -> list[tuple[int, int]]:
    if len(blob) < 32:
        return []
    magic = struct.unpack_from("<I", blob, 0)[0]
    if magic not in (0xFEEDFACF, 0xCFFAEDFE):
        return []
    endian = "<" if magic == 0xFEEDFACF else ">"
    ncmds = struct.unpack_from(endian + "I", blob, 16)[0]
    sizeofcmds = struct.unpack_from(endian + "I", blob, 20)[0]
    off = 32
    end = min(len(blob), 32 + sizeofcmds)
    out: list[tuple[int, int]] = []
    for _ in range(ncmds):
        if off + 8 > end:
            break
        cmd, cmdsize = struct.unpack_from(endian + "II", blob, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            # cmd,cmdsize,segname[16],vmaddr,vmsize,fileoff,filesize,maxprot,initprot
            if off + 64 <= len(blob):
                _vmaddr, _vmsize, fileoff, filesize = struct.unpack_from(
                    endian + "QQQQ", blob, off + 24
                )
                maxprot, initprot = struct.unpack_from(endian + "II", blob, off + 56)
                # VM_PROT_EXECUTE = 4. Some arm64e dyld TEXT lists initprot=0
                # in the on-disk header; still strip if the segname is __TEXT
                # or either prot bit is set.
                segname = blob[off + 8 : off + 24].split(b"\0", 1)[0]
                executable = (initprot & 4) or (maxprot & 4) or segname == b"__TEXT"
                if executable and filesize and fileoff + filesize <= len(blob):
                    out.append((base + fileoff, int(filesize)))
        if cmdsize < 8:
            break
        off += cmdsize
    return out


def patch_image(img_path: pathlib.Path, *, dry_run: bool = False) -> dict:
    stats: dict = {"subcaches": 0, "machos": 0, "words": 0, "counts": {}}
    with img_path.open("r+b") as f:
        parts = gpt_partitions(f)
        root = next(p for p in parts if p[0] == "lapis-root")
        root_off = root[1]
        f.seek(root_off)
        directory = f.read(8 << 20)
        _, _, paths = lapisfs_paths(directory)

        seen: set[int] = set()
        for path, typ, off, size in paths:
            if typ != 2:
                continue
            pl = path if isinstance(path, str) else path.decode("utf-8", "replace")
            if off in seen:
                continue

            do_cache = _is_text_subcache(pl)
            do_macho = any(pl.endswith(s) or pl == s for s in MACHO_SUFFIXES)
            if not do_cache and not do_macho:
                continue
            seen.add(off)

            f.seek(root_off + off)
            raw = bytearray(f.read(size))
            if do_cache:
                # Strip whole file: TEXT subcaches are almost entirely code;
                # rewriting DATA-ish tails as code is rare false-positive risk
                # for HINT/PAC encodings and is accepted for TCG bring-up.
                counts = strip_blob(raw)
                kind = "cache"
            else:
                counts = {}
                for so, ss in _macho_text_slices(bytes(raw)):
                    part = bytearray(raw[so : so + ss])
                    c = strip_blob(part)
                    raw[so : so + ss] = part
                    for k, v in c.items():
                        counts[k] = counts.get(k, 0) + v
                kind = "macho"

            total = sum(counts.values())
            if total == 0:
                print(f"{pl}: no PAC ops ({kind})")
                continue
            print(f"{pl}: {kind} rewrites={total} {counts}")
            stats["words"] += total
            for k, v in counts.items():
                stats["counts"][k] = stats["counts"].get(k, 0) + v
            if kind == "cache":
                stats["subcaches"] += 1
            else:
                stats["machos"] += 1
            if not dry_run:
                f.seek(root_off + off)
                f.write(raw)

    if stats["subcaches"] == 0 and stats["machos"] == 0:
        raise SystemExit("no DSC TEXT / dyld / launchd extents patched")
    return stats


def main() -> None:
    global NOP_BRK1
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "image",
        nargs="?",
        default="/tmp/ufs/fullroot.img",
        type=pathlib.Path,
    )
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument(
        "--nop-brk1",
        action="store_true",
        help="also rewrite TEXT padding brk #1 → NOP (can fall through)",
    )
    args = ap.parse_args()
    NOP_BRK1 = bool(args.nop_brk1)
    stats = patch_image(args.image, dry_run=args.dry_run)
    verb = "would rewrite" if args.dry_run else "rewrote"
    print(
        f"{verb} words={stats['words']} "
        f"subcaches={stats['subcaches']} machos={stats['machos']} "
        f"by_class={stats['counts']} nop_brk1={NOP_BRK1}"
    )


if __name__ == "__main__":
    main()
