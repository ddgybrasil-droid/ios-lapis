#!/bin/bash
# Apply IPSW 23F77 CoreFoundation.framework + Foundation.framework on-disk
# resources onto an EXISTING fullroot image IN PLACE (lapisfs_add keeps
# existing paths — idempotent).
#
# boot59 stopper: appprotectiond still SIGSEGV in ___CFCheckCFInfoPACSignature
# (FAR=0x8) after AppProtection.framework Info.plist is present. FPCHAIN:
#   ___CFCheckCFInfoPACSignature
#     <- _CFBundleGetInfoDictionary+24
#     <- __copyBundleLocalizationsFromResources+64
#     <- __copyAppleLocalizations+124
#     <- __copyBundleLocalizationsFromResources+304
#     <- __CFBundleCopyBundleLocalizations+144
#     <- … <- _CFBundleCopyResourceURL <- AppProtection / _APDServerEntry
# ПРОВЕРЕНО: elr unslid 0x1834f48cc = CFCheck+4 (add x8,x0,#8; ldar) with x0=NULL.
# ГИПОТЕЗА: __copyAppleLocalizations resolves com.apple.CoreFoundation and calls
# GetInfoDictionary without NULL-check; without on-disk
# CoreFoundation.framework/Info.plist the CFBundleRef is NULL → FAR=0x8.
# Foundation.framework added the same way (same class of CFBundle consumers).
#
# usage: apply_cf_foundation_frameworks.sh FULLROOT_IMG [METADATA.tsv]
# Never touches the pristine ufs-fullroot.img. No Mach-O/stub invented (IPSW
# has no on-disk CFBundleExecutable for these). No CF/AppProtection patches,
# no instruction NOPs.
set -euo pipefail
IMG=${1:?need path to fullroot image (e.g. fullroot-mnt2.icu.img)}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE="$ROOT/rootfs/ipsw-cf-foundation"
CF="$BASE/CoreFoundation.framework"
FO="$BASE/Foundation.framework"
test -f "$CF/Info.plist"
test -f "$CF/Error.loctable"
test -f "$FO/Info.plist"
test ! -e "$CF/CoreFoundation"
test ! -e "$FO/Foundation"

# Empty *.lproj dirs from IPSW are omitted from git (no files); Info.plist +
# Error.loctable / Foundation loctables + _CodeSignature are sufficient for
# CFBundleGetInfoDictionary. Optional: host may re-add empty lprojs from IPSW
# with lapisfs_add --dir if a later boot needs them.
python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "System/Library/Frameworks/CoreFoundation.framework=$CF" \
  "System/Library/Frameworks/Foundation.framework=$FO"

if [ -n "${2:-}" ]; then
  python3 "$ROOT/tools/lapisfs_owners.py" "$IMG" "$2" --modes /private/var
else
  echo "NOTE: skipped lapisfs_owners (pass METADATA.tsv as \$2 to keep /private/var/mobile 501:501)"
fi
echo "done: $IMG (+ CoreFoundation.framework + Foundation.framework on-disk resources)"
