# ios-lapis / lapis-pthread deliverables

Patches: `patches/` = `git format-patch afc7b33..lapis-pthread` (local branch, not pushed).

Kernels (`kernel/` / GitHub release `lapis-pthread-pth18`, plus local pth19):
- Image.ipados.lapis_pthread.amfi-iomedia-vmm-owners (pth18) 6194f1bb... — release lapis-pthread-pth18 (boot55).
- Image.ipados.lapis_pthread.amfi-iomedia-vmm-sandbox (pth19) fbf2658a... — pth18 + lapis_sandbox MAC policy (chunk 17). Build: `xnu/lapis-ramfs/build.sh` then `tools/patch_kernel_ramfs_replace.py pth18 pth19`.
- Image.ipados.lapis_pthread       c34fe621...  = qemu/Image.ipados + xnu/lapis_pthread/build.sh + tools/patch_kernel_lapis_pthread.py
- Image.ipados.lapis_pthread.boot  a5410683...  = above + tools/patch_kernel_rootdev_bootarg.py (rd=md0 -> rootdev=md0)
                                                  + tools/patch_kernel_lapisfs_rootauth.py + tools/patch_kernel_platform_binary.py
  No dyld/brk patches anywhere.

Rootfs used: /workspace/tpro-fix/fullroot-boot.img (copy; original /workspace/v4-fullroot/ufs-fullroot.img untouched, sha256 819cc92b4ef9463b...).
Built from fullroot-etc.img with tools/lapisfs_add.py (--grow-mb 600, IPSW 23F77 boot subset from 7zz extraction,
--links-file ipsw-boot.links.tsv, --exclude usr/lib/dyld sbin/launchd, mirrors /var=/private/var, zoneinfo) + tools/ipsw_7z_fixups.py.

Logs: `logs/<run>/gdb-events.txt`. Run history: STATUS.md.

Current rootfs: fullroot-mnt2.img = cp --sparse=always fullroot-boot.img fullroot-mnt2.img;
  tools/lapisfs_add.py fullroot-mnt2.img private/etc/fstab=rootfs/fstab etc/fstab=rootfs/fstab   (rootfs/fstab: "/dev/md0 / lapisfs ro,noauto 0 1")
Current tested kernel: kernel/Image.ipados.lapis_pthread.boot (a5410683...). mntupdate-untested = + patch_kernel_lapisfs_update.py (not boot-tested).
Then (chunk 3, in place on fullroot-mnt2.img): 7zz x <IPSW OS dmg 094-54982-099.dmg> System/Library/Filesystems/apfs.fs/apfs_boot_util System/Library/Filesystems/tmpfs.fs/*
  lapisfs_add.py fullroot-mnt2.img System/Library/Filesystems/apfs.fs/apfs_boot_util=... System/Library/Filesystems/tmpfs.fs/mount_tmpfs=...   (list: rootfs/ipsw-extra-files.txt)
Then (chunk 4): tools/reseal_corecrypto_fips_hmac.py fullroot-mnt2.img --write   (recompute corecrypto FIPS HMAC after PAC strip)
