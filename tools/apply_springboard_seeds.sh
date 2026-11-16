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
  private/var/mobile/Library/Caches/MappedImageCache/Session
  private/var/mobile/Library/Caches/MappedImageCache/Temporary
  private/var/mobile/Library/Caches/MappedImageCache/Icons
  private/var/mobile/Library/Caches/com.apple.springboard
  private/var/mobile/Library/Caches/com.apple.springboard/WallpaperColorBoxesCache
  private/var/mobile/Library/Wallpaper
  private/var/logs/hidfw-crashlogs
  System/Library/HIDPlugins
  System/Library/HIDPlugins/SessionFilters
  System/Library/HIDPlugins/ServicePlugins
  System/Library/HIDPlugins/ConnectionPlugins
  Library/Apple/System/Library/HIDPlugins
  Library/Apple/System/Library/HIDPlugins/SessionFilters
  Library/Apple/System/Library/HIDPlugins/ServicePlugins
  Library/Apple/System/Library/HIDPlugins/ConnectionPlugins
  AppleInternal/Library/HIDPlugins
  AppleInternal/Library/HIDPlugins/SessionFilters
  AppleInternal/Library/HIDPlugins/ServicePlugins
  AppleInternal/Library/HIDPlugins/ConnectionPlugins
  # boot146: BB NAMEI's /usr/appleinternal/lib/HIDPlugins (not AppleInternal/Library).
  usr/appleinternal/lib/HIDPlugins
  usr/appleinternal/lib/HIDPlugins/SessionFilters
  usr/appleinternal/lib/HIDPlugins/ServicePlugins
  usr/appleinternal/lib/HIDPlugins/ConnectionPlugins
  System/Library/Preferences/Logging/Subsystems
  Library/Preferences/Logging/Subsystems
  private/var/preferences
  private/var/mobile/Library/Preferences
  private/var/Managed Preferences/mobile
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
# boot55: BB reaches HID after ioHID.plist NAMEI misses — seed empty plists so
# prefs path does not thrash once RB handshake lets BB past RunningBoard.
iohid=$(mktemp)
printf '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0">\n<dict/>\n</plist>\n' >"$iohid"
python3 "$ROOT/tools/lapisfs_add.py" "$IMG" \
  "private/var/mobile/Library/SpringBoard/SBLastRestoreIdentifier=$tmp" \
  "private/var/mobile/Library/Preferences/com.apple.iohid.plist=$iohid" \
  "private/var/preferences/com.apple.iohid.plist=$iohid" \
  "private/var/Managed Preferences/mobile/com.apple.iohid.plist=$iohid" \
  "private/var/Managed Preferences/com.apple.iohid.plist=$iohid" \
  "System/Library/Preferences/Logging/Subsystems/com.apple.iohid.plist=$iohid" \
  "Library/Preferences/Logging/Subsystems/com.apple.iohid.plist=$iohid" \
  "System/Library/Preferences/Logging/Subsystems/com.apple.runningboard.plist=$iohid" \
  "System/Library/Preferences/Logging/Subsystems/com.apple.coreanimation.plist=$iohid" \
  "Library/Preferences/Logging/Subsystems/com.apple.coreanimation.plist=$iohid" \
  "System/Library/Preferences/Logging/Subsystems/com.apple.SpringBoard.plist=$iohid" \
  "Library/Preferences/Logging/Subsystems/com.apple.SpringBoard.plist=$iohid"
rm -f "$tmp" "$iohid"
echo "springboard seeds applied to $IMG"
