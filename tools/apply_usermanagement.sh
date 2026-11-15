#!/bin/bash
# Apply UserManagement personas.plist + framework Info.plist resources.
# usage: apply_usermanagement.sh FULLROOT_IMG [METADATA.tsv]
set -euo pipefail
IMG=${1:?need fullroot image}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE="$ROOT/rootfs/ipsw-usermanagement"
test -f "$BASE/UserManagement/com.apple.xpc.launchd.personas.plist"
test -f "$BASE/UserManagement.framework/Info.plist"

python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "System/Library/UserManagement/com.apple.xpc.launchd.personas.plist=$BASE/UserManagement/com.apple.xpc.launchd.personas.plist" \
  "System/Library/PrivateFrameworks/UserManagement.framework=$BASE/UserManagement.framework" \
  "System/Library/PrivateFrameworks/UserManagementLayout.framework=$BASE/UserManagementLayout.framework" \
  --dir private/var/PersonaVolumes \
  --dir private/var/logs

if [ -f "$BASE/FeatureFlags/Domain/UserManagement.plist" ]; then
  python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
    "System/Library/FeatureFlags/Domain/UserManagement.plist=$BASE/FeatureFlags/Domain/UserManagement.plist"
fi

if [ -n "${2:-}" ]; then
  python3 "$ROOT/tools/lapisfs_owners.py" "$IMG" "$2" --modes /private/var
fi
echo "done: $IMG (+ UserManagement personas + frameworks)"
