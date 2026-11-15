#!/bin/bash
# Apply IPSW 23F77 ContainerManagerCommon.framework resources onto a COPY of fullroot-mnt2.
# usage: apply_mcm_framework.sh FULLROOT_MNT2_COPY [METADATA.tsv]
# Does not touch the pristine ufs-fullroot.img. Does not invent runtime_state or /private/var/containers.
set -euo pipefail
IMG=${1:?need path to fullroot-mnt2 COPY}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FW="$ROOT/rootfs/ipsw-mcm/ContainerManagerCommon.framework"
test -f "$FW/User.plist" && test -f "$FW/System.plist"
python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "System/Library/PrivateFrameworks/ContainerManagerCommon.framework=$FW"
if [ -f "$ROOT/rootfs/ipsw-mcm/LoggingSubsystems/com.apple.containermanager.plist" ]; then
  python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
    "System/Library/Preferences/Logging/Subsystems/com.apple.containermanager.plist=$ROOT/rootfs/ipsw-mcm/LoggingSubsystems/com.apple.containermanager.plist" || true
fi
if [ -f "$ROOT/rootfs/ipsw-mcm/FeatureFlags/containermanagerd.plist" ]; then
  python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
    "System/Library/FeatureFlags/Domain/containermanagerd.plist=$ROOT/rootfs/ipsw-mcm/FeatureFlags/containermanagerd.plist" || true
fi
if [ -n "${2:-}" ]; then
  python3 "$ROOT/tools/lapisfs_owners.py" "$IMG" "$2" --modes /private/var
else
  echo "NOTE: skipped lapisfs_owners (pass METADATA.tsv as \$2 to keep /private/var/mobile 501:501)"
fi
echo "done: $IMG"
