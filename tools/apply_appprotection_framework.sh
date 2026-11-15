#!/bin/bash
# Apply IPSW 23F77 AppProtection.framework on-disk resources onto an EXISTING
# fullroot image IN PLACE (lapisfs_add keeps existing paths — idempotent).
#
# boot58 stopper: appprotectiond SIGSEGV in ___CFCheckCFInfoPACSignature (FAR=0x8)
# after NAMEI_FAIL on /System/Library/PrivateFrameworks/AppProtection.framework.
# FPCHAIN: _CFBundleGetInfoDictionary <- … <- _CFBundleCopyResourceURL <-
# -[NSBundle URLForResource:…] <- AppProtection / APDServerEntry.
# Without the on-disk bundle, CFBundle has no Info.plist → GetInfoDictionary NULL
# → CFCheck on NULL.
#
# usage: apply_appprotection_framework.sh FULLROOT_IMG [METADATA.tsv]
# Applies in place (no new copy — disk-constrained hosts). Never touches
# the pristine ufs-fullroot.img. Does not invent runtime_state/containers.
# Does not patch CF/AppProtection or insert instruction NOPs.
set -euo pipefail
IMG=${1:?need path to fullroot image (e.g. fullroot-mnt2.icu.img)}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FW="$ROOT/rootfs/ipsw-appprotection/AppProtection.framework"
test -f "$FW/Info.plist"
test -f "$FW/Localizable.loctable"

python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "System/Library/PrivateFrameworks/AppProtection.framework=$FW"

if [ -n "${2:-}" ]; then
  python3 "$ROOT/tools/lapisfs_owners.py" "$IMG" "$2" --modes /private/var
else
  echo "NOTE: skipped lapisfs_owners (pass METADATA.tsv as \$2 to keep /private/var/mobile 501:501)"
fi
echo "done: $IMG (+ AppProtection.framework Info.plist + Localizable.loctable + momd)"
