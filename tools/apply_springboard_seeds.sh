#!/bin/bash
# Seed SpringBoard/backboardd runtime dirs that NAMEI'd during boot79.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMG=${1:?image}
ADD=(python3 "$ROOT/tools/lapisfs_add.py" "$IMG")
DIRS=(
  private/var/mobile/Library/SpringBoard
  private/var/mobile/Library/Caches/MappedImageCache
  private/var/mobile/Library/Caches/MappedImageCache/Persistent
  private/var/mobile/Library/Caches/MappedImageCache/Wallpaper
  private/var/mobile/Library/Caches/com.apple.springboard
  private/var/mobile/Library/Caches/com.apple.springboard/WallpaperColorBoxesCache
  private/var/mobile/Library/Wallpaper
  private/var/logs/hidfw-crashlogs
  private/var/containers/Data/System/com.apple.SpringBoard
  private/var/containers/Data/System/com.apple.SpringBoard/Library
  private/var/containers/Data/System/com.apple.SpringBoard/Library/Caches
  private/var/containers/Data/System/com.apple.SpringBoard/Library/Preferences
  private/var/containers/Data/System/com.apple.SpringBoard/Library/Logs
  private/var/containers/Data/System/com.apple.SpringBoard/tmp
  private/var/containers/Data/System/com.apple.backboardd
  private/var/containers/Data/System/com.apple.backboardd/Library
  private/var/containers/Data/System/com.apple.backboardd/Library/Caches
  private/var/containers/Data/System/com.apple.backboardd/Library/Preferences
  private/var/containers/Data/System/com.apple.backboardd/Library/Logs
  private/var/containers/Data/System/com.apple.backboardd/tmp
)
for d in "${DIRS[@]}"; do
  "${ADD[@]}" --dir "$d" || true
done
# Restore marker so SB does not loop on ENOENT forever.
tmp=$(mktemp)
printf 'lapis-bringup' >"$tmp"
python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "private/var/mobile/Library/SpringBoard/SBLastRestoreIdentifier=$tmp"
rm -f "$tmp"
echo "springboard seeds applied to $IMG"
