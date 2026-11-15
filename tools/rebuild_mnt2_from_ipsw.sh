#!/bin/bash
# Rebuild a bring-up fullroot-mnt2.img on this cloud VM from:
#   - pristine ufs-fullroot 23F77 (DSC + launchd + dyld), sha256 819cc92b…
#   - IPSW OS APFS mount (apfs-fuse of 094-54982-099.dmg)
#   - vendored MCM / ICU / AppProtection / CF+Foundation resources
#
# This is NOT a byte-identical copy of the host's /workspace/tpro-fix/fullroot-mnt2*.img
# (that image never lived on this VM; it was built iteratively on the emulator host / Grok
# bring-up machine). Goal: enough userspace to reach SpringBoard/appprotectiond so boot60
# can be run here.
#
# usage:
#   tools/rebuild_mnt2_from_ipsw.sh [OUT_IMG] [IPSW_ROOT] [PRISTINE_IMG]
# defaults:
#   OUT=/tmp/fullroot-mnt2.img
#   IPSW_ROOT=/tmp/apfs-mnt/root
#   PRISTINE=/tmp/ios-lapis-ufs-fullroot-23F77.img
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-/tmp/fullroot-mnt2.img}
IPSW=${2:-/tmp/apfs-mnt/root}
PRISTINE=${3:-/tmp/ios-lapis-ufs-fullroot-23F77.img}
STAGE=${STAGE:-/tmp/mnt2-rebuild-stage}
GROW_MB=${GROW_MB:-2560}

test -f "$PRISTINE"
test -d "$IPSW/sbin"
test -f "$IPSW/System/Library/xpc/launchd.plist"

echo "== copy pristine -> $OUT (sparse) =="
rm -f "$OUT"
cp --sparse=always "$PRISTINE" "$OUT"

echo "== stage selective IPSW trees =="
rm -rf "$STAGE"
mkdir -p "$STAGE"

# Collect LaunchDaemon Program paths + companion bundles (no .lproj).
python3 - "$IPSW" "$STAGE" <<'PY'
import os, plistlib, shutil, sys
ipsw, stage = sys.argv[1], sys.argv[2]
ld = os.path.join(ipsw, 'System/Library/LaunchDaemons')
progs = set()
for fn in os.listdir(ld):
    if not fn.endswith('.plist'):
        continue
    try:
        d = plistlib.load(open(os.path.join(ld, fn), 'rb'))
    except Exception:
        continue
    p = d.get('Program')
    if isinstance(p, str) and p.startswith('/'):
        progs.add(p)
    pa = d.get('ProgramArguments')
    if isinstance(pa, list) and pa and isinstance(pa[0], str) and pa[0].startswith('/'):
        progs.add(pa[0])

# Always include SpringBoard + backboardd + key boot tasks.
extra = [
    '/System/Library/CoreServices/SpringBoard.app/SpringBoard',
    '/usr/libexec/backboardd',
    '/usr/libexec/appprotectiond',
    '/usr/libexec/containermanagerd',
    '/usr/libexec/containermanagerd_system',
    '/usr/libexec/lsd',
    '/System/Library/Filesystems/apfs.fs/apfs_boot_util',
    '/System/Library/Filesystems/tmpfs.fs/mount_tmpfs',
]
for p in extra:
    progs.add(p)

BUNDLE_SUFS = ('.app', '.framework', '.xpc', '.appex', '.bundle', '.migrator', '.service')

def bundle_root(abs_path: str):
    # /…/Foo.app/Bar -> /…/Foo.app
    parts = abs_path.strip('/').split('/')
    acc = []
    for i, part in enumerate(parts):
        acc.append(part)
        for suf in BUNDLE_SUFS:
            if part.endswith(suf):
                return '/' + '/'.join(acc)
    return None

def copy_file(rel):
    src = ipsw + rel
    dst = stage + rel
    if not os.path.lexists(src):
        return
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.islink(src):
        if not os.path.lexists(dst):
            os.symlink(os.readlink(src), dst)
        return
    if os.path.isdir(src):
        return
    if not os.path.exists(dst):
        shutil.copy2(src, dst, follow_symlinks=False)

def copy_tree_filtered(src_dir, dst_dir):
    if not os.path.isdir(src_dir):
        return
    for dp, dns, fns in os.walk(src_dir):
        # drop localization dirs
        dns[:] = [d for d in dns if not d.endswith('.lproj')]
        rel = os.path.relpath(dp, src_dir)
        out = dst_dir if rel == '.' else os.path.join(dst_dir, rel)
        os.makedirs(out, exist_ok=True)
        for fn in fns:
            if fn.endswith('.lproj'):
                continue
            s = os.path.join(dp, fn)
            d = os.path.join(out, fn)
            if os.path.lexists(d):
                continue
            if os.path.islink(s):
                os.symlink(os.readlink(s), d)
            elif os.path.isfile(s):
                shutil.copy2(s, d, follow_symlinks=False)

