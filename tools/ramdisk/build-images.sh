#!/bin/bash
# Rebuild lapisfs root images. Inputs extracted from IPSW 23F77 (iPad16,3) into $X
# (see BRINGUP-NOTES "s11 rootfs"): os/ = OS volume files, sysos/ = Cryptex1,SystemOS files.
set -e; cd "$(dirname "$0")"
X=${X:-/workspace/ios-lapis/ipsw/extract}; RD=${RD:-/workspace/ios-lapis/rd}; mkdir -p $RD
M="python3 mklapisfs.py"; DIRS="--dir /dev --dir /private/var --dir /private/etc --dir /tmp"
$M $RD/placeholder.lfs /sbin/launchd=launchd.macho $DIRS
$M $RD/hw-min-23F77.lfs /sbin/launchd=$X/os/sbin/launchd /usr/lib/dyld=$X/os/usr/lib/dyld $DIRS --dir /System/Library/Caches/com.apple.dyld
C=/System/Library/Caches/com.apple.dyld
$M $RD/rootfs-23F77.lfs /sbin/launchd=$X/os/sbin/launchd /usr/lib/dyld=$X/os/usr/lib/dyld \
  $C=$X/sysos$C /private/preboot/Cryptexes/OS$C=@$C/ /System/Cryptexes/OS$C=@$C/ $DIRS
