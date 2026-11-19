#!/bin/bash
# Seed KRExperiments.framework + Trial NamespaceDescriptor paths that
# runningboardd NAMEI-fails after IOGPU match (boot83).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?image}
IPSW=${2:-/tmp/apfs-mnt/root}
KF="$IPSW/System/Library/PrivateFrameworks/KRExperiments.framework"
test -d "$KF"
test -f "$KF/default_factors_COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.pb"

GMPOWER_PB="$KF/default_factors_COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.pb"
GMPOWER_ND="$KF/NamespaceDescriptor.COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.plist"
DELAY_PB="$KF/default_factors_COREOS_VM_OBJECT_COPY_DELAYED_NO_WAIT.pb"
DELAY_ND="$KF/NamespaceDescriptor.COREOS_VM_OBJECT_COPY_DELAYED_NO_WAIT.plist"

ARGS=(
  --dir System/Library/PrivateFrameworks/KRExperiments.framework
  --dir System/Library/PrivateFrameworks/KRExperiments.framework/Resources
  --dir System/Library/PrivateFrameworks/KRExperiments.framework/_CodeSignature
  --dir System/Library/Trial
  --dir System/Library/Trial/NamespaceDescriptors
  --dir private/var/mobile/Library/Trial
  --dir private/var/mobile/Library/Trial/NamespaceDescriptors
  --dir private/var/mobile/Library/Trial/NamespaceDescriptors/v2
  --dir private/var/mobile/Library/Trial/NamespaceDescriptors/experiment
  --dir private/var/mobile/Library/Trial/NamespaceDescriptors/devOverride
  --dir private/var/mobile/Library/Trial/NamespaceDescriptors/Projects
  --dir private/var/mobile/Library/Trial/NamespaceDescriptors/Projects/227
  "System/Library/PrivateFrameworks/KRExperiments.framework/Info.plist=$KF/Info.plist"
  "System/Library/PrivateFrameworks/KRExperiments.framework/default_factors_COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.pb=$GMPOWER_PB"
  "System/Library/PrivateFrameworks/KRExperiments.framework/Resources/default_factors_COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.pb=$GMPOWER_PB"
  "System/Library/PrivateFrameworks/KRExperiments.framework/NamespaceDescriptor.COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.plist=$GMPOWER_ND"
  "System/Library/PrivateFrameworks/KRExperiments.framework/default_factors_COREOS_VM_OBJECT_COPY_DELAYED_NO_WAIT.pb=$DELAY_PB"
  "System/Library/PrivateFrameworks/KRExperiments.framework/NamespaceDescriptor.COREOS_VM_OBJECT_COPY_DELAYED_NO_WAIT.plist=$DELAY_ND"
  "System/Library/PrivateFrameworks/KRExperiments.framework/com.apple.Trial.NamespaceDescriptor.2010.plist=$KF/com.apple.Trial.NamespaceDescriptor.2010.plist"
  "System/Library/PrivateFrameworks/KRExperiments.framework/com.apple.coreos.sysperf.KRExperiments.Trial.Default.pb=$KF/com.apple.coreos.sysperf.KRExperiments.Trial.Default.pb"
  "System/Library/PrivateFrameworks/KRExperiments.framework/KnownFactors.COREOS_FAST_PREWARMING.plist=$KF/KnownFactors.COREOS_FAST_PREWARMING.plist"
  "System/Library/PrivateFrameworks/KRExperiments.framework/_CodeSignature/CodeResources=$KF/_CodeSignature/CodeResources"
  "System/Library/Trial/NamespaceDescriptors/NamespaceDescriptor.COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.plist=$GMPOWER_ND"
  "System/Library/Trial/NamespaceDescriptors/NamespaceDescriptor.COREOS_VM_OBJECT_COPY_DELAYED_NO_WAIT.plist=$DELAY_ND"
  "private/var/mobile/Library/Trial/NamespaceDescriptors/experiment/NamespaceDescriptor.COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.plist=$GMPOWER_ND"
  "private/var/mobile/Library/Trial/NamespaceDescriptors/devOverride/NamespaceDescriptor.COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS.plist=$GMPOWER_ND"
)

# Optional extras if present on the IPSW root.
for extra in \
  "$KF/default_factors_COREOS_GMPOWER_VM_TUNING_PAGE_SHORTAGE_THRESHOLDS_fbs.bin" \
  "$IPSW/System/Library/Trial/AllowedFactorsAndDefaults" \
  ; do
  if [ -f "$extra" ]; then
    rel=${extra#"$IPSW/"}
    ARGS+=("$rel=$extra")
  elif [ -d "$extra" ]; then
    rel=${extra#"$IPSW/"}
    ARGS+=("$rel=$extra")
  fi
done

# Minimal empty markers so open() does not ENOENT-loop forever on subject/treatments.
EMPTY=$(mktemp)
: > "$EMPTY"
trap 'rm -f "$EMPTY"' EXIT
ARGS+=(
  "private/var/mobile/Library/Trial/NamespaceDescriptors/subject.data=$EMPTY"
  "private/var/mobile/Library/Trial/NamespaceDescriptors/Projects/227/treatmentsV2.data=$EMPTY"
  "private/var/mobile/Library/Trial/NamespaceDescriptors/v2/activeFactorProviders.plplist=$EMPTY"
  "private/var/mobile/Library/Trial/NamespaceDescriptors/v2/activeLowLevelFactors.plist=$EMPTY"
)

python3 "$ROOT/tools/lapisfs_add.py" "$IMG" "${ARGS[@]}"
echo "KRExperiments + Trial seeds applied to $IMG"
