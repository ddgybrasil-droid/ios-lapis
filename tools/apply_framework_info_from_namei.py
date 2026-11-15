#!/usr/bin/env python3
"""Add IPSW on-disk framework resources (excl. CFBundleExecutable) for frameworks
that appear as NAMEI_FAIL directory lookups in a gdb-events log and are listed in
docs/status/boot55-frameworks-needing-info.txt.

usage:
  apply_framework_info_from_namei.py IMG GDB_EVENTS IPSW_ROOT [METADATA.tsv]
    [--budget-mib 150] [--dry-run]

Add-only via lapisfs_add semantics (existing paths kept). Skips Mach-O
CFBundleExecutable. Does not invent stubs. CoreFoundation/Foundation should be
applied via apply_cf_foundation_frameworks.sh (full resources including
Error.loctable); this tool is for the boot59∩441 remainder.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

NAMEI_RE = re.compile(
    r"NAMEI_FAIL err=2 path='(/System/Library/(?:Private)?Frameworks/[^']+\.framework)(?:/[^']*)?'"
)


def parse_namei_frameworks(events_path: str) -> set[str]:
    out: set[str] = set()
    with open(events_path, "r", errors="replace") as f:
        for line in f:
            m = NAMEI_RE.search(line)
            if m:
                out.add(m.group(1))
    return out


def load_needing_info(list_path: str) -> set[str]:
    out: set[str] = set()
    with open(list_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("/System/"):
                out.add(line)
    return out


def framework_payload_bytes(ipsw_fw: str) -> tuple[int, str | None]:
    """Return (byte size of copyable files, executable basename to skip)."""
    base = os.path.basename(ipsw_fw)
    if not base.endswith(".framework"):
        return 0, None
    exe_name = base[: -len(".framework")]
    total = 0
    if not os.path.isdir(ipsw_fw):
        return 0, exe_name
    for dp, _dns, fns in os.walk(ipsw_fw):
        for fn in fns:
            if fn == exe_name and os.path.dirname(os.path.join(dp, fn)) == ipsw_fw:
                continue
            p = os.path.join(dp, fn)
            try:
                total += os.path.getsize(p)
            except OSError:
                pass
    return total, exe_name


def stage_framework(ipsw_fw: str, dest_fw: str, exe_name: str | None) -> None:
    def ignore(directory: str, names: list[str]) -> list[str]:
        if exe_name and directory.rstrip("/") == ipsw_fw.rstrip("/") and exe_name in names:
            return [exe_name]
        return []

    if os.path.exists(dest_fw):
        shutil.rmtree(dest_fw)
    shutil.copytree(ipsw_fw, dest_fw, symlinks=True, ignore=ignore)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("img")
    ap.add_argument("gdb_events")
    ap.add_argument("ipsw_root")
    ap.add_argument("metadata", nargs="?")
    ap.add_argument("--budget-mib", type=float, default=150.0)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument(
        "--needing-list",
        default=os.path.join(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
            "docs/status/boot55-frameworks-needing-info.txt",
        ),
    )
    ap.add_argument(
        "--skip",
        action="append",
        default=[],
        help="framework abs path already applied elsewhere (repeatable)",
    )
    args = ap.parse_args()

    namei = parse_namei_frameworks(args.gdb_events)
    needing = load_needing_info(args.needing_list)
    skip = set(args.skip) | {
        "/System/Library/Frameworks/CoreFoundation.framework",
        "/System/Library/Frameworks/Foundation.framework",
        "/System/Library/PrivateFrameworks/AppProtection.framework",
        "/System/Library/PrivateFrameworks/ContainerManagerCommon.framework",
    }
    candidates = sorted((namei & needing) - skip)
    budget = int(args.budget_mib * 1024 * 1024)

    selected: list[tuple[str, int, str | None]] = []
    used = 0
    deferred: list[tuple[str, int]] = []
    for fw in candidates:
        ipsw_fw = args.ipsw_root + fw
        sz, exe = framework_payload_bytes(ipsw_fw)
        if sz == 0 and not os.path.isfile(os.path.join(ipsw_fw, "Info.plist")):
            print(f"skip (no IPSW Info.plist): {fw}")
            continue
        if used + sz > budget:
            deferred.append((fw, sz))
            continue
        selected.append((fw, sz, exe))
        used += sz

    print(f"NAMEI frameworks: {len(namei)}")
    print(f"intersection with needing-info: {len(namei & needing)}")
    print(f"selected: {len(selected)} ({used} bytes, {used/1024/1024:.2f} MiB)")
    for fw, sz, _ in selected:
        print(f"  + {fw} ({sz} B)")
    if deferred:
        print(f"deferred over budget: {len(deferred)}")
        for fw, sz in deferred[:20]:
            print(f"  - {fw} ({sz} B)")

    if args.dry_run or not selected:
        return 0

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    lapisfs_add = os.path.join(root, "tools/lapisfs_add.py")
    with tempfile.TemporaryDirectory(prefix="fwinfo-") as tmp:
        maps: list[str] = []
        for fw, _sz, exe in selected:
            ipsw_fw = args.ipsw_root + fw
            # DEST for lapisfs_add is relative without leading slash
            dest_rel = fw.lstrip("/")
            stage = os.path.join(tmp, dest_rel)
            os.makedirs(os.path.dirname(stage), exist_ok=True)
            stage_framework(ipsw_fw, stage, exe)
            maps.append(f"{dest_rel}={stage}")
        cmd = [sys.executable, lapisfs_add, args.img, *maps]
        subprocess.check_call(cmd)

    if args.metadata:
        owners = os.path.join(root, "tools/lapisfs_owners.py")
        subprocess.check_call(
            [sys.executable, owners, args.img, args.metadata, "--modes", "/private/var"]
        )
    else:
        print("NOTE: skipped lapisfs_owners (pass METADATA.tsv to keep /private/var/mobile 501:501)")
    print(f"done: {args.img}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
