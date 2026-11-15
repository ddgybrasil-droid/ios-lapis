#!/bin/bash
# Apply IPSW 23F77 CoreServices / MobileCoreServices / CoreServicesStore
# on-disk resources (Info.plist + loctables + _CodeSignature; no Mach-O)
# onto an EXISTING fullroot image IN PLACE (lapisfs_add keeps existing).
#
# boot62: trampolines cleared; SpringBoard stays up; lsd respawn loop with
# xpcproxy SIGABRT. NAMEI shows MobileCoreServices LaunchServices prefs and
# IOMFB_FDR_Loader looking up CoreServices*.framework — same CFBundle
# Info.plist class as boot59 CFCheck FAR=0x8.
#
# usage: apply_coreservices_frameworks.sh FULLROOT_IMG [METADATA.tsv]
set -euo pipefail
IMG=${1:?need path to fullroot image}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE="$ROOT/rootfs/ipsw-coreservices"
CS="$BASE/CoreServices.framework"
MCS="$BASE/MobileCoreServices.framework"
CSS="$BASE/CoreServicesStore.framework"
test -f "$CS/Info.plist"
test -f "$MCS/Info.plist"
test -f "$CSS/Info.plist"
test ! -e "$CS/CoreServices"
test ! -e "$MCS/MobileCoreServices"
test ! -e "$CSS/CoreServicesStore"

python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "System/Library/Frameworks/CoreServices.framework=$CS" \
  "System/Library/Frameworks/MobileCoreServices.framework=$MCS" \
  "System/Library/PrivateFrameworks/CoreServicesStore.framework=$CSS" \
  --dir private/var/mobile/Library/Preferences/com.apple.MobileCoreServices \
  --dir private/var/db/lsd

if [ -n "${2:-}" ]; then
  python3 "$ROOT/tools/lapisfs_owners.py" "$IMG" "$2" --modes /private/var
else
  echo "NOTE: skipped lapisfs_owners (pass METADATA.tsv as \$2 to keep /private/var/mobile 501:501)"
fi
echo "done: $IMG (+ CoreServices/MobileCoreServices/CoreServicesStore on-disk resources)"