# Copy LaunchDaemon plists
copy_tree_filtered(ld, stage + '/System/Library/LaunchDaemons')

bundles = set()
for p in sorted(progs):
    copy_file(p)
    b = bundle_root(p)
    if b:
        bundles.add(b)

# Companion support dirs next to some frameworks
for b in sorted(bundles):
    copy_tree_filtered(ipsw + b, stage + b)

print(f'staged programs={len(progs)} bundles={len(bundles)}')
PY

# Whole trees that are small / required
copy_tree() {
  local rel=$1
  echo "  tree $rel"
  mkdir -p "$STAGE/$(dirname "$rel")"
  if [ -d "$IPSW/$rel" ]; then
    python3 - "$IPSW/$rel" "$STAGE/$rel" <<'PY'
import os, shutil, sys
src, dst = sys.argv[1], sys.argv[2]
def ignore(directory, names):
    return [n for n in names if n.endswith('.lproj')]
if os.path.exists(dst):
    # merge
    for dp, dns, fns in os.walk(src):
        dns[:] = [d for d in dns if not d.endswith('.lproj')]
        rel = os.path.relpath(dp, src)
        out = dst if rel == '.' else os.path.join(dst, rel)
        os.makedirs(out, exist_ok=True)
        for fn in fns:
            s = os.path.join(dp, fn); d = os.path.join(out, fn)
            if os.path.lexists(d):
                continue
            if os.path.islink(s):
                os.symlink(os.readlink(s), d)
            elif os.path.isfile(s):
                shutil.copy2(s, d, follow_symlinks=False)
else:
    shutil.copytree(src, dst, symlinks=True, ignore=ignore)
PY
  fi
}

copy_tree sbin
copy_tree bin
copy_tree usr/bin
copy_tree usr/sbin
copy_tree usr/lib
copy_tree usr/libexec
copy_tree private/etc
copy_tree System/Library/xpc
copy_tree System/Library/Filesystems
copy_tree System/Library/CoreServices
# private/var template: keep structure, skip huge staged_system_apps if present
echo "  tree private/var (filtered)"
python3 - "$IPSW/private/var" "$STAGE/private/var" <<'PY'
import os, shutil, sys
src, dst = sys.argv[1], sys.argv[2]
skip_dirs = {'staged_system_apps', 'MobileAsset', 'MobileSoftwareUpdate', 'containers'}
def ignore(directory, names):
    out = [n for n in names if n.endswith('.lproj') or n in skip_dirs]
    return out
if os.path.isdir(src):
    shutil.copytree(src, dst, symlinks=True, ignore=ignore, dirs_exist_ok=True)
PY

# Applications (no .lproj) — needed for UI scene services
echo "  tree Applications (no lproj)"
python3 - "$IPSW/Applications" "$STAGE/Applications" <<'PY'
import os, shutil, sys
src, dst = sys.argv[1], sys.argv[2]
def ignore(directory, names):
    return [n for n in names if n.endswith('.lproj')]
if os.path.isdir(src):
    shutil.copytree(src, dst, symlinks=True, ignore=ignore, dirs_exist_ok=True)
PY

# fstab from repo (noauto + ramdisk /private/var). Keep under private/etc only —
# /etc must be a symlink to private/etc (IPSW layout) so getpwnam finds passwd.
mkdir -p "$STAGE/private/etc"
cp "$ROOT/rootfs/fstab" "$STAGE/private/etc/fstab"
# DarwinDirectory: mobile/root users (launchd dies without getpwnam("mobile"))
copy_tree System/Library/DarwinDirectory
copy_tree private/var/db/DarwinDirectory

# mount_tmpfs mirror under /sbin (host may already have it via LaunchDaemon staging)
mkdir -p "$STAGE/sbin"
SRC_MT="$STAGE/System/Library/Filesystems/tmpfs.fs/mount_tmpfs"
if [ -e "$SRC_MT" ] && [ ! -e "$STAGE/sbin/mount_tmpfs" ]; then
  cp -a "$SRC_MT" "$STAGE/sbin/mount_tmpfs"
fi

echo "== stage size =="
du -sh "$STAGE"

