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
# chunk 9: Image.pth13 = patch_kernel_ramfs.py pth5 -> patch_kernel_amfi.py -> patch_image_adt.py /chosen ephemeral-storage u32:1
#          -> patch_kernel_bootarg_add.py launchd_unsecure_cache=1
# chunk 10: Image.pth14 = same chain as pth13 with lapis_iomedia in the ramfs blob (build.sh links lapis_ramfs/amfi/iomedia)
# chunk 11: Image.pth15 = pth14 -> patch_image_adt.py defaults vmm-present u32:1 (QEMU is a VM: kern.hv_vmm_present=1,
#           watchdogd takes its "virtual machine and no watchdog KEXT -> exit 0" path) -> patch_kernel_bootarg_add.py panic_on_crash_disabled=1
#           (xnu kern_exit.c tunable: launchd _PanicOnCrash services no longer panic the kernel when they crash)
# rootfs: lapisfs_add.py IMG --dir /private/var/log --dir /private/var/log/com.apple.xpc.launchd (ramfs template)
#         SystemOS cryptex non-DSC content -> /System/Cryptexes/OS (see tools/decmpfs_decode_tree.py docstring)
#         missing daemon binaries (launchd "binary is missing" -> xpcproxy exit 78): 259 LaunchDaemons programs (+ their .app/.xpc/.appex/.bundle
#         bundles, no .lproj) extracted from the OS dmg 094-54982-099.dmg with 7zz @list -> lapisfs_add.py --grow-mb 384 /System=.. /Applications=..;
#         AppOS cryptex 094-54871-103.dmg (all, decmpfs decoded) -> /System/Cryptexes/App; lapisfs_fix_macho_modes.py IMG /System/Cryptexes/OS /System/Cryptexes/App
# chunk 12: Image.pth16 = pth15 -> patch_kernel_bootarg_add.py ml-timeout-global-scale=16 (xnu kern/machine.c machine-timeout scale:
#           lock-panic 0xc00000 ticks (~0.5 s) x16 under TCG; boot36/42 "Mutex interlock timeout" in lck_mtx_ilk_lock_contended)
# rootfs: 37 launchd-cache programs still missing (from /System/Library/xpc/launchd.plist, incl. /Applications/*.app UI scene services)
#         + bundles (no .lproj) from the OS dmg -> lapisfs; OS dmg private/var skeleton (minus staged_system_apps, 220 entries) -> /private/var template
