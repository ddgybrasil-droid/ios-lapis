#!/usr/bin/env python3
"""List IPSW frameworks that have on-disk Info.plist but are absent from a lapisfs image.

usage: list_missing_framework_info.py IMG [IPSW_ROOT]
  IPSW_ROOT defaults to /tmp/apfs-mnt/root (apfs-fuse of 094-54982-099.dmg).
  Read-only; does not modify IMG.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lapisfs_names import gpt_partitions, lapisfs_paths

def lapisfs_path_set(img):
    with open(img, 'rb') as f:
        parts = gpt_partitions(f)
        root = next(p for p in parts if p[0] == 'lapis-root')
        # metadata can exceed 1 MiB on bring-up images; read up to 32 MiB prefix
        f.seek(root[1])
        blob = f.read(32 << 20)
    _, _, paths = lapisfs_paths(blob)
    return {p for p, typ, *_ in paths}

def ipsw_frameworks_with_info(ipsw_root):
    out = []
    for sub in ('System/Library/PrivateFrameworks', 'System/Library/Frameworks'):
        d = os.path.join(ipsw_root, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.endswith('.framework'):
                continue
            fw = os.path.join(d, name)
            if os.path.isfile(os.path.join(fw, 'Info.plist')):
                out.append('/' + sub + '/' + name)
    return out

def main():
    img = sys.argv[1]
    ipsw = sys.argv[2] if len(sys.argv) > 2 else '/tmp/apfs-mnt/root'
    present = lapisfs_path_set(img)
    missing = []
    for fw in ipsw_frameworks_with_info(ipsw):
        info = fw + '/Info.plist'
        if info not in present and fw not in present:
            missing.append(fw)
    print(f'missing_frameworks_with_Info.plist: {len(missing)}')
    for fw in missing:
        print(fw)

if __name__ == '__main__':
    main()