echo "== lapisfs_add (grow ${GROW_MB} MiB) =="
# Exclude dyld/launchd already in pristine
python3 "$ROOT/tools/lapisfs_add.py" "$OUT" --grow-mb "$GROW_MB" \
  --exclude usr/lib/dyld \
  --exclude sbin/launchd \
  --exclude System/Library/Caches/com.apple.dyld \
  --exclude System/Cryptexes/OS/System/Library/Caches/com.apple.dyld \
  --exclude private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld \
  \
  "sbin=$STAGE/sbin" \
  "bin=$STAGE/bin" \
  "usr=$STAGE/usr" \
  "private=$STAGE/private" \
  "System=$STAGE/System" \
  "Applications=$STAGE/Applications"

echo "== timezone parents + symlinks =="
# zoneinfo.default comes with apply_icu; ensure parents exist before linking.
python3 "$ROOT/tools/lapisfs_add.py" "$OUT" \
  --dir usr/share \
  --dir private/var/db \
  --dir private/var/db/timezone || true
python3 "$ROOT/tools/lapisfs_symlink.py" "$OUT" \
  /etc=private/etc \
  /var=private/var \
  /usr/share/zoneinfo=/var/db/timezone/zoneinfo \
  /private/var/db/timezone/zoneinfo=/usr/share/zoneinfo.default \
  /private/var/db/timezone/localtime=/var/db/timezone/zoneinfo/America/Los_Angeles || true

echo "== apply MCM / ICU / AppProtection / CF+Foundation =="
# Generate a minimal METADATA.tsv from staged modes (uid/gid/mode)
META=/tmp/mnt2-METADATA.tsv
python3 - "$STAGE" "$META" <<'PY'
import os, sys
stage, out = sys.argv[1], sys.argv[2]
rows = []
for dp, dns, fns in os.walk(stage):
    for name in dns + fns:
        p = os.path.join(dp, name)
        rel = '/' + os.path.relpath(p, stage)
        try:
            st = os.lstat(p)
        except OSError:
            continue
        rows.append(f"{rel}\t{st.st_mode & 0o7777:04o}\t{st.st_uid}\t{st.st_gid}")
open(out, 'w').write('\n'.join(rows) + '\n')
print('metadata rows', len(rows))
PY

bash "$ROOT/tools/apply_mcm_framework.sh" "$OUT" "$META"
bash "$ROOT/tools/apply_icu_data.sh" "$OUT" "$META"
bash "$ROOT/tools/apply_appprotection_framework.sh" "$OUT" "$META"
bash "$ROOT/tools/apply_cf_foundation_frameworks.sh" "$OUT" "$META"
bash "$ROOT/tools/apply_coreservices_frameworks.sh" "$OUT" "$META"

# log dirs for launchd + ramdisk mountpoints (mount -P 2 looks for /.b/8/
# then /private/system_data before template=/private/var/)
python3 "$ROOT/tools/lapisfs_add.py" "$OUT" \
  --dir private/var/log \
  --dir private/var/log/com.apple.xpc.launchd \
  --dir private/system_data \
  --dir .b \
  --dir .b/8 || true

# libobjc-trampolines.dylib is NOT in the system APFS root (094-54982-099);
# it lives on a sibling restore DMG. SpringBoard/audiomxd dlopen it from
# /usr/lib (and cryptex OS mirror); "not in dyld cache" is expected — on-disk only.
TRAMP="${TRAMP:-$ROOT/bringup/libobjc-trampolines.dylib}"
if [ ! -f "$TRAMP" ]; then
  IPSW_ZIP=${IPSW_ZIP:-/tmp/ipsw23f77/iPad_Pro_M4_26.5_23F77_Restore.ipsw}
  IPSW_BIN=${IPSW_BIN:-/tmp/ipsw}
  if [ -x "$IPSW_BIN" ] && [ -f "$IPSW_ZIP" ]; then
    echo "== extract libobjc-trampolines from IPSW =="
    mkdir -p /tmp/tramp-ipsw
    "$IPSW_BIN" extract --files --pattern '.*libobjc-trampolines.*' \
      -o /tmp/tramp-ipsw "$IPSW_ZIP"
    TRAMP=$(find /tmp/tramp-ipsw -name libobjc-trampolines.dylib | head -1)
  fi
fi
if [ -f "$TRAMP" ]; then
  echo "== add libobjc-trampolines.dylib =="
  python3 "$ROOT/tools/lapisfs_add.py" "$OUT" \
    /usr/lib/libobjc-trampolines.dylib="$TRAMP" \
    /System/Cryptexes/OS/usr/lib/libobjc-trampolines.dylib="$TRAMP"
else
  echo "WARN: libobjc-trampolines.dylib missing (SpringBoard will SIGABRT on dlopen)" >&2
fi

