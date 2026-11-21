#!/bin/bash
# Seed RunningBoard + LifecyclePolicy files NAMEI'd missing in boot80.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?image}
IPSW=${2:-/tmp/apfs-mnt/root}
test -f "$IPSW/System/Library/RunningBoard/runningboardEntitlementsConfiguration.plist"
ARGS=(
  --dir System/Library/RunningBoard
  --dir System/Library/LifecyclePolicy
  --dir System/Library/LifecyclePolicy/DomainAttributes
  "System/Library/RunningBoard/runningboardEntitlementsConfiguration.plist=$IPSW/System/Library/RunningBoard/runningboardEntitlementsConfiguration.plist"
  "System/Library/RunningBoard/default_factors_259.pb=$IPSW/System/Library/RunningBoard/default_factors_259.pb"
  "System/Library/LifecyclePolicy/domains.plist=$IPSW/System/Library/LifecyclePolicy/domains.plist"
)
n=0
for f in "$IPSW/System/Library/LifecyclePolicy/DomainAttributes"/*.plist; do
  [ -f "$f" ] || continue
  ARGS+=("System/Library/LifecyclePolicy/DomainAttributes/$(basename "$f")=$f")
  n=$((n+1))
done
python3 "$ROOT/tools/lapisfs_add.py" "$IMG" "${ARGS[@]}"
echo "runningboard seeds applied ($n DomainAttributes) to $IMG"
