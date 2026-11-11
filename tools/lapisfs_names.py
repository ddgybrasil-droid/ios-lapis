#!/usr/bin/env python3
"""Read-only GPT + LAPISFS name listing. Does not extract file bytes."""
import argparse, struct

NEEDLES = (
    "kernelcache",
    "AppleImage4",
    "AppleMobileFileIntegrity",
    "trustcache",
    "launchd",
    "dyld",
)

def gpt_partitions(f):
    for bps in (512, 4096):
        f.seek(bps)
        if f.read(8) != b"EFI PART":
            continue
        f.seek(bps)
        hdr = f.read(512)
        ent_lba, nent, esz = struct.unpack_from("<QII", hdr, 72)
        off = ent_lba * bps
        parts = []
        for i in range(nent):
            f.seek(off + i * esz)
            e = f.read(esz)
            if not any(e[:16]):
                continue
            first, last = struct.unpack_from("<QQ", e, 32)
            name = e[56:56 + 72].decode("utf-16le", "replace").split("\x00", 1)[0]
            parts.append((name, first * bps, (last - first + 1) * bps, bps))
        return parts
    raise ValueError("GPT header not found")

def lapisfs_paths(blob):
    magic, nent, version, ent_off, str_off, total = struct.unpack_from("<8sIIQQQ", blob, 0)
    if magic != b"LAPISFS1":
        raise ValueError("not LAPISFS1")
    ents = []
    for i in range(nent):
        parent, typ, mode, data_off, size, name_off, name_len = struct.unpack_from("<IHHQQII", blob, ent_off + 32 * i)
        name = blob[str_off + name_off:str_off + name_off + name_len].decode("utf-8", "replace")
        ents.append((parent, typ, data_off, size, name))
    by_parent = {}
    for i, ent in enumerate(ents):
        by_parent.setdefault(ent[0], []).append(i)
    paths = []
    def rec(idx, path):
        _, typ, data_off, size, _ = ents[idx]
        paths.append((path, typ, data_off, size))
        for child in by_parent.get(idx, []):
            if child == idx:
                continue
            name = ents[child][4]
            rec(child, (path.rstrip("/") + "/" + name) if path != "/" else "/" + name)
    rec(0, "/")
    return version, total, paths

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("image")
    a = p.parse_args()
    with open(a.image, "rb") as f:
        parts = gpt_partitions(f)
        print(f"GPT partitions {len(parts)}")
        for name, off, size, bps in parts:
            print(f"PART {name!r} offset {off} size {size} sector {bps}")
        root = next(part for part in parts if part[0] == "lapis-root")
        f.seek(root[1])
        directory = f.read(1024 * 1024)
    version, total, paths = lapisfs_paths(directory)
    # directory strings live near the header; 1 MiB covers the entry table of this image
    print(f"LAPISFS version {version} entries {len(paths)} declared_total {total}")
    files = [item for item in paths if item[1] == 2]
    dirs = [item for item in paths if item[1] == 1]
    extents = {}
    for path, _, data_off, size in files:
        extents.setdefault((data_off, size), []).append(path)
    aliases = sum(len(v) - 1 for v in extents.values() if len(v) > 1)
    print(f"COUNTS dirs {len(dirs)} files {len(files)} alias_paths {aliases} unique_extents {len(extents)}")
    for needle in NEEDLES:
        hit = sorted(path for path, typ, *_ in paths if needle in path and typ == 2)
        print(f"FILE {needle} count {len(hit)}")
        for path in hit[:8]:
            print(f"  {path}")
        if len(hit) > 8:
            print(f"  ... {len(hit) - 8} more")

if __name__ == "__main__":
    main()