echo "== DSC bring-up patches (host mnt2 parity) =="
# Required for launchd past _container_init / pthread features under TCG.
# DSC bring-up for Image.pth20:
# - container_init skip (Sandbox 0x43 fails under TCG)
# - PAC-strip DSC TEXT + launchd; restore pristine dyld only
# - do NOT force pthread features 0x4000001e: pth20 lapis_pthread returns
#   0x400001df (WORKQ_KEVENT|WORKLOOP); the old #13 mask makes libdispatch
#   DISPATCH_CLIENT_CRASH and --nop-brk1 turns that into FAR=0x4000001e SEGV.
python3 "$ROOT/tools/patch_fullroot_container_init.py" "$OUT"
python3 "$ROOT/tools/patch_fullroot_pac_strip.py" --nop-brk1 "$OUT"
python3 - "$ROOT" "$PRISTINE" "$OUT" <<'PY'
import os, sys, tempfile
root, pristine, out = sys.argv[1:4]
sys.path.insert(0, os.path.join(root, 'tools'))
from lapisfs_names import gpt_partitions, lapisfs_paths

def extract(img, want):
    with open(img, 'rb') as f:
        rootp = next(p for p in gpt_partitions(f) if p[0] == 'lapis-root')
        f.seek(rootp[1]); meta = f.read(32 << 20)
        _, _, paths = lapisfs_paths(meta)
        for p, t, off, sz in paths:
            if p == want and t == 2:
                f.seek(rootp[1] + off)
                return f.read(sz)
    raise SystemExit('missing ' + want)

blob = extract(pristine, '/usr/lib/dyld')
tmp = tempfile.NamedTemporaryFile(delete=False)
tmp.write(blob); tmp.close()
rc = os.system(f"python3 {root}/tools/lapisfs_replace.py {out} /usr/lib/dyld {tmp.name}")
os.unlink(tmp.name)
raise SystemExit(rc >> 8)
PY
python3 "$ROOT/tools/reseal_corecrypto_fips_hmac.py" "$OUT" --write

echo "== DSC CodeDirectory rehash (stale after PAC strip) =="
# Without this, CS_KILL processes (UserEventAgent) die on tainted DSC pages
# (boot32 → boot33). Must run after pac_strip + FIPS reseal.
python3 "$ROOT/tools/dsc_cd_rehash.py" "$OUT" --write

echo "== IPSW owners/modes for /private/var (mobile 501:501) =="
# SpringBoard/backboardd need mobile-owned Library/Caches (boot47→50).
IPSW_META=${IPSW_META:-/tmp/mnt2-IPSW-METADATA.tsv}
python3 - "$IPSW" "$IPSW_META" <<'PY'
import os, stat, sys
ipsw, out = sys.argv[1], sys.argv[2]
rows = []
for dp, dns, fns in os.walk(ipsw):
    for name in dns + fns:
        p = os.path.join(dp, name)
        try:
            st = os.lstat(p)
        except OSError:
            continue
        rel = os.path.relpath(p, ipsw)
        rows.append(f"{rel}\t{stat.filemode(st.st_mode)}\t{st.st_uid}\t{st.st_gid}")
open(out, 'w').write('\n'.join(rows) + '\n')
print('IPSW metadata rows', len(rows))
PY
python3 "$ROOT/tools/lapisfs_owners.py" "$OUT" "$IPSW_META" --modes /private/var

echo "== done: $OUT =="
ls -lh "$OUT"
python3 - <<PY
import sys
sys.path.insert(0, '$ROOT/tools')
from lapisfs_names import gpt_partitions, lapisfs_paths
img='$OUT'
with open(img,'rb') as f:
  root=next(p for p in gpt_partitions(f) if p[0]=='lapis-root')
  f.seek(root[1]); blob=f.read(32<<20)
_,_,paths=lapisfs_paths(blob)
present={p for p, *_ in paths}
need=[
 '/etc/fstab','/private/etc/fstab','/sbin/mount','/sbin/fsck',
 '/usr/libexec/containermanagerd_system','/usr/libexec/appprotectiond',
 '/System/Library/Frameworks/CoreFoundation.framework/Info.plist',
 '/System/Library/Frameworks/Foundation.framework/Info.plist',
 '/System/Library/PrivateFrameworks/AppProtection.framework/Info.plist',
 '/System/Library/PrivateFrameworks/ContainerManagerCommon.framework/User.plist',
 '/usr/share/icu/icudt78l.dat',
 '/System/Library/CoreServices/SpringBoard.app/SpringBoard',
 '/usr/lib/libobjc-trampolines.dylib',
 '/System/Library/Frameworks/CoreServices.framework/Info.plist',
 '/System/Library/Frameworks/MobileCoreServices.framework/Info.plist',
]
print('paths', len(present))
for n in need:
  print(('YES' if n in present else 'NO '), n)
PY
