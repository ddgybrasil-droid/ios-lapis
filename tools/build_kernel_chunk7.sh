#!/bin/sh
# Reproduce Image.pth12 (chunk 7) from Image.pth5 and apply the rootfs symlinks.
set -e; T=/workspace/tpro-fix; R=$(dirname "$0")
xnu/lapis-ramfs/build.sh 2>/dev/null || "$R/../xnu/lapis-ramfs/build.sh"
python3 "$R/patch_kernel_ramfs.py" $T/Image.pth5 $T/Image.pth11.tmp          # ramfs + lapisfs readdir/readlink/vtype
python3 "$R/patch_image_adt.py" $T/Image.pth11.tmp $T/Image.pth11 /chosen ephemeral-storage u32:1
rm -f $T/Image.pth11.tmp
python3 "$R/patch_kernel_bootarg_add.py" $T/Image.pth11 $T/Image.pth12 launchd_unsecure_cache=1
# rootfs (in place, on a copy):
# python3 $R/lapisfs_symlink.py IMG /var=private/var /usr/share/zoneinfo=/var/db/timezone/zoneinfo \
#   /private/var/db/timezone/zoneinfo=/usr/share/zoneinfo.default \
#   /private/var/db/timezone/localtime=/var/db/timezone/zoneinfo/America/Los_Angeles
# chunk 8 (rootfs, in place):
# python3 $R/lapisfs_replace.py IMG /usr/lib/dyld <IPSW original dyld>   # undo dyld PAC strip (not needed)
# python3 $R/dsc_cd_rehash.py IMG --write                                 # fix DSC CodeDirectory slots
