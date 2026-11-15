#!/bin/bash
# Seed LaunchServices FeatureFlags + container/lsd runtime dirs for lsd wall.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?image}
SEED=$ROOT/rootfs/ipsw-lsd-seeds
ADD=(python3 "$ROOT/tools/lapisfs_add.py" "$IMG")
DIRS=(
  private/var/db/lsd
  private/var/containers
  private/var/containers/Data
  private/var/containers/Data/System
  private/var/containers/Shared
  private/var/containers/Shared/SystemGroup
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.lsd.iconscache
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.lsd.iconscache/Library
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.lsd.iconscache/Library/Caches
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.lsd.iconscache/Library/Caches/com.apple.IconsCache
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles
  private/var/containers/Shared/SystemGroup/systemgroup.com.apple.configurationprofiles/Library/ConfigurationProfiles/PublicInfo
  private/var/containers/Bundle
  private/var/containers/Bundle/Application
  private/var/containers/Bundle/PluginKitPlugin
  private/var/containers/Bundle/VPNPlugin
  private/var/containers/Bundle/Framework
  private/var/containers/Temp
  private/var/root/Library/Backup
  private/var/root/Library/Backup/SystemContainers
  private/var/root/Library/Backup/SystemContainers/Data
  private/var/root/Library/Backup/SystemContainers/Shared
  private/var/root/Library/MobileContainerManager
  private/var/root/Library/MobileContainerManager/System
  private/var/root/Library/Caches/com.apple.containermanagerd
  private/var/root/Library/Caches/com.apple.containermanagerd/System
  private/var/root/Library/Caches/com.apple.containermanagerd/System/Temp
  private/var/root/Library/Caches/com.apple.containermanagerd/System/Staging
  private/var/installd/Library/Caches/com.apple.containermanagerd
  private/var/mobile/Containers/Data/VPNPlugin
  System/Library/FeatureFlags/Domain
  System/Library/FeatureFlags/Unified/Domain
)
for d in "${DIRS[@]}"; do ADD+=(--dir "$d"); done
"${ADD[@]}"
mapfile -t FILES < <(find "$SEED" -type f ! -name .gitkeep | sort)
ARGS=(python3 "$ROOT/tools/lapisfs_add.py" "$IMG")
for f in "${FILES[@]}"; do
  rel=${f#"$SEED"/}
  ARGS+=("$rel=$f")
done
"${ARGS[@]}"
echo "apply_lsd_seeds: done (${#FILES[@]} files)"
