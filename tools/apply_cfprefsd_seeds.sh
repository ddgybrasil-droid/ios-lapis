#!/bin/bash
# Seed managed-preferences paths + cfpreferences system group for cfprefsd.
# boot106: cfprefsd burns CPU; CF strings point at managed-prefs container + shm.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?image}
ADD=(python3 "$ROOT/tools/lapisfs_add.py" "$IMG")
DIRS=(
  "private/var/Managed Preferences"
  "private/var/Managed Preferences/mobile"
  "private/var/Managed Preferences/root"
  "Library/Managed Preferences"
  "Library/Managed Preferences/mobile"
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.cfpreferences.managed
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.cfpreferences.managed/Library
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.cfpreferences.managed/Library/Preferences
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.cfpreferences.managed/Library/Managed Preferences
)
for d in "${DIRS[@]}"; do ADD+=(--dir "$d"); done
"${ADD[@]}"
echo "apply_cfprefsd_seeds: done (${#DIRS[@]} dirs)"
