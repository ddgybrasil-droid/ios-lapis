# lapis-pthread bring-up status (Europe/Moscow times)

| run | kernel | rootfs | change | result |
|---|---|---|---|---|
| etc3 | Image.pth | fullroot-etc.img | IPSW /private/etc + /etc mirror | 'mobile' abort gone; namei failures only |
| boot2 | Image.pth | fullroot-boot.img | + IPSW boot subset (LaunchDaemons, sbin, usr/...) | launchd in RESTORE mode (rd=md0) -> HorizonSetup, stall |
| boot3 | Image.pth2 | fullroot-boot.img | cmdline rd=md0 -> rootdev=md0 | panic: rootvp not authenticated |
| boot5-7 | Image.pth3 | fullroot-boot.img | lapisfs FSIOC_KERNEL_ROOTAUTH -> 0 | normal boot tasks; spawns /sbin/fsck; REBOOT 'boot task is not a platform binary (fsck, /sbin/fsck)' |
| boot8/9 | Image.pth4 (dropped) | fullroot-boot.img | OR CS_PLATFORM_BINARY in ubc_cs_blob_add | same reboot; csb flags=0x4000003 but proc csflags not platform |
| boot10 | Image.pth5 | fullroot-boot.img | OR CS_SIGNED|CS_PLATFORM_BINARY in proc_getcsflags | platform reboot GONE: fsck runs, then /sbin/mount spawned (see logs/boot10) |
| boot11 | Image.pth5 | fullroot-boot.img | clean rerun of boot10 (330 s) | fsck spawned+exited; /sbin/mount (mount-phase-1) spawned; ENOENT /etc/fstab and mount_lapisfs (4 helper paths, 4 child exits); REBOOT 'boot task failure: mount-phase-1 - exited due to exit(72)' |

Next blocker: mount-phase-1 exit(72) (EX_OSFILE): no /etc/fstab and no mount_lapisfs helper for the lapisfs root.
| boot12 | Image.pth5 | fullroot-mnt.img (v1) | + /etc/fstab "/dev/md0 / lapisfs ro 0 1" | INVALID: lapisfs_add created duplicate /private,/etc dirs for relative DEST (bug, fixed); same exit(72). New exec hook: mount execs mount_lapisfs in 4 dirs, all ENOENT; SYS_EXIT rval=72 |
| boot13 | Image.pth5 | fullroot-mnt.img (v2) | /etc/fstab "/dev/md0 / lapisfs ro 0 1" (dup bug fixed) | /etc/fstab now found (no ENOENT); mount still remounts / via mount_lapisfs (4x ENOENT) -> exit(72) -> REBOOT mount-phase-1 |
| boot14 | Image.pth5 | fullroot-mnt2.img | /etc/fstab "/dev/md0 / lapisfs ro,noauto 0 1" (root already mounted ro by kernel; lapisfs has no rw mode, so no remount) | mount-phase-1 SYS_EXIT rval=0 (PASSED); next boot task spawned: /usr/libexec/init_data_protection |
| boot14 (cont.) | | | | data-protection: /usr/libexec/init_data_protection exit 0; finish-obliteration: mobile_obliterator exit 0; HorizonSetup spawn ENOENT (not required); mount-phase-2: /sbin/mount -P 2 execs /System/Library/Filesystems/apfs.fs/apfs_boot_util (ENOENT) -> exit(66) -> REBOOT 'boot task failure: mount-phase-2 - exited due to exit(66)' |

Kernel Image.pth6 (= pth5 + tools/patch_kernel_lapisfs_update.py, sha256 fa4aaffb...) built and committed but NOT boot-tested (not needed once fstab uses noauto).

Next blocker: mount-phase-2 exit(66): data volume lookup via apfs_boot_util (APFS); lapisfs root has no APFS data volume. mount strings: "mount: data volume missing, but not required in env: %u" (os_env_type from IODeviceTree) -> candidate fix: DT os_env_type / a /private/var mount that mount -P 2 accepts, or tmpfs /private/var.

## chunk 3 (19:37–) mount-phase-2
Analysis (VERIFIED from disassembly, scratch-helper/mount.dis / abu.dis via mdis.py):
- /sbin/mount -P 2: reads IODeviceTree:/filesystems/fstab (os_env_type), looks up APFS boot container + data volume
  (missing data volume is fatal only when env==1; else "data volume missing, but not required in env"), mounts fstab
  entries with passno==2 (supports type "ramdisk" -> /sbin/mount_tmpfs with size=/template=), then ALWAYS
  execv("/System/Library/Filesystems/apfs.fs/apfs_boot_util", "2"); exec failure -> exit(66) (boot14).
- apfs_boot_util 2 (IPSW binary) -> mount_phase_two: "Boot device lookup" via APFS, data/log volumes by role: needs APFS.
- Kernel has no tmpfs (no tmpfs symbols; tmpfs is a kext on iOS) and no writable fs at all (lapisfs ro, devfs).
=> writable /private/var needs a NEW in-kernel RAM fs (vfs_fsadd, like lapisfs) + something that mounts it in phase 2.
| boot15 | Image.pth5 | fullroot-mnt2.img (+ IPSW apfs_boot_util, tmpfs.fs/mount_tmpfs; added in place, no disk space for another copy) | real /System/Library/Filesystems/apfs.fs/apfs_boot_util | mount -P 2 execs apfs_boot_util -> SYS_EXIT rval=0: mount-phase-2 PASSED. Then MSUEarlyBootTask (spawns /usr/local/bin/snapshottool ENOENT, exit 0), MobileAssetEarlyBootTask, cc_fips_test spawned; no reboot within 330 s (timeout). /private/var still read-only lapisfs (NAMEI_FAIL /private/var/mobile, /private/var/mobile/tmp) |
| boot16 | Image.pth5 | same | 720 s run | reproduces boot15; after cc_fips_test spawn ~5 min of workqueue thread create/terminate churn, then REBOOT 'boot task failure: fips - exited due to exit(230)'; followed by a kernel PANIC during reboot (fmt "%s at pc ... saved state", lr 0xfffffe0007bcf93c) |

Next blocker: fips boot task (/usr/libexec/cc_fips_test) exit(230). Writable /private/var NOT done: needs a new in-kernel RAM fs (no tmpfs/writable fs in this kernel).

## chunk 4 (20:00–) fips
cc_fips_test (disasm scratch-helper/fips.dis): no args -> reads boot-arg `fips_mode` (os_parse_boot_arg), calls libcorecrypto FIPS_POST(mode, dli_fname), exit(result). Mode bits from its option parser: -d 0x4 disable, -F 0x10 force-fail, -N 0x40 no-integrity, -t 0x80 trace.
Hypothesis for exit(230): integrity (HMAC) check of libcorecrypto __TEXT fails because tools/patch_fullroot_pac_strip.py rewrote PAC instructions across the whole dyld shared-cache TEXT.
| boot17 | Image.pth5-fipsN (= pth5 + boot-arg fips_mode=0x40, tools/patch_kernel_bootarg_add.py) | fullroot-mnt2.img | diagnostic: POST without integrity check (KATs still run) | (running) |
| boot17 | Image.pth5-fipsN | fullroot-mnt2.img | fips_mode=0x40 diagnostic | ABORTED early (superseded by the offline HMAC check below) |

HMAC check (tools/reseal_corecrypto_fips_hmac.py, algorithm from apple/corecrypto cc_fips/src/fipspost_get_hmac.c: HMAC-SHA256, zero key, over libcorecrypto __TEXT,__text (va 0x1e2530c00, size 0x874d0, subcache .31) vs __TEXT,__fips_hmacs (va 0x1e25dc668)):
- original /workspace/v4-fullroot/ufs-fullroot.img (read-only): stored ff142ad0…ea2c == computed ff142ad0…ea2c  (algorithm VERIFIED)
- fullroot-mnt2.img (PAC-stripped cache TEXT): stored ff142ad0… != computed 313f41ec493c6643b21b502a782a4f9245a254729beaa783404bcb85f6a4ea5d  -> root cause of fips exit(230) (CCPOST_INTEGRITY_ERROR)
- fix: --write stores the recomputed HMAC in __fips_hmacs of fullroot-mnt2.img (re-check: match True). No fips_mode boot-arg.
| boot18 | Image.pth5 (no fips boot-arg) | fullroot-mnt2.img (resealed) | proper fix: recomputed FIPS HMAC | cc_fips_test SYS_EXIT rval=0 (fips PASSED, line 98386); next boot task keybagd spawned (line 98724) |
| boot18 (cont.) | | | | keybagd spawned, then KERNEL PANIC: synchronous exception (brk #0xbffc assertion) at _vm_sanitize_addr_size+0x1d0 (0xfffffe00074f2d64) <- vm_map_copyin_internal <- ipc_kmsg_copyin_body <- mach_msg2_trap. The assert path is taken when ID_AA64MMFR2_EL1.VARange != 0 (FEAT_LVA); QEMU `-cpu max` always advertises FEAT_LVA (no property to turn it off in QEMU 10.0.13; only `lpa2`). |

Next blocker: kernel assertion in vm_sanitize_addr_size during keybagd's mach_msg OOL copyin, caused by the emulated CPU advertising FEAT_LVA (52-bit VA), which Apple silicon lacks.
Note: fullroot-mnt2.img was resealed IN PLACE (32 bytes in dyld_shared_cache_arm64e.31 __fips_hmacs); reproduce with tools/reseal_corecrypto_fips_hmac.py IMG --write.

## Chunk 5 (QEMU LVA)
Hardware check: Cortex-X4 TRM 102484 r0p3, Cortex-A720 TRM 102530, Cortex-A520 TRM 102517: ID_AA64MMFR2_EL1.VARange[19:16] = 0b0000 (48-bit VA). So SM8650 does NOT have FEAT_LVA -> emulator fix, not kernel.
QEMU 11.0.0 + new `lva` property for -cpu max (repo qemu/qemu-11.0.0-arm-lva-property.patch, qemu/build-qemu11-lva.sh). gdb read of ID_AA64MMFR2_EL1: lva default 0x1221011112011011, lva=off 0x1221011112001011.
| boot | kernel | rootfs | emulator | result | log |
| boot19 | Image.pth5 | fullroot-mnt2.img | qemu11 -cpu max,pauth-impdef=on,lva=off | keybagd SPAWN #11 -> SYS_EXIT 0 (no panic), then REBOOT_KERNEL howto=0 | out/logs/boot19 |
| boot20 | same + console/reboot(2) hooks | same | same | console: "MKB_INIT: Could not open /private/var/keybags/systembag.kb ... FATAL KEYBAG ERROR: failed to load system bag ... REBOOTING INTO RECOVERY MODE"; SYS_REBOOT proc=keybagd | out/logs/boot20 |
Next blocker: keybagd needs /private/var/keybags/systembag.kb (writable /private/var + system keybag; "Could not save value:e00002c7" = AppleKeyStore absent).
| boot21 | Image.pth7 (pth5 + lapis-ramfs "tmpfs") | mnt2 + fstab `tmpfs /private/var tmpfs` | qemu11 lva=off | "LAPIS ramfs: registered tmpfs"; mount execs mount_tmpfs: not found (only in tmpfs.fs/) | out/logs/boot21 |
| boot22 | same | + /sbin/mount_tmpfs mirror | same | mount_tmpfs usage error (mount(8) passes spec+dir) -> "mount: /private/var failed with 64"; keybag reboot | out/logs/boot22 |
| boot23 | same | fstab `ramdisk /private/var tmpfs ...,size=1048576,template=/private/system_data` + /private/system_data dir | same | mount's ramdisk path runs `/sbin/mount_tmpfs -s 536870912 /private/system_data/` -> "Unsuccessful mount: Bad address" (VFS_TBLLOCALVOL w/o GENERICMNTARGS => kernel copyins a devpath from tmpfs args) -> mount-phase-2 exit(71) | out/logs/boot23 |
| boot24 | Image.pth7 rebuilt with VFS_TBLGENERICMNTARGS (sha256 2c92b689…) | same | same | VERIFIED both tmpfs mounts succeed: "LAPIS ramfs: mounted tmpfs mp=fffffe24d1af6000" (tmp location) and again for /private/var; then mount's template copy recursed (template == tmp location -> /private/var/system_data/system_data/...), aborted | out/logs/boot24 (filtered) |
Working image fstab reverted to root-only (ramdisk line commented) so it boots to the keybag blocker again.
Next: give mount a readable template dir (implement readdir in lapisfs, or a template on the ramfs) so ramdisk /private/var completes; then keybagd systembag.

## Chunk 6 (ramfs /private/var + keybag)
lapisfs source: candidates in /workspace/gpt-r2/{chk,ios-lapis-rootstatus-patched}/…/lapis-mockfs contain all 8 kernel blob strings but rebuild (6928 B) != kernel blob (~5.4 KB); gpt-r1/wB and ios-lapis/src differ more. So readdir was added without rebuilding lapisfs: `lapisfs_readdir` lives in the ramfs blob and the kernel lapisfs vnodeop table slot (0xfffffe000743de28, vnop_readdir_desc entry, was err_readdir) points to it. Struct layout verified from the kernel's own lapisfs lookup disassembly (0xfffffe000743d2d8).
keybag: "dt = %d, bootarg = %d" is MKBDeviceSupportsEnhancedAPFS (IODeviceTree:/filesystems e-apfs / -apfs_shared_datavolume), not the fatal gate. keybagd --init skips MKB_INIT when os_variant_uses_ephemeral_storage("com.apple.mobile.keybagd") ("DEVICE HAS EPHEMERAL DATA VOLUME, SKIP INIT", keybagd @0x100012c6c); libdarwin variant.c reads sysctl hw.ephemeral_storage, which xnu kern_mib.c fills from ADT /chosen/ephemeral-storage (u32). Added via tools/patch_image_adt.py -> Image.pth9.
| boot25 | Image.pth8 (ramfs + lapisfs readdir) | fstab ramdisk template=/private/var | qemu11 | VERIFIED readdir: mount copies template: "Copying /private/var/db/timezone/zoneinfo/Europe/Helsinki -> /private/system_data//var/db/..." (no trailing slash => extra var/ level); stopped | runs/boot25 |
| boot26 | Image.pth9 (+ /chosen/ephemeral-storage=1) | template=/private/var/ | qemu11, lp hooks off | both tmpfs mounts OK, 697 template files copied, copy into /private/var running; then `/sbin/umount` (of tmp location) never returns (qemu ~44% CPU, no events) | out/logs/boot26 |
| boot27 | Image.pth9 + unmount tracing | same | same | trace: `R_UNMOUNT mp=0xfffffe24d8fac000` then `MSLEEP wmesg='mount_drain'` -> ramfs VFS_UNMOUNT never vflushed the root vnode (dounmount flushes SKIPROOT), mount_refdrain sleeps forever | out/logs/boot27 |
Fix: lapis_ramfs.c r_unmount now does vflush(mp, NULLVP, MNT_FORCE?FORCECLOSE:0), frees the node tree, clears fsprivate. -> Image.pth10 (sha256 68adcdd25ea7a76c8d60b9804d3f915e1694bc84d438901e18f7484b8002cbe8, out/kernel/Image.ipados.lapis_pthread.ramfs-ephemeral).
| boot28 | Image.pth10 | fullroot-mnt2.img (fstab ramdisk template=/private/var/) | qemu11 -cpu max,pauth-impdef=on,lva=off | VERIFIED: umount OK (`R_UNMOUNT` -> `VFLUSH flags=0x2` -> `SYS_EXIT rval=0`), mount prints ` on /private/var (tmpfs, local)`, mount-phase-2 (apfs_boot_util) exit 0; keybagd: "****** DEVICE HAS EPHEMERAL DATA VOLUME, SKIP INIT ****" (no keybag fatal); usermanagerd, dirs_cleaner, xpcroleaccountd, init_featureflags run; then tzinit exit(17): "Unable to locate data; resetting to system partition: No such file or directory / Unlink of zoneinfo symlink failed: Read-only file system / Last resort force_system_dataset() failed: File exists" -> launchd userspace panic "boot task failure: tzinit" | out/logs/boot28 |
Next blocker: tzinit. HYPOTHESIS: template copy turned /private/var/db/timezone/zoneinfo into a real directory (or ramfs symlink/remove semantics differ), tzinit expects a symlink it can unlink/recreate; the EROFS suggests the unlink resolves onto lapisfs (e.g. /var/db/timezone path vs /private/var, or ramfs VNOP_REMOVE on a dir). Check rootfs entry type + ramfs remove/symlink ops.
AppleKeyStore stub: NOT DONE (not needed with ephemeral-storage path so far).

## Chunk 7 (tzinit, lapisfs symlinks, launchd)
Root cause of tzinit exit(17): lapisfs had NO symlinks (entry types only 1=dir, 2=file); lapisfs_add.py had emulated /var -> private/var, /usr/share/zoneinfo, etc. as mirrored real directories. So /var/db/timezone/zoneinfo resolved onto read-only lapisfs (/var mirror) -> "Unlink of zoneinfo symlink failed: Read-only file system"; template entry /private/var/db/timezone/zoneinfo was a dir (type 1), localtime a plain file.
Fix (kernel, Image.pth11 sha256 f717c349f9b3ddb038a0f437cc08a75df0e2ad390a8c84efbadb8687cc35b15b, out/kernel/Image.ipados.lapis_pthread.ramfs-symlink): lapisfs symlink = type-2 entry with mode S_IFLNK (keeps kernel metadata validation unchanged); lapisfs vget @0x743d268 calls lapisfs_vtype (VREG/VDIR/VLNK); lapisfs vnodeop readlink slot 0x743de38 (was err_readlink) -> lapisfs_readlink (reads the data extent with the kernel lapisfs reader @0x743d464); readdir returns DT_LNK.
Fix (rootfs, tools/lapisfs_symlink.py on fullroot-mnt2.img; backup fullroot-mnt2.pre-symlink.img): /var -> private/var, /usr/share/zoneinfo -> /var/db/timezone/zoneinfo, /private/var/db/timezone/zoneinfo -> /usr/share/zoneinfo.default, /private/var/db/timezone/localtime -> /var/db/timezone/zoneinfo/America/Los_Angeles; old mirror dirs moved under /private/.lapis-mirrors.
| boot29 | Image.pth11 | first symlink rootfs | qemu11 lva=off | root mount loop "cannot mount root, errno = 19": kernel lapisfs validation requires parent index < child index (moved mirror dirs under a new high-index holder) -> tool fixed (first moved dir becomes holder) | out/logs/boot29 |
| boot30 | Image.pth11 | fixed symlink rootfs | same | VERIFIED tzinit SYS_EXIT 0 ("localtime link invalid; resetting to America/Los_Angeles" then exit 0); FinishRestoreFromBackup, finish_demo_restore OK, sysstatuscheck exit 1 / prng_seedctl exit 13 (non-fatal); launchd_cache_loader abort_with_payload "Unable to find AMFI" (no AppleMobileFileIntegrity IOService in this kernel) | out/logs/boot30 |
launchd_cache_loader: with kern.bootargs `launchd_unsecure_cache=<nonzero>` it skips the AMFI signature check of launchd.plist (disasm @0x100000b28..0x100000ba4 -> 0x100000ed4). Added to the built-in cmdline -> Image.pth12 (sha256 decbb7cdcc4e557cfc11207824743a498c338a9bb90dc6799a5a6b6c5df8f4a6, out/kernel/Image.ipados.lapis_pthread.ramfs-symlink-unsecurecache).
| boot31 | Image.pth12 | same | same | VERIFIED launchd_cache_loader exit 0, init_exclavekit exit 0, "(boot) Early boot complete. Continuing system boot.", system + user/501 bootstrap; xpcproxy spawns #27..#93 (SpringBoard [28], UserEventAgent [29], ...). Harness stopped at first fatal: UserEventAgent SIGKILL by code signing: "rejecting invalid page at address 0x104f7c000 from offset 0x8c000 in file /usr/lib/dyld (... validated:1 tainted:1)", status 0x25003200 | out/logs/boot31 |
Next blocker: /usr/lib/dyld pages modified earlier (PAC-strip/brk restore) no longer match its code-directory hashes; processes with CS_KILL/hardened flags (UserEventAgent) get killed. HYPOTHESIS: proper fix = recompute dyld's CodeDirectory page hashes (re-sign) for the modified pages, or restore original dyld pages where the change is no longer needed.

## Chunk 8 (code signing: dyld + shared cache)
Diagnosis: fullroot-mnt2 /usr/lib/dyld differed from the IPSW original (ipsw-boot/usr/lib/dyld == original ufs-fullroot.img copy, sha256 83103974…) in 37 of its 16K pages (0x0..0x90000, ~20k bytes): tools/patch_fullroot_pac_strip.py rewrote PAC ops in standalone dyld; CodeDirectory (1 CD, sha256, 4K pages, 319 slots) left stale -> 146 stale slots (tools/macho_cd_rehash.py dry check).
Step 1: restored the IPSW-original dyld (tools/lapisfs_replace.py fullroot-mnt2.img /usr/lib/dyld <original>): the dyld PAC strip is not needed with qemu11 pauth-impdef=on.
| boot32 | Image.pth12 | mnt2 + original dyld | qemu11 lva=off | VERIFIED same progress as boot31 with unmodified dyld (Early boot complete, SpringBoard spawned). UserEventAgent now killed on a SHARED CACHE page: "cs_invalid_page(0x1bc67c000): p=29[UserEventAgent] final status 0x25003200, denying page sending SIGKILL"; 18 cs_invalid_page events (others allowed, no CS_KILL) | out/logs/boot32 |
Cause: PAC-strip (+ FIPS HMAC reseal) of DSC TEXT left the subcache CodeDirectory slots stale. tools/dsc_cd_rehash.py (in place, per subcache: header codeSignatureOffset@0x28; only changed slots written). Validated on the untouched original image: 0 stale slots. On mnt2: 185936 stale slots in .01–.79 rewritten; re-check 0 stale. (DSC PAC strip still kept: unsigned Auth64 pointers from the slide pager.)
| boot33 | Image.pth12 | mnt2 + original dyld + DSC CD rehash | same | VERIFIED 0 cs_invalid_page events (was 18); Early boot complete; 103 spawns incl. SpringBoard; UserEventAgent now aborts: ABORT_WITH_PAYLOAD reason_string='Error setting low space io policy: 1 (Operation not permitted)' | out/logs/boot33 |
Next blocker: entitlements. xnu iopolicysys_vfs_allow_lowspace_writes requires IOCurrentTaskHasEntitlement("com.apple.private.vfs.allow-low-space-writes") (xnu-src kern_resource.c:3042); entitlement queries are answered by AMFI via amfi_interface_register / amfi_core_entitlements_register (symbols present at 0xfffffe0007a91d50/0x7a91d70) but no AMFI kext registers in this kernel ("Unable to find AMFI" earlier), so every entitlement check fails (also tzinit/keybagd setiopolicy EPERM). HYPOTHESIS: proper fix = minimal kernel entitlement provider (register CoreEntitlements/amfi interface that reads the entitlements blob of the code signature), or locate an AMFI kext for this build.

## Chunk 9 (lapis_amfi entitlement provider)
Finding: the prebuilt kernel's IOKit entitlement entry points (IOTaskHasEntitlement, IOCurrentTaskHasEntitlement, IOTask/IOCurrentTaskHasStringEntitlement, IOTaskGetEntitlement, IOTaskGetIntegerEntitlement, IOTaskHasEntitlementAsBooleanOrObject x2) had the amfi->OSEntitlements call replaced by `nop` (was `ldr x8,[x8,#off]`) + `mov w0,#1` (was `blr x8`) -> every entitlement query "not entitled"; `amfi` (0xfffffe0007277430) is NULL (no AMFI kext). Intact reference call: IOUserClient::copyClientEntitlement (ldr x8,[x8,#0x1e0]; blr x8).
amfi_t layout (libkern/amfi/amfi.h, verified against binary uses): get_xml +0x20, TrustCache @0x118 (loadModule +0x120, query +0x130), OSEntitlements @0x198 (adjustContext +0x1a0, queryEntitlementBooleanWithProc +0x1c0, ...StringWithProc +0x1d0, copyEntitlementAsOSObjectWithProc +0x1e0), MTE hooks 0x1e8..0x208.
Implementation: xnu/lapis-ramfs/lapis_amfi.c (linked into the ramfs blob @0xfffffe0007440428..): entitlements from csproc_get_blob() -> csblob_get_entitlements() (XML blob, hash-checked against the CD special slot) + top-level plist <key> lookup; bool/string(+array)/integer/OSObject (kOSBooleanTrue/False, OSString::withCString, OSNumber::withNumber); TrustCache.* -> error, adjustContext -> success (nothing attached), MTE NULL. Registered by lapis_amfi_boot, which replaces `bl load_static_trust_cache` in kernel_bootstrap_thread (0xfffffe0007ba6e10): calls it, then amfi_interface_register() - before machine_lockdown (amfi is SECURITY_READ_ONLY_LATE). tools/patch_kernel_amfi.py also restores the 8 IOKit call sites (real ldr/blr, asserted originals).
Kernel Image.pth13 = pth5 + ramfs/lapisfs + lapis_amfi + ADT ephemeral-storage + launchd_unsecure_cache=1 (sha256 691bf4a08797867d4e0ced8edcd53fc2af04d5b0bc0ba3e5269b9970d39dbfb6, out/kernel/Image.ipados.lapis_pthread.amfi).
| boot34 | Image.pth13 | mnt2 (orig dyld, DSC rehash) | qemu11 lva=off | VERIFIED "LAPIS amfi: entitlement provider registered"; io-policy EPERMs gone (boot33: "MKB_INIT: Error setting io policy: -1", "tzinit: setiopolicy ... Operation not permitted", UserEventAgent abort; boot34: none); UserEventAgent runs; 168 spawns (boot33: 99) incl. backboardd, rapportd, PerfPowerServices; 26 daemons exit 78; then kernelmanagerd -> kext_request -> OSKext::loadFileSetKexts -> IOService::iokitDaemonLaunched -> adjustBusy -> _adjustBusy -> publishHiddenMedia.cold.1 = assert(iomediaClass) (no IOMedia class: IOStorageFamily kext absent) -> kernel panic | out/logs/boot34 |
Note: the IOLog "%p" in lapis_amfi_boot prints a wrong value (Apple variadic ABI passes varargs on the stack; gcc uses registers) - cosmetic, to drop.
Next blocker: IOMedia metaclass missing (IOStorageFamily) -> assert in IOService::publishHiddenMedia when kernelmanagerd reports in. Also: why 26 daemons exit(78) (EX_CONFIG).

## Chunk 10 (IOMedia, amfi log, xpcproxy exit 78)
IOStorageFamily from the IPSW: the IPSW (/workspace/ios-lapis/ipsw/iPad_Pro_M4_26.5_23F77_Restore.ipsw) has kernelcache.release.ipad16 (Apple fileset KC). Its kexts are prelinked against Apple's own t8132 XNU in that KC; our kernel is the OSS runner build ("Darwin Kernel Version 25.5.0 ... runner:xnu-12377.121.6", ksyms has no IOMedia/IOStorage classes and no kxld), so those kexts cannot be loaded or linked here (HYPOTHESIS-level: not attempted beyond inspection). -> minimal built-in IOMedia class.
Implementation: xnu/lapis-ramfs/lapis_iomedia.c, called from lapis_ramfs_wrap (bsd_init, IOKit up): OSMetaClass::preModLoad("__kernel__") -> OSMetaClass::OSMetaClass(mc, "IOMedia", IOService::metaClass, IOService classSize) -> vtable = copy of IOService::MetaClass vtable (32 entries, alloc at entry 23 verified) with alloc() -> NULL -> OSMetaClass::postModLoad(). No instances (no storage driver). amfi log: bogus %p removed. Kernel Image.pth14 sha256 bbbc17611aeef0712a11a60a231e18f8f5a98484c81df2744d32ace741e3d26d (out/kernel/Image.ipados.lapis_pthread.amfi-iomedia).
Harness: SYS_EXIT now prints the process name; drive.py CONT_REASON=1 keeps running past user-process aborts (stops only on kernel panic).
| boot35 | Image.pth14 | mnt2 | qemu11 lva=off | "LAPIS iomedia: registered built-in IOMedia metaclass"; harness stopped at lockdownd abort "Library not loaded: /usr/lib/libramrod.dylib" (not in rootfs, not in dyld cache) | out/logs/boot35 |
| boot36 | same, CONT_REASON=1 | same | same | kernel panic "Mutex interlock timeout" in lck_mtx_lock_contended <- vm_map_enter_upl_range <- buf_map <- lapisfs strategy (0x743cd40) <- cluster_pagein (not seen in boot34/35/37: HYPOTHESIS timing under TCG+gdb, or a real lock-order issue in lapisfs strategy mapping bufs during pagein) | out/logs/boot36 |
| boot37 | same | same | same | VERIFIED past the old panic: "KextLog: Loading FileSet KC(s)" -> "deferred rematching count 0" -> "KextLog: kernelmanagerd is active" (no publishHiddenMedia assert). 205 spawns. Exit-78 processes are all xpcproxy (27x "SYS_EXIT rval=78 proc=['xpcproxy']"). Then watchdogd SIGTRAP (brk, esr 0xf2000001 at 0x102da6fc0) -> proc_prepareexit.cold.3 kernel panic (critical-process exit) | out/logs/boot37 |
Exit 78: VERIFIED it is xpcproxy (EX_CONFIG) for ~27 services; HYPOTHESIS: xpcproxy cannot set up/exec those services (missing binaries/libs from the AppOS/OS cryptex, e.g. libramrod.dylib which dyld also looks for under /System/Cryptexes/OS; the OS cryptex 094-55686-100.dmg.aea was only extracted for the dyld cache). Not yet mapped service-by-service (launchd.log not writable: /private/var/log/com.apple.xpc.launchd missing).
Next blocker: watchdogd crash (brk trap) -> kernel panic on its exit; then libramrod/cryptex content and lapisfs pagein lock timeout.

## Chunk 11 (watchdogd brk, panic on its exit, launchd log, xpcproxy exit 78, cryptex content)
watchdogd brk: pc 0x102da6fc0 = slide + 0x10000efc0 in /usr/libexec/watchdogd (only brk #1 with page offset 0x2fc0 in __text). Preceding call: abort-with-message "connect_to_kext() unexpectedly failed - unable to lookup watchdog KEXT service" (IOServiceGetMatchingService(IOServiceMatching("IOWatchdog")) == 0; no watchdog driver in our kernel).
  watchdogd's own design: at 0x100005b4c it calls a helper (0x10000adb0) that reads sysctl kern.hv_vmm_present; if set it logs "detected virtual machine environment and no watchdog KEXT found, exiting..." and exits 0 (KeepAlive SuccessfulExit=false -> not restarted). xnu: kern.hv_vmm_present = IODTGetDefault("vmm-present") (bsd/kern/hvg_sysctl.c), PE_vmm_present = TUNABLE_DT /defaults vmm-present (pexpert/arm/pe_init.c) - what Apple's VMs set.
  Fix (QEMU only; QEMU really is a VM): ADT /defaults vmm-present = 1. Other xnu uses of PE_vmm_present: allow_research_modes() (code_signing.h) only; the hvc paravirt-lock probe is independent of it.
  Tablet (real SM8650): NOT DONE - needs a real IOWatchdog driver (Qualcomm APSS WDT) or watchdogd will hit the same brk there.
Kernel panic on its exit: proc_prepareexit.cold.3 = proc_handle_critical_exit (kern_exit.c) - deliberate: watchdogd's launchd plist has _PanicOnCrash {PanicOnConsecutiveCrash}, so launchd spawns it with a POSIX_SPAWN_PANIC_ON_* crash behaviour after consecutive crashes and xnu panics by design. Fix: the xnu tunable boot-arg panic_on_crash_disabled=1 ("panic-on-crash disabled via boot-arg" now printed instead of panicking). The console message seen in boot37 ("Console I/O from interrupt-disabled context") was a nested panic in handle_debugger_trap's panic-string print (console_suspended still 0); HYPOTHESIS: kernel_debugger_entry_count read as 0 under the gdb harness; not investigated further (no longer reached).
launchd log: /private/var/log + /private/var/log/com.apple.xpc.launchd added to the lapisfs /private/var template (lapisfs_add --dir); launchd also logs to console, which gives the service label for every xpcproxy exit.
xpcproxy exit 78 map (boot40): AppleCredentialManagerDaemon, CommCenter, SpringBoard, accountsd, identityservicesd, imagent, mobiletimerd, powerd, sleepd - VERIFIED every one is a service whose Program is not in our rootfs (launchd: "service will always use xpcproxy because its binary is missing or not executable"; 41 such services). Static scan of /System/Library/LaunchDaemons: 276 programs missing, 259 present in the IPSW OS dmg, 16 in the AppOS cryptex (/System/Cryptexes/App/...), 1 Recovery.
Cryptex content: libramrod.dylib is NOT in the OS dmg or the dyld cache; it is a standalone dylib in the SystemOS cryptex (094-55686-100.dmg.aea, usr/lib/libramrod.dylib). Re-decrypted the cryptex (ipsw fw aea), extracted its non-DSC content (2017 files, 128 MB; 1595 files are decmpfs type 14 LZBITMAP, decoded with libzbitmap via tools/decmpfs_decode_tree.py) -> lapisfs /System/Cryptexes/OS. AppOS cryptex 094-54871-103.dmg (16 MB) -> /System/Cryptexes/App. The 259 missing daemon programs (+ their .app/.xpc/.appex/.bundle bundles without .lproj, 1076 files) extracted from the OS dmg -> lapisfs (lapis-root grown +192 MiB +384 MiB in place). tools/lapisfs_fix_macho_modes.py restores 0755 on 48 decoded Mach-Os (first decoder run lost the exec bit; decoder now keeps mode/mtime).
Kernel Image.pth15 = pth14 + ADT /defaults vmm-present=1 + panic_on_crash_disabled=1 (sha256 7251e646d6fd95d16ccccf2c0dc39c517af171b16da78bee29baedb6ff9fabc9, out/kernel/Image.ipados.lapis_pthread.amfi-iomedia-vmm).
| boot38 | Image.pth15 | mnt2 + log dir | qemu11 lva=off | aborted (EXTRA hook script not set -> no console events) | - |
| boot39 | same | mnt2 + log dir + SystemOS cryptex | same | early kernel panic "Spinlock timeout" in thread_quantum_expire (timer irq) while the fb console scrolls (gc_scroll_up <- IOLog) right after lapisfs root mount; HYPOTHESIS TCG timing (fbcon scroll is very slow under TCG), not reproduced in boot40/41 | out/logs/boot39 |
| boot40 | same | same | same | VERIFIED: "SYS_EXIT rval=0 proc=['watchdogd']" (VM path, no brk, no panic); lockdownd now loads libramrod (spawned, then crashes later: "Process[80] crashed: lockdownd"); 242+ spawns (boot37: 205), no kernel panic in ~7 min; 29 exit-78 = the 9 services above; stopped by me to update the rootfs | out/logs/boot40 |
| boot41 | same | + 259 daemon binaries + AppOS cryptex + modes | same | SpringBoard now really spawned (SPAWN #29 .../SpringBoard.app/SpringBoard); "binary is missing" 41 -> 33 (remaining: UI scene services/angels whose plists are not in LaunchDaemons, e.g. PosterBoard, InputUI, AccessibilityUIServer, sharinguiservice); 173 spawns, then KERNEL PANIC via sleh_synchronous_sp1 (synchronous exception taken on SP1 = exception-stack path, typically a kernel stack overflow/guard hit); panic args not captured (Apple variadic ABI) | out/logs/boot41 |
Next blocker: boot41 kernel panic in sleh_synchronous_sp1 (capture saved state/pc: hook panic_with_thread_kernel_state and read the arm_saved_state); then userland crashes seen in boot40 (lockdownd, atc, fairplayd.A2 SIGTRAP/NULL-deref; launchd "Unable to find persona with type 6/4" spam - kpersona not set up), 33 remaining UI services without binaries.

## Chunk 12 (boot41 SP1 panic, timeouts, personas, lockdownd, UI services)
Harness: new hook on panic_with_thread_kernel_state dumps the arm_saved_state (pc/lr/sp/fp/cpsr/far/esr, x0-x28, frame chain); persona() syscall trace (op + return). Fixed harness bug: temporary return breakpoints were never deleted when stop() returned False (boot43 flooded with 117k RET lines; aborted).
boot41 panic root cause (VERIFIED, boot44 capture): `PWTKS msg='Kernel instruction fetch abort' pc=0x0 lr=0xfffffe0007b58ed8 (IOPMrootDomain::setProperties+0x88) far=0x0 esr=0x86000006` <- is_io_registry_entry_set_properties <- ipc_kobject_server <- mach_msg2_trap. Not a stack overflow: the "sleh_synchronous_sp1" seen in PANIC2 was only the stale return address of the bl into panic_with_thread_kernel_state (sp1 starts right after it). setProperties calls IOUserClient::copyClientEntitlement() and release()s the result; our lapis_amfi copyEntitlementAsOSObjectWithProc returned x0=0xfffffe0007ce8540 = &gOSBooleanTrue: libkern defines `OSBoolean * const & kOSBooleanTrue = gOSBooleanTrue;` (a C++ reference), so the exported symbol kOSBooleanTrue is a pointer to the gOSBooleanTrue slot. vtable slot 0x28 of that "object" was 0 -> blr to 0. Fix: lapis_amfi.c declares kOSBooleanTrue/False as `void *const *` and returns *kOSBooleanTrue (objdump: adrp/ldr/ldr). Kernel Image.pth17 (sha256 6a037305678c371185f214f6041f31416b3b3281a7c7593b7500f1583fe3df76, out/kernel/Image.ipados.lapis_pthread.amfi-iomedia-vmm-fix).
Mutex interlock timeout (boot36, boot42: lck_mtx_ilk_lock_contended <- ipc_importance_task_process_updates <- mach_msg): spin timeout policy lck_mtx_ilk_timeout_policy uses MACHINE_TIMEOUT lock-panic = 0xc00000 ticks (~0.5 s). Under TCG (8 vCPU threads on 8 host CPUs, gdb breakpoints pausing the VM) a lock holder vCPU can stall longer than that. Fix (QEMU-only boot config, documented xnu knob kern/machine.c): boot-arg ml-timeout-global-scale=16. HYPOTHESIS that this is the whole story (no recurrence in boot44/45, but intermittent before).
Personas (VERIFIED): launchd calls persona(PERSONA_OP_FIND_BY_TYPE=9) for types 6 (SYSTEM_PROXY) and 4 (SYSTEM) -> ESRCH (3); no process ever calls PERSONA_OP_ALLOC/PALLOC (1/2) during boot44/45. /System/Library/UserManagement/com.apple.xpc.launchd.personas.plist is present (SystemPersonaServices: cloudd, accountsd, mdmd, backupd). HYPOTHESIS: the system personas are created by usermanagerd (it has the kpersona_alloc/palloc paths, "persona manifest", /private/var/PersonaVolumes); its early boot task exits 0 without creating any; the daemon is launched late (speculative). NOT DONE: find the usermanagerd condition (manifest / UserSessions in /private/var/mobile) and fix.
lockdownd (boot40): brk at 0x1000ac100 after "Failed to intialize local workers." (main, line 0x74): local-workers init (0x1000112c4) works on /private/var/mobile/Media/ApplicationArchives, /Library/Lockdown, repair_permissions(mobile). Our /private/var template had only db/ and log/. Fix: the OS dmg's private/var skeleton (Keychains, mobile, root, MobileDevice, preferences, run, tmp, ... 220 entries, minus staged_system_apps = 1 GB of preinstalled apps) added to the lapisfs /private/var template. boot45: lockdownd spawned (#111), no crash/exit seen by 237 spawns (VERIFIED no crash in that window). Caveat: lapisfs has no owner field, so the template copy is root-owned (iOS has mobile 501 on /private/var/mobile) - HYPOTHESIS future EACCES for mobile daemons.
UI services: the authoritative job list is the launchd cache /System/Library/xpc/launchd.plist (675 jobs, incl. /Applications/*.app scene services). 47 programs missing: 37 in the OS dmg (PosterBoard, InputUI, SharingUIService, AccessibilityUIServer, ... + 2096 bundle files, 44 MB) -> added; 10 not shipped for this device (proximitycontrold, sntpd, dietapplecamerad, cellularcertd, factory/Developer tools, ...). boot45: "binary is missing" 33 -> 1 (proximitycontrold, not in the IPSW); xpcproxy exit 78: 0.
| boot42 | Image.pth15 | chunk-11 rootfs | qemu11 lva=off | "Mutex interlock timeout" panic at 247 spawns (see above); SP1 panic not reproduced in this run | out/logs/boot42 |
| boot43 | Image.pth16 | + UI services + /private/var skeleton | same | aborted (harness RetBP flood) | - |
| boot44 | Image.pth16 | same | same | panic captured: Kernel instruction fetch abort pc 0 in IOPMrootDomain::setProperties (lapis_amfi kOSBooleanTrue bug) at 181 spawns | out/logs/boot44 |
| boot45 | Image.pth17 | same | same | VERIFIED past it: 252 spawns in ~8.5 min, no kernel panic (stopped by me at 23:11 MSK), 0 exit-78, 1 "binary missing"; crashes: SpringBoard x4, backboardd x2, cameracaptured x4, tccd x2, bluetoothd x2, accountsd, imagent, rapportd, siriactionsd, contextstored, appprotectiond (corpse names, rate-limited) | out/logs/boot45 |
Next blocker: user-space crashes of the UI stack (backboardd/SpringBoard - likely no display/HID drivers: IOMobileFramebuffer etc.), tccd/accountsd; personas not created (usermanagerd); fairplayd.A2 killed repeatedly (FairPlay needs SEP - expected on QEMU).

## Chunk 13 (backboardd / SpringBoard crash reasons, IOKit needs)
Harness: SIGABRT capture on ___pthread_kill (sig 6) with the user frame chain; IOKit matching trace (io_service_get_matching_service(s)_bin, add_notification_bin_64) per process; csops_internal trace (ops 0/7/16 + return); NAMEI_FAIL now deduplicated per path with process name (old 800-line cap hid late failures). DSC slide for symbolization = 0x102b8000 (runtime ___pthread_kill+8 0x2424f91d0 vs cache 0x2322411d0); `ipsw dyld a2s` on pc-slide.
backboardd (VERIFIED, boot47 x3, boot48 x2): uncaught ObjC exception -> abort: abort <- objc_terminate <- objc_exception_throw <- +[NSURL fileURLWithPath:] (nil path) <- -[BSSystemContainerForCurrentProcessPathProvider libraryPath] <- cachesPath <- dispatch_once <- backboardd main. libraryPath = BSSystemSharedResourcesDirectoryForCurrentProcess -> BSSystemSharedDirectoryForIdentifier -> container_system_path_for_identifier() returned NULL (no system container for backboardd).
SpringBoard (VERIFIED, boot47): EXC_BREAKPOINT brk #0 at 0x1b42d6654 = -[BSUIMappedImageCache initWithUniqueIdentifier:options:]+1220 (after __bs_set_crash_log_message): the cache directory could not be made: boot48 NAMEI_FAIL ENOENT /var/mobile/Library/Caches/MappedImageCache{,/Persistent} (SpringBoard runs as mobile 501; /private/var/mobile/Library/Caches in our template is root:wheel 0755 because lapisfs has no owner field and mount_tmpfs copies the template as root) -> HYPOTHESIS: EACCES on mkdir (not traced yet).
Neither crash is an IOKit/display failure: both die in BaseBoard path setup before display bring-up. IOKit lookups seen: backboardd notification on {IOProviderClass IOService, IOPropertyMatch HIDServiceSupport}; IOMFB_FDR_Loader notification on IOProviderClass IOMobileFramebuffer (no such class in our kernel: ksyms has no IOMobileFramebuffer/IOHIDEventService/IOSurface*/AppleCLCD). Notifications do not fail, so missing display/HID services are not yet fatal; they will be after the container/ownership fixes.
Entitlements to userland work (csops DER entitlements ops 16 ret 0 for MSUEarlyBootTask, containermanagerd, ...). containermanagerd (agent, user/501, pid 73) runs, no crash; containermanagerd.system (root, mach service com.apple.containermanagerd.system) is never demand-launched in boot47/48; /private/var/containers does not exist (MobileAsset/appprotectiond ENOENT on /private/var/containers/Shared/SystemGroup/...).
NOT DONE: kernel-side display provider (IOMobileFramebuffer over the simple framebuffer) - not the first blocker; needs a real C++ IOService subclass (metaclass named IOMobileFramebuffer) in the lapis blob plus its user client selectors; IOSurface (IOSurfaceRoot) is also absent and QuartzCore needs it. Personas via usermanagerd, /private/var/mobile uid 501 ownership: NOT DONE (analysis above).
| boot46 | Image.pth17 | chunk-12 rootfs | qemu11 lva=off | harness test (IOKit trace), stopped early | - |
| boot47 | same | same | same | 442 spawns, no kernel panic; SIGABRT frames for backboardd x3, bluetoothd x2, contextstored, accountsd, xpcproxy (same NSURL nil-path pattern in bluetoothd/contextstored); SpringBoard brk in BSUIMappedImageCache | out/logs/boot47 |
| boot48 | same | same | same | 345 spawns, no kernel panic; csops + dedup NAMEI trace (MappedImageCache ENOENT, no /private/var/containers) | out/logs/boot48 |
Next blocker: data-volume layout and ownership: /private/var/mobile (and Library/Caches etc.) must be owned by mobile:mobile 501 and /private/var/containers created by containermanagerd(.system); then IOMobileFramebuffer/IOSurface/HID providers for backboardd.

## Chunk 14 (/private/var ownership via lapisfs owners; containermanagerd.system)
LAPISFS owner extension (backward compatible, still ver 2): optional table in the metadata prefix between the 40-byte header and the entries (ent_off is a header field; old kernels/tools that honour it skip it): 0x28 "LFSOWN01", u32 count, u32 0, u64 table_off=0x40; count x {u32 uid, u32 gid} indexed by entry number; entries >= count / images without it = 0:0.
  Kernel: lapisfs_getattr (xnu/lapis-ramfs/lapis_ramfs.c, real code) replaces the kernel lapisfs getattr (0x743caf4, hard-coded uid/gid 0); tools/patch_kernel_ramfs.py points the lapisfs vnodeop getattr slot (0xfffffe000743dd48, desc vnop_getattr_desc) at it. Same fields as before (nlink, sizes, mode, fileid) plus uid/gid from the table; reads md->base (prefix [0,str_off) is resident for md and blk roots).
  First layout (table hung off the root entry extent at the image end) was REJECTED by the kernel lapisfs mount: it validates ents[0].data_off == 0 && size == 0 (0xfffffe000743d70c) -> boot49 "cannot mount root, errno = 19" in a loop. Moved into the metadata prefix; boot50 "LAPIS lapisfs: mounted root size=17732c000 entries=14194 disk=1" (VERIFIED).
  Tools: tools/lapisfs_owners.py IMG METADATA [--modes PREFIX] (TSV path/mode/uid/gid from `7zz l -slt` of the IPSW OS dmg 094-54982-099.dmg, 434519 records); lapisfs_add.py keeps the table (new entries 0:0); fix_macho_modes/symlink accept ent_off > 0x28.
  Applied to fullroot-mnt2.img (in place): 7909 entries matched in the dmg, 1829 non-root owners, 47 modes under /private/var corrected from the dmg (e.g. /private/var/mobile 0711 501:501, Library/Caches 0700 501:501, Media 0750 501:501, /private/var/tmp 1777 0:0, /private/var/root 0750 0:0; ExternalAccessory 501:286).
  Kernel Image.pth18 = pth17 chain with the new ramfs blob (sha256 6194f1bb41797a769577e5a19edbe3205ca9f1c51114ff3774eff6fa07581b4e, out/kernel/Image.ipados.lapis_pthread.amfi-iomedia-vmm-owners).
containermanagerd.system: launchd job (LaunchDaemons/com.apple.containermanagerd.system.plist) is purely on-demand: LimitLoadToSessionType System, MachServices com.apple.containermanagerd.system, no RunAtLoad/KeepAlive -> launchd starts it only when a client looks the service up. In boot47/48 nobody did (the per-user agent containermanagerd runs with --system-container-mode=proxy). NOT DONE: find why the agent never proxies (needs a mach lookup trace / agent log).
| boot49 | Image.pth18 (root-entry layout) | mnt2 + owners v0 | qemu11 lva=off | mountroot loop "cannot mount root, errno = 19" (layout rejected, see above) | out/logs/boot49-rootentry-rejected |
| boot50 | Image.pth18 | mnt2 + owners (prefix layout) | same | root mounted; 171 spawns in ~145 s kernel time; no crash/abort of SpringBoard (#29) or backboardd (#122) in that window, no MappedImageCache ENOENT; run ended by a harness stop (unexpected SIGTRAP at 0xfffffe0007a4697c while a temporary return breakpoint was being deleted; not a kernel panic) before the point where SpringBoard/backboardd crashed in boot47/48 | out/logs/boot50 |
Next blocker: rerun past ~250 spawns to confirm SpringBoard/backboardd behaviour with owners (fix the harness temp-breakpoint race first: drop the csops return hook); then containermanagerd.system demand launch (/private/var/containers), personas.

## Chunk 15 (harness race, owners verified, SpringBoard past its path crash, containermanagerd.system)
Harness: return hooks are now persistent per-address breakpoints (never deleted) with pending tags keyed by (return pc, sp) - no temporary-breakpoint create/delete race (boot50 stray SIGTRAP). Found while doing it: csops_internal and persona are syscall handlers whose x30 is unix_syscall's return path, so a persistent return bp there fires on every syscall (boot51 crawled: 18 spawns in 10 min) -> both are entry-only now. New r_setattr (lapis-ramfs) hook logs uid/gid set on /private/var nodes.
Owners after the ramfs template copy (VERIFIED, boot51/53): mount (template copy into the ramfs /private/var) chowns per the lapisfs owner table, e.g.
  RAMFS_CHOWN uid=501 gid=501 path='/private/var/mobile/Library/Caches' proc=['mount']
  RAMFS_CHOWN uid=501 gid=501 path='/private/var/mobile/Library/Preferences' proc=['mount']
  RAMFS_CHOWN uid=284 gid=284 path='/private/var/db/darwindaemon' proc=['mount'] (also 263 analyticsd, 272 diagnostics, 266 timed, ...)
SpringBoard (VERIFIED boot53): past the BSUIMappedImageCache crash - MappedImageCache is now created as mobile (only .../MappedImageCache/Persistent and AppleDouble "._MappedImageCache" lookups before creating), atomic writes of SBLastRestoreIdentifier succeed. New crash: brk #0 at -[FBSDisplayMonitor _initWithBookendObserver:transformer:].cold.5+196 after __bs_set_crash_log_message ("failure in ..."), i.e. FrontBoardServices display monitor init fails - backboardd (owner of com.apple.backboard.display.services) is dead.
backboardd (VERIFIED boot53, 2 aborts): unchanged - NSURL fileURLWithPath:nil <- BSSystemContainerForCurrentProcessPathProvider libraryPath (container_system_path_for_identifier NULL).
containermanagerd.system: libsystem_containermanager container_xpc_create_connection(disposition,...) picks the mach service by disposition: 1 -> "com.apple.containermanagerd.system", otherwise "com.apple.containermanagerd" (0x1e2761b58 csel); dispositions 2/3 go through getuid() checks, 0 -> "Disposition unsupported". So system-container lookups from backboardd connect to com.apple.containermanagerd.system directly (not via the user agent). launchd never logs a demand spawn for it in boot47-53. HYPOTHESIS: backboardd (user/501 domain, uid mobile) cannot reach a LimitLoadToSessionType=System service from its domain, or the lookup is never made because the client fails earlier. NOT DONE: needs a launchd lookup trace (mach_msg to launchd from backboardd containing the service name) - next step.
| boot51 | Image.pth18 | mnt2 + owners | qemu11 lva=off | owners verified via RAMFS_CHOWN; crawled (persistent ret bp on unix_syscall return), stopped at 18 spawns | out/logs/boot51 |
| boot53 | same | same | same | 247 spawns, no kernel panic; SpringBoard x3 crashes now in FBSDisplayMonitor init (past the path crash); backboardd x2 same system-container abort; also appprotectiond, imagent, rapportd, accountsd | out/logs/boot53 |
Next blocker: backboardd system container (container_system_path_for_identifier -> com.apple.containermanagerd.system never launched); then display/HID services for backboardd.

## Chunk 16 (backboardd container request path)
Harness: user-space breakpoints on dyld-shared-cache functions (QEMU gdbstub bps are TCG bps by VA and fire in every process). Per-boot cache slide from vm_shared_region_start_address() (unslid base 0x180000000; boot54 slide 0xafe8000 cross-checked against the SIGABRT ___pthread_kill pc). boot54: bps armed from a kernel stop in kernel/launchd context never fired - QEMU invalidates already-translated code for a new bp through the current address space; arming them from a namei stop in a user process with the cache mapped (fsck) works (boot55).
VERIFIED (boot55): backboardd really asks containermanagerd.system:
  CM_SYSPATH proc=backboardd id=None                      (container_system_path_for_identifier)
  CM_CONNECT proc=backboardd disposition=1                (container_xpc_create_connection -> "com.apple.containermanagerd.system")
  XPC_LOOKUP proc=backboardd name='com.apple.containermanagerd.system' type=7 handle=0x0 flags=0x0   (_xpc_look_up_endpoint, 3 tries)
  same for PerfPowerServices (disposition 1, repeated lookups); for comparison backboardd's analyticsd/mobilegestalt lookups are type=2 handle=0x1f5 (user 501 domain), oscard/frontboard type=7 handle 0.
launchd never logs anything for com.apple.containermanagerd.system (no WILL_SPAWN / spawn scheduled) in boot47-55, although the job is in the launchd cache (LimitLoadToSessionType System, MachServices com.apple.containermanagerd.system, binary /usr/libexec/containermanagerd_system present 0755; entitlements: sandbox profile "containermanagerd", container-manager, storage.* - nothing that blocks a spawn here). Other System-session on-demand jobs (lsd.system, cfprefsd.xpc.daemon.system, logd_helper) are likewise silent (never demanded); notifyd/usermanagerd do spawn in system/.
HYPOTHESIS (superseded by chunk 17): launchd's reply was "not found" / domain visibility. The x0=0x9f return is in fact the reply status (see below).
NOT DONE (chunk 16): launchd reply decode was incomplete at the time of boot55 write-up.
| boot54 | Image.pth18 | mnt2 + owners | qemu11 lva=off | user bps armed in wrong context (never fired); 167 spawns, no panic | out/logs/boot54 |
| boot55 | same | same | same | XPC lookup trace above; 194 spawns, no panic; backboardd same abort | out/logs/boot55 |

## Chunk 17 (launchd lookup reply = Sandbox restriction 159; lapis_sandbox MAC policy)
Root cause (VERIFIED from boot55 logs, no new boot required for the diagnosis):
- Console: `denied lookup: name = com.apple.logd ... error = 159: Sandbox restriction` (and notification_center / commcenter.xpc) — launchd itself prints xpc error 159.
- Every traced `XPC_LOOKUP_RET` in boot55 has `x0=0x9f` (=159). That includes `com.apple.containermanagerd.system` from backboardd / PerfPowerServices / fairplayd / bluetoothd / imagent / appprotectiond. So the lookup reply status is 159, not a silent "not found", and not a System-vs-user/501 domain visibility miss.
- Chain: launchd `sandbox_check_by_audit_token(..., "mach-lookup", name)` -> libsystem_sandbox `sandbox_check_common` -> `__mac_syscall("Sandbox", 2, args)`. This kernel has no Sandbox.kext; `___mac_syscall` returns ENOPOLICY (103); sandbox_check returns -1; launchd maps that to xpc 159 and does not demand-launch the service.
- Domain-visibility hypothesis REJECTED for this wall: the denial is the sandbox check, and it hits System-session names the same way.
mac_policy_ops layout (VERIFIED against apple-oss-distributions `xnu-12377.121.6` `security/mac_policy.h`): 335 entries, `mpo_policy_syscall` at index 117.

Fix (real code, no NOPs): `xnu/lapis-ramfs/lapis_sandbox.c` — register a MAC policy named `"Sandbox"` from `lapis_ramfs_wrap` (next to IOMedia). `mpo_policy_syscall` call 2 (check) returns 0 (allowed, result word left pre-zeroed); every other Sandbox call still returns ENOPOLICY (sandbox_init / extensions unchanged — same bring-up class as lapis_amfi).
  efe0a23 had compiled `lapis_sandbox.o` but `build.sh` linked only ramfs/amfi/iomedia (sandbox never entered the blob) — that WIP was unverified and incomplete. Fixed: `$OBJS` includes sandbox; `tools/patch_kernel_ramfs_replace.py` rebuilds an already-patched Image (zeros the blob region, re-applies vnodeop/wrap/amfi restores).
Kernel Image.pth19 = pth18 blob replaced with the new ramfs+sandbox build
  sha256 fbf2658a8752e6e8aeae29e76d3df0e0f60ce8d317e2b06fde2c228a3a401aac
  (docs/status/Image.ipados.lapis_pthread.amfi-iomedia-vmm-sandbox.sha256;
   build: `xnu/lapis-ramfs/build.sh` && `python3 tools/patch_kernel_ramfs_replace.py Image.pth18 Image.pth19`).
Harness: `___mac_syscall` Sandbox call/return trace; XPC_LOOKUP_RET annotates `0x9f` as xpcerr 159.
Registration (VERIFIED on this agent with pth19 + the old 283-entry fullroot, gdb break after `bl mac_policy_register`): w0=0; console `LAPIS sandbox: check-allow policy registered`. Full containermanagerd.system path still needs mnt2.

| boot56-reg | Image.pth19 | old fullroot (283 ents) | qemu11 lva=off | VERIFIED mac_policy_register w0=0 + success log; rootfs too small to reach backboardd | /tmp/boot56-reg, /tmp/boot56-smoke2 |
| boot56 | bad rebuild (sha 95fcfae4…) | mnt2 + owners | qemu11 lva=off | ПРОВЕРЕНО (bring-up host): early panic after "Guarded vnode policy", before "LAPIS sandbox"/launchd. pc=0xfffffe000743ff54 (wrap), lr=0xfffffe000744056c (= mistaken amfi_boot+8), far=0xaf8 esr=0x96000006. Image sha ≠ fbf2658a (registration kernel). | host |

## Chunk 18 (boot56 panic: amfi_boot SITE / elf↔bin mismatch)
Root cause (ПРОВЕРЕНО offline against committed ramfs.bin 11688 B):
- Real `lapis_amfi_boot` in the matching blob is at 0xfffffe0007440580 (prologue `stp x29,x30` / `mov x29,sp`; +8 `bl load_static_trust_cache` → 0xfffffe000791a1c4).
- Panic lr 0xfffffe000744056c implies SITE was aimed at 0xfffffe0007440564. At that VA in the 11688 B blob the bytes are mid-function: `bl` 0x97fffe7a → 0xfffffe000743ff50 (inside `lapis_ramfs_wrap`) — matches pc/far.
- ГИПОТЕЗА confirmed as mechanism: `patch_kernel_ramfs_replace.py` took `lapis_amfi_boot` from ramfs.elf while placing a different ramfs.bin (or a rebuild with a different layout / 11656 B blob). It did not check prologue or rewrite/assert the in-blob BLs against ksyms.
- Not a wrong restore of IOKit sites; the death is SITE → non-prologue address inside the blob.

Fix:
- `tools/patch_kernel_ramfs_replace.py`: assert elf symbols lie inside the placed blob; assert amfi_boot prologue; rewrite amfi_boot+8/+0x14 to LSTC/AIR; assert `kernel_bootstrap_thread` SITE bl target == amfi_boot.
- `xnu/lapis-ramfs/build.sh`: pin CFLAGS (`-fno-ident`), `--build-id=none`, write `TOOLCHAIN.txt` + `ramfs.bin.sha256`. Bring-up toolchain: gcc 13.3.0 / ld 2.42 → ramfs.bin 11688 B sha256 f84c4345… (bit-identical across two rebuilds here).
- tests/test-ramfs-replace-amfi-bl.py: encodes the 0x1c-shifted mismatch pattern.

Image.pth20 = same bytes as the registration pth19 when built with that toolchain
  sha256 fbf2658a8752e6e8aeae29e76d3df0e0f60ce8d317e2b06fde2c228a3a401aac
  recipe: docs/status/Image.pth20-recipe.txt; releases lapis-pthread-pth19 + lapis-pthread-pth20.

| boot56-retry | Image.pth20 (fbf2658a…) | mnt2 + owners | qemu11 lva=off | ПРОВЕРЕНО (host): no early panic past vnguard; Sandbox MAC registered; containermanagerd.system lookup x0=0x0 (not 0x9f); `/usr/libexec/containermanagerd_system` spawns; backboardd/SpringBoard running, no nil-path abort. New wall: both containermanagerd and containermanagerd_system SYS_EXIT rval=0 then respawn (~11 exits / 720 s). | host |

## Chunk 19 (containermanagerd exit-0: missing ContainerManagerCommon plists)
NAMEI before exit (ПРОВЕРЕНО host boot56-retry; same order already in boot55 for the user agent):
  `/private/var/mobile/Library/MobileContainerManager/runtime_state.plist`  (containermanagerd)
  `/System/Library/PrivateFrameworks/ContainerManagerCommon.framework/User.plist`
  `/private/var/root/Library/MobileContainerManager/runtime_state.plist`   (containermanagerd_system)
  `/System/Library/PrivateFrameworks/ContainerManagerCommon.framework/System.plist`
  `/private/var/containers` (NAMEI_FAIL from profiled — tree never created)

IPSW 23F77 OS APFS (ПРОВЕРЕНО on this agent, apfs-fuse of 094-54982-099.dmg):
  - User.plist / System.plist / whole resource bundle of ContainerManagerCommon.framework
    (Base, Platform, Containers, Container.Class.*, Container.Global.*, Container.User.*, Info, _CodeSignature)
    are on the OS volume (~10 KiB). Framework Mach-O is DSC-only; sibling ContainerManager{System,User}/MobileContainerManager.framework hold only Info+_CodeSignature.
  - `#Include` chain: User|System → Platform → Base → Containers → Container.Class.* / Container.Global.* — need the full Common.framework resources, not two files alone.
  - `/private/var/containers` and `…/MobileContainerManager/runtime_state.plist` are ABSENT on the OS volume (runtime; daemon creates them). Do not invent their contents.
  - `/private/var/mobile` is 501:501 0711; `Library` 501:501; no MobileContainerManager dir in the IPSW skeleton.

ГИПОТЕЗА: SYS_EXIT rval=0 is clean failure after config plist open fails (boot55: User.plist ENOENT then immediate exit). Once plists are present on lapisfs, daemon should create runtime_state + `/private/var/containers` on the writable ramfs `/private/var`.

Minimal rootfs fix:
  IPSW bytes vendored at `rootfs/ipsw-mcm/` (ПРОВЕРЕНО byte-identical to APFS User/System.plist).
  Host on a COPY: `cp --sparse=always fullroot-mnt2.img fullroot-mnt2.mcm.img`
  then `tools/apply_mcm_framework.sh fullroot-mnt2.mcm.img METADATA.tsv`
  (or recipe `docs/status/Image.mcm-framework-recipe.txt`).

| boot57 | Image.pth20 | mnt2.mcm + owners | qemu11 lva=off | ПРОВЕРЕНО (host): MCM User/System.plist ENOENT gone; no SYS_EXIT rval=0 from containermanagerd*; Sandbox OK; SpringBoard running; no backboardd nil-path abort. New wall: SIGSEGV EXC_BAD_ACCESS ns=2 code=0xb FAR=0x8 in ___CFCheckCFInfoPACSignature+4 (x0=NULL) from CFDateFormatterSetFormat during MCMMigrationStatus ISO8601 init; respawn loop; `/private/var/containers` still absent. | host |

## Chunk 20 (containermanagerd SIGSEGV: missing ICU data for CFDateFormatter)
Crash (ПРОВЕРЕНО host boot57 FPCHAIN, DSC slide 0x1232c000):
  `-[MCMContainerMigrator performSynchronousBuildUpgradeMigration:…]`
  → `-[MCMMigrationStatus setMigrationCompleteForType:]`
  → `-[NSISO8601DateFormatter init]`
  → `CFDateFormatterCreateISO8601Formatter`
  → `CFDateFormatterSetFormat` with CFDateFormatterRef == NULL
  → `___CFCheckCFInfoPACSignature` (`add x8,x0,#8; ldar`) FAR=0x8.
  Not PAC mismatch (brk #0xc470 not taken).

Open-source CF (ПРОВЕРЕНО `CFDateFormatter.c`):
  `__CreateCFDateFormatter` → `__ResetUDateFormat` → `__cficu_udat_open`; on failure `_df` stays NULL → Create returns NULL.
  `CFDateFormatterCreateISO8601Formatter` does not NULL-check before `CFDateFormatterSetFormat`.

Missing data (ПРОВЕРЕНО boot55 NAMEI + IPSW APFS LuckF23F77…OS):
  `/usr/share/icu/icudt78l.dat` (34 MiB, sha256 e95268c3…) — ENOENT in guest, present in IPSW
  `/usr/share/icu/icutzformat.txt`, `/usr/share/locale/UTF-8/LC_CTYPE`
  `/usr/share/zoneinfo.default` (incl. posixrules; boot55 also ENOENT `/var/db/timezone/zoneinfo/posixrules`)
  Do not invent `runtime_state` / `/private/var/containers`.

ГИПОТЕЗА: udat_open fails without icudt78l.dat → CreateISO8601 returns NULL → SetFormat SIGSEGV. Adding IPSW ICU (+ locale + zoneinfo.default) should let migration proceed and create `/private/var/containers`.

Minimal rootfs fix (НЕ СДЕЛАНО apply/boot here — no mnt2 on this VM):
  vendored `rootfs/ipsw-icu/`; host: `tools/apply_icu_data.sh fullroot-mnt2.icu.img METADATA.tsv`
  recipe `docs/status/Image.icu-data-recipe.txt`.

| boot58 | Image.pth20 | mnt2.mcm+icu | qemu11 lva=off | ПРОВЕРЕНО (host): ICU ENOENT gone; MCM ISO8601 crash gone; containermanagerd/_system spawn once and stay up (lookup x0=0); SpringBoard/backboardd running. New wall: appprotectiond SIGSEGV FAR=0x8 in ___CFCheckCFInfoPACSignature after NAMEI_FAIL on AppProtection.framework (10 respawns / 720 s). `/private/var/containers` still missing (profiled); deferred. | host |

## Chunk 21 (appprotectiond: missing AppProtection.framework on-disk bundle)
Crash (ПРОВЕРЕНО host boot58):
  NAMEI_FAIL `…/PrivateFrameworks/AppProtection.framework`
  then `___CFCheckCFInfoPACSignature` <- `_CFBundleGetInfoDictionary` <-
  `__copyBundleLocalizationsFromResources` <- … <- `_CFBundleCopyResourceURL` <-
  `-[NSBundle URLForResource:withExtension:subdirectory:]` <- AppProtection / `_APDServerEntry`.
  elr unslid 0x1834f48cc, far=0x8 (NULL+8), same CFCheck pattern as boot57 but different caller.

IPSW (ПРОВЕРЕНО): on-disk bundle with Info.plist, Localizable.loctable, *.lproj,
AppProtection.momd, _CodeSignature, com.apple.appprotection.plist (Mach-O in DSC).

ГИПОТЕЗА: without the on-disk bundle CFBundle has no Info.plist → GetInfoDictionary NULL → CFCheck SIGSEGV.

Minimal fix (НЕ СДЕЛАНО apply/boot here): vendored `rootfs/ipsw-appprotection/`;
  in-place `tools/apply_appprotection_framework.sh fullroot-mnt2.icu.img METADATA.tsv`
  (lapisfs_add keeps existing entries — no new image copy).
  recipe `docs/status/Image.appprotection-recipe.txt`.

Other frameworks (list only, not copied): boot55 NAMEI_FAIL ∩ IPSW Info.plist → 441 paths
  in `docs/status/boot55-frameworks-needing-info.txt`. Host audit:
  `python3 tools/list_missing_framework_info.py IMG IPSW_ROOT`.

| boot59 | Image.pth20 | mnt2.icu+AppProtection | qemu11 lva=off | ПРОВЕРЕНО (host): AppProtection Info.plist present (sha256 7b205531…); NAMEI_FAIL on framework dir gone; MCM alive; SpringBoard running. Wall remains: appprotectiond 5 spawns / 4 crashes — only NAMEI is …/AppProtection (CFBundleExecutable, DSC-only); SIGSEGV ___CFCheckCFInfoPACSignature FAR=0x8 via __copyAppleLocalizations → GetInfoDictionary. | host |

## Chunk 22 (appprotectiond: CoreFoundation/Foundation on-disk Info.plist)
Crash (ПРОВЕРЕНО host boot59): same CFCheck FAR=0x8, but AppProtection bundle resources already present.
  FPCHAIN: `___CFCheckCFInfoPACSignature` <- `_CFBundleGetInfoDictionary+24` <-
  `__copyBundleLocalizationsFromResources+64` <- `__copyAppleLocalizations+124` <-
  `__copyBundleLocalizationsFromResources+304` <- `__CFBundleCopyBundleLocalizations+144` <-
  … <- `_CFBundleCopyResourceURL` <- AppProtection / `_APDServerEntry`.
  elr unslid `0x1834f48cc` = CFCheck+4.

DSC (ПРОВЕРЕНО `/tmp/dsc` .01): at `0x1834f48c8`: `add x8,x0,#8; ldar x9,[x8]` → `x0=NULL` (FAR=0x8).

IPSW (ПРОВЕРЕНО): `/System/Library/Frameworks/CoreFoundation.framework/Info.plist` + `Error.loctable`
  (+ empty `*.lproj`, `_CodeSignature`); no on-disk `CoreFoundation` Mach-O. Same for Foundation
  (+ loctables). Vendored payload ~2.17 MiB. Listed in `boot55-frameworks-needing-info.txt`.

ГИПОТЕЗА: `__copyAppleLocalizations` resolves `com.apple.CoreFoundation` and calls
  `CFBundleGetInfoDictionary` without NULL-check; without on-disk CF `Info.plist` the
  CFBundleRef is NULL → this crash. AppProtection is the trigger, not the missing dict.
НЕ СДЕЛАНО: full private-symbol walk of `__copyAppleLocalizations+100..124` (name not in
  DSC export strings here). Second hypothesis (missing CFBundleExecutable Mach-O): not fixed
  (do not invent stub). Deferred: `/private/var/containers`, lsd SIGABRT.

Budget (ПРОВЕРЕНО sizes vs IPSW): CF+Foundation full on-disk ≈ 2.17 MiB; Info.plist-only
  for all 441 ≈ 0.36 MiB; full on-disk excl. executables for all 441 ≈ 1887 MiB (over 150 MiB
  — do not bulk-copy). boot59 NAMEI ∩ 441: apply via host log +
  `tools/apply_framework_info_from_namei.py` (НЕ СДЕЛАНО here — no boot59 gdb-events on VM).

Minimal fix applied (ПРОВЕРЕНО cloud): vendored `rootfs/ipsw-cf-foundation/`;
  `tools/apply_cf_foundation_frameworks.sh` on cloud-rebuilt mnt2;
  recipe `docs/status/Image.cf-foundation-recipe.txt`.

## Chunk 23 (cloud: rebuild fullroot-mnt2 + boot60 self-test)
Host `/workspace/tpro-fix/fullroot-mnt2*.img` never lived on this VM (Grok bring-up host only).
  Rebuilt locally: `tools/rebuild_mnt2_from_ipsw.sh` from pristine ufs-fullroot 23F77
  (`/tmp/ios-lapis-ufs-fullroot-23F77.img`, sha256 819cc92b…) + IPSW APFS root
  (`/tmp/apfs-mnt/root`) + MCM/ICU/AppProtection/CF + DSC bring-up patches.
  Cloud harness: `tools/emu_run_gdb_cloud.sh` (qemu11, default **-m 43G** overcommit;
  8–12G hits early stub PC=0).

Rebuild deltas vs naive IPSW dump (ПРОВЕРЕНО):
  - DSC: `patch_fullroot_container_init` + `pac_strip --nop-brk1`; restore pristine `/usr/lib/dyld`;
    FIPS reseal → stored HMAC `313f41ec…` match True. **Do not** force pthread features
    `0x4000001e` on Image.pth20 (libdispatch DISPATCH_CLIENT_CRASH → FAR=0x4000001e).
  - `/etc` → `private/etc` symlink (else getpwnam("mobile") fails after early boot).
  - DarwinDirectory users/groups; `/.b/8` + `/private/system_data` for mount -P 2 ramdisk;
    timezone symlinks; launchd log dirs.

| boot60 | Image.pth20 | cloud-rebuilt mnt2 + CF/Foundation | qemu11 43G lva=off | ПРОВЕРЕНО (cloud): lapisfs mount 14180 ents; launchd past _container_init; mount-phase-2/tmpfs/apfs_boot_util/cc_fips/keybagd/tzinit OK; Early boot complete; MCM lookup x0=0; SpringBoard×15 + backboardd + appprotectiond spawn. **CFCheck FAR=0x8 / AppProtection NAMEI: 0 hits** (boot59 wall cleared). SpringBoard still SIGABRT corpse loop (not CFCheck). | out/logs/boot60 + docs/status/logs/boot60/gdb-events-key.txt |

Next: SpringBoard SIGABRT / corpses; `/private/var/containers`; lsd; boot59 NAMEI∩441 Info.plist. Then IOMobileFramebuffer / IOSurface / HID; personas.

## Chunk 24 (boot61 owners/CD rehash → trampolines; boot62 add libobjc-trampolines)

boot61 (ПРОВЕРЕНО cloud, Image.pth20 + mnt2 with `dsc_cd_rehash` + `/private/var` owners):
  - DSC CD stale slots cleared (0 after rehash); CS_KILL / MappedImageCache OK.
  - New wall: SpringBoard/audiomxd `ABORT_WITH_PAYLOAD` —
    `couldn't dlopen libobjc-trampolines.dylib` (ENOENT on `/usr/lib/…` and
    `/System/Cryptexes/OS/usr/lib/…`; "not in dyld cache" is expected — on-disk only).
  - File absent from system APFS root `094-54982-099`; extracted via
    `ipsw extract --files --pattern '.*libobjc-trampolines.*'` from sibling restore DMG
    → Mach-O arm64e dylib 68176 B (`bringup/libobjc-trampolines.dylib`).

Minimal fix (applied to `/tmp/fullroot-mnt2.img` + rebuild script):
  - `lapisfs_add` both `/usr/lib/libobjc-trampolines.dylib` and cryptex OS mirror.
  - `tools/rebuild_mnt2_from_ipsw.sh` vendors/`ipsw extract` fallback + checklist YES.

| boot62 | Image.pth20 | mnt2 + trampolines | qemu11 43G lva=off | ПРОВЕРЕНО (cloud): **0** `libobjc-trampolines` NAMEI/ABORT (wall cleared). SpringBoard×1 stays up past MappedImageCache; backboardd + containermanagerd(_system) + appprotectiond spawn. New wall: **lsd** respawn loop (~15×) with xpcproxy `SIGABRT_PTHREAD_KILL` (proc truncated `roxy`); `kpersona_find_by_type` / persona type 6 errno 3; lockdownd one-shot missing `libramrod.dylib`. | out/logs/boot62 + docs/status/logs/boot62/gdb-events-key.txt |

Next: lsd SIGABRT (LaunchServices DB / Info.plist / personas); `/private/var/db/lsd`; usermanagerd personas; IOMobileFramebuffer/IOSurface/HID.

## Chunk 25 (boot63: CoreServices Info.plist for lsd)

boot62 next wall (ПРОВЕРЕНО): lsd respawn + xpcproxy SIGABRT; NAMEI on
  MobileCoreServices LaunchServices prefs; CoreServices*.framework absent.
ГИПОТЕЗА: same CFBundle Info.plist NULL path as boot59 CFCheck.
Fix: vendored `rootfs/ipsw-coreservices/`; `tools/apply_coreservices_frameworks.sh`.

| boot63 | Image.pth20 | mnt2 + trampolines + CoreServices | qemu11 43G lva=off | ПРОВЕРЕНО (cloud): trampolines still **0**; CoreServices/MobileCoreServices/CoreServicesStore Info.plist NAMEI **0** (added). SpringBoard×1 stays. **lsd wall remains**: ~16 lsd spawns + ~16 xpcproxy SIGABRT (same frames); FAR=0x38 EXC_BAD_ACCESS still seen. ГИПОТЕЗА Info.plist-only for CoreServices: **not sufficient**. Next: personas (`kpersona_find` / usermanagerd), LS DB/prefs content, symbolicate lsd/xpcproxy abort. | out/logs/boot63 + docs/status/logs/boot63/gdb-events-key.txt |

## Chunk 26 (boot64: personas via usermanagerd stub + IOMFB metaclasses + framework batch)

Root cause (ПРОВЕРЕНО boot63): `usermanagerd --init` prints
  `DEVICE HAS EPHEMERAL DATA VOLUME, SKIP INIT` because ADT
  `/chosen/ephemeral-storage=1` (required for keybagd). No PERSONA_OP_ALLOC
  ever runs → launchd `kpersona_find_by_type` ESRCH for types 4/6.

Fix (not instruction-NOP of unrelated code):
  - `tools/patch_fullroot_usermanagerd_ephemeral.py`: rewrite only
    `j__os_variant_uses_ephemeral_storage` stub in usermanagerd to
    `mov w0,#0; ret` (keybagd still uses real libdarwin).
  - personas.plist + UserManagement* Info.plist + FeatureFlags.
  - `bringup/libramrod.dylib` for lockdownd.
  - Batch ~191 on-disk framework resources from boot63∩441 (≤80 MiB).
  - Kernel Image.pth21: `lapis_iomfb.c` registers IOMobileFramebuffer /
    IOSurfaceRoot / IOHIDEventService metaclasses (IOMedia pattern).

| boot64 | Image.pth21 | mnt2 + personas/UMD + IOMFB + fw batch | qemu11 43G lva=off | ПРОВЕРЕНО: `IN/DONE UMD_INIT`; PERSONA op=1 ×5; **0** kpersona_find ESRCH (was spam). trampolines/libramrod 0. IOMFB metaclasses register (3). SpringBoard×1. New wall: usermanagerd daemon ABORT `Daemon failed to load persona manifest` (~12×); UMD_INIT NAMEI on missing `/private/var/keybags/persona.kb` (dir absent). lsd SIGABRT ~17 (may be coupled). | out/logs/boot64 |

| boot65 | Image.pth21 | + `/private/var/keybags` | qemu11 43G lva=off | ПРОВЕРЕНО: persona manifest ABORT **0** (keybags dir fixed); kpersona 0; UMD_INIT OK. SpringBoard×1. **lsd wall remains**: ~16 lsd + ~15 xpcproxy SIGABRT (no ABORT reason string). Next: symbolicate lsd/xpcproxy frames; seed Containers/*; real IOMFB IOService instance. | out/logs/boot65 |

| boot66 | Image.pth21 (iomfb strings) | + mobile Containers + empty LS prefs | qemu11 43G lva=off | ПРОВЕРЕНО: persona OK; SpringBoard×1; **lsd wall root cause symbolicated**: `_LSDefaults systemContainerBaseURL` → objc_exception → abort (boot65 was `systemGroupContainerURL`/`userContainerURL`). FAR=0x38 = libsqlite3 in BSSqlite truncate (secondary). `/private/var/containers` still ENOENT (system tree never created). IOMFB_FDR_Loader notify on IOMobileFramebuffer then exit 1 (metaclass only, no instance). | out/logs/boot66 |


## Chunk 27 (boot66: lsd = missing system container URL; IOMFB instances)

ПРОВЕРЕНО (boot66, DSC slide 0xbf40000, pristine `ipsw dyld a2s`):
- boot65 abort: `-[_LSDefaults systemGroupContainerURL]` / `userContainerURL` → `__LSServerMain`
- boot66 abort: `-[_LSDefaults systemContainerBaseURL]` → `__LSServerMain` (progress after mobile Containers seed; still fatal)
- FAR=0x38 at `libsqlite3` `func+220` via `-[BSSqliteDatabaseConnection truncateDatabaseAndReturnError:]` (secondary)
- `/private/var/containers/{Data/System,Shared/SystemGroup,...}` NAMEI_FAIL — containermanagerd_system never created the system container tree (absent on IPSW; runtime)
- IOMFB metaclasses register; `IOMFB_FDR_Loader` `IOKMATCH notify IOMobileFramebuffer` then SYS_EXIT 1 — no published instance

Fix applied for boot67:
- `tools/apply_lsd_seeds.sh` + `rootfs/ipsw-lsd-seeds/`: FeatureFlags Domain (26 plists incl. LaunchServices), `/private/var/db/lsd`, `/private/var/containers/*` skeleton, MCM cache/staging dirs
- Kernel Image.pth22: `lapis_iomfb.c` publishes real IOService instances (IOMobileFramebuffer / IOSurfaceRoot / IOHIDEventService+HIDServiceSupport) via `registerService`

| boot67 | Image.pth22 | mnt2 + lsd seeds + IOMFB instances | qemu11 43G lva=off | ПРОВЕРЕНО: 3× `published instance`; IOMFB_FDR_Loader **IOKMATCH notify IOMobileFramebuffer** (instance visible) then exit 1. `/private/var/containers` tree created. **lsd still SIGABRT** on `systemContainerBaseURL` — `_container_legacy_lookup(class=12 systemData, id=com.apple.lsd)` returns NULL (entitlements OK: system-container present). containermanagerd matches missing AppleKeyStore. | out/logs/boot67 |

## Chunk 28 (boot68: AppleKeyStore stub + CM_LOOKUP ret trace)

boot67 root cause for remaining lsd wall (ПРОВЕРЕНО a2s + disasm):
  `container_system_path_for_identifier` → `__container_legacy_lookup(0xc /*systemData*/, "com.apple.lsd")`
  → NULL → `_LSDefaults systemContainerBaseURL` throws → abort.
  lsd entitlements include `com.apple.security.system-container` (not an AMFI miss).
  containermanagerd IOKMATCH AppleKeyStore (absent) before creating system containers.

Fix: Image.pth23 publishes AppleKeyStore IOService stub; harness traces CM_LOOKUP/CM_SYSPATH returns;
  seed systemgroup.com.apple.lsd.iconscache + MCM System/ + Backup/SystemContainers.

| boot68 | Image.pth23 | mnt2 + systemgroup seeds + AKS stub | qemu11 43G lva=off | ПРОВЕРЕНО: AKS metaclass+instance; containermanagerd IOKMATCH AKS; **CM_LOOKUP still x0=0** for class=12 com.apple.lsd (MCM cannot create systemData). IOMFB notify still works. Next: DSC stub for container_system_path_for_identifier. | out/logs/boot68 |

| boot69 | Image.pth23 | + DSC stub syspath (lost container_init on .31 replace) | qemu11 43G lva=off | ПРОВЕРЕНО: **regression** — launchd `_container_init(): sandbox_get_container_expected() failed: -1` (syspath stub replaced .31 without re-applying container_init+pac_strip+FIPS). | out/logs/boot69 |

## Chunk 29 (boot70–72: restore DSC bring-up + clear lsd + CoverSheet)

ПРОВЕРЕНО:
- Replacing DSC `.31` requires `tools/reapply_dsc_bringup_patches.sh` (container_init → pac_strip → FIPS reseal → syspath stub → CD rehash).
- boot70: container_init restored but FIPS integrity mismatch → userspace panic `fips`.
- boot71: FIPS resealed; **Early boot complete**; **lsd SIGABRT = 0** (was ~16); CM_SYSPATH stub returns `str='/private/var/containers/Data/System/com.apple.lsd'` (ПРОВЕРЕНО). New wall: SpringBoard SIGABRT loop — CoverSheet `+[CSMainPageContentViewController _phoneListWidth]` via `UIScreen.mainScreen` (nil/zero bounds without display).
- boot72: CoverSheet `_phoneListWidth` → constant 834.0; Image.pth24 IOMFB display size props; MG cache seed.

| boot70 | Image.pth23 | restore container_init+pac_strip+syspath | qemu11 43G lva=off | ПРОВЕРЕНО: past `_container_init`; **FIPS_POST integrity FAILED** (−74) → launchd panic fips. | out/logs/boot70 |
| boot71 | Image.pth23 | + FIPS reseal | qemu11 43G lva=off | ПРОВЕРЕНО: FIPS PASS; Early boot; **lsd wall cleared**; SpringBoard CoverSheet `_phoneListWidth` SIGABRT×14. | out/logs/boot71 |
| boot72 | Image.pth24 | + CoverSheet width stub + IOMFB 2420×1668 props + MG plist | qemu11 43G lva=off | ПРОВЕРЕНО: same SpringBoard SIGABRT stack (CoverSheet symbols); short stub did not change FP chain. | out/logs/boot72 |
| boot73 | Image.pth24 | + onceToken=-1 + width data seed + once-block stub | qemu11 43G lva=off | ПРОВЕРЕНО: unchanged SB SIGABRT×5; FP still shows cold.1 (likely FP-walk noise or non-executing stub path). | out/logs/boot73 |
| boot74 | Image.pth24 | full method+cold.1 overwrite | qemu11 43G lva=off | ПРОВЕРЕНО: still SB SIGABRT; next: assert/err string hooks + SearchUI/FeatureFlags seeds. | out/logs/boot74 |
| boot75 | Image.pth24 | + assert hooks + SearchUI resources + SB FeatureFlags | qemu11 43G lva=off | ПРОВЕРЕНО: SB still SIGABRT; assert string not yet captured (err BP miss). | out/logs/boot75 |
| boot76 | Image.pth24 | DSC hooks `_err` + `abort_with_reason` | qemu11 43G lva=off | partial: hooks present; SB AS still needed. | out/logs/boot76 |
| boot77 | Image.pth24 | re-arm err/abort BPs inside SpringBoard AS | qemu11 43G lva=off | progress toward string capture. | out/logs/boot77 |
| boot78 | Image.pth24 | log gdbpc + x0 string on abort/err DSC hits | qemu11 43G lva=off | **ПРОВЕРЕНО**: ABORT `Assertion failed: (platformBinary), function SBStaticInitializer, file StaticInitializer.m, line 39.`; CON `cs_invalid_page... final status 0x24000000, allowing (remove VALID)`. | out/logs/boot78 |

## Chunk 30 (boot79: CS_VALID on proc_getcsflags return)

ПРОВЕРЕНО (boot78 + DSC disasm `_SBStaticInitializer` @ 0x224040bd4):
- Check: `SecTaskGetCodeSignStatus` → `csops_task(CS_OPS_STATUS)` must satisfy
  `(status & 0x0fffffff & 0xfc000001) == 0x04000001` (CS_VALID|CS_PLATFORM_BINARY),
  else `os_variant_has_internal_content("com.apple.SpringBoard")` or assert.
- Stored flags after soft `cs_invalid_page` are `0x24000000` (SIGNED|PLATFORM, **VALID cleared**).
- Prior cave ORed only SIGNED|PLATFORM → status still fails the bit0 requirement.

Fix: `tools/patch_kernel_platform_binary.py` also ORs `CS_VALID` (cave 0x...6100..6113).
Image.pth25 = Image.pth24 + that patch (sha256 8cafa8061fc7192d91a6061e9e3562088c29ddafd6116cc79a47d0e5c53f63c6).

| boot79 | Image.pth25 | OR CS_VALID in proc_getcsflags return | qemu11 43G lva=off | **ПРОВЕРЕНО**: `SBSTATIC_STATUS status=0x24000001 pass=1`; **0** SpringBoard SIGABRT / platformBinary assert; SB+backboardd+runningboardd reach `service state: running`. Next: XPC `com.apple.runningboard` still x0=0 (runningboardd IOKMATCH **IOGPU** absent); UserEventAgent SIGKILL on `/usr/lib/dyld` invalid pages (status 0x25003201 deny); no SB/backboard IOKMATCH for IOMFB yet. | out/logs/boot79 |

## Chunk 31 (boot80: IOGPU + always-allow cs_invalid + SB seeds)

Fix:
- `lapis_iomfb.c`: publish **IOGPU** + **AppleCLCD** stubs (runningboardd gated on IOGPU).
- `patch_kernel_cs_invalid_always_allow.py`: soft path always allow (UserEventAgent dyld SIGKILL).
- `apply_springboard_seeds.sh`: mobile Library/SpringBoard + caches + container dirs.
Image.pth26 sha256 d5441392b10ee7ed96b4b5779fca128319d8943b8ce22702ab5dd8f229df21b7.

| boot80 | Image.pth26 | IOGPU/CLCD stubs + cs_invalid always-allow + SB seeds | qemu11 43G lva=off | ПРОВЕРЕНО: IOGPU/CLCD metaclasses ok; SBSTATIC pass=1; **0 SIGABRT**; SB/backboardd/runningboardd running. **cs_invalid deny still** (tbnz@0x…3710 bypassed late cbnz patch). XPC `com.apple.runningboard` still x0=0; runningboardd NAMEI missing LifecyclePolicy + RunningBoard plists. | out/logs/boot80 |

## Chunk 32 (boot81: soft-entry always-allow + RunningBoard seeds)

- `patch_kernel_cs_invalid_always_allow.py`: soft entry 0x…3700 → b allow epilogue (covers tbnz deny escape).
- `apply_runningboard_seeds.sh`: RunningBoard/*.plist + LifecyclePolicy/domains.plist + 133 DomainAttributes.
Image.pth27 sha256 14511e5297b2e905a80eee8a3de3295a93c8c4a1868504fe9c25b2c315a042c8.

| boot81 | Image.pth27 | soft-entry allow + RunningBoard/LifecyclePolicy seeds | qemu11 43G lva=off | ПРОВЕРЕНО: soft-entry insufficient — early `cbnz@0x…31ac` → deny@0x…379c still kills UserEventAgent (10× denying). SBSTATIC pass=1; 0 SIGABRT; RB/SB/backboard running. | out/logs/boot81 |

## Chunk 33 (boot82: early-deny bypass + deny-block redirect + dyld refresh)

- Patch early cbnz@0x…31ac → fallthrough; deny-block@0x…379c → allow epilogue.
- Refresh `/usr/lib/dyld` from IPSW (macho_cd_rehash no-op; replace onto mnt2).
Image.pth28.

| boot82 | Image.pth28 | early-deny bypass + dyld refresh | qemu11 43G lva=off | **ПРОВЕРЕНО**: **denying SIGKILL = 0** (was 10+); **0 SIGABRT**; SBSTATIC pass=1×3; IOMFB_FDR **IOKMATCH IOMobileFramebuffer** then exit 1; runningboardd **IOKMATCH IOGPU** late (timeout before mach check-in); XPC runningboard still early-race x0=0. | out/logs/boot82 |

## Chunk 34 (boot83: longer run + RB/Trial/IOMFB seeds)

- Empty `runningboardAdditionalEntitlementsConfiguration.plist`
- Trial NamespaceDescriptors dirs + COREOS GMPOWER plist if present
- `/usr/bin/Info.plist` for IOMFB_FDR_Loader
- Timeout 1200s so runningboardd can check in after IOGPU match

| boot83 | Image.pth28 | + RB/Trial/IOMFB seeds; T=1200s | qemu11 43G lva=off | **ПРОВЕРЕНО**: deny=0; SBSTATIC pass=1; IOGPU+IOMFB IOKMATCH; IOMFB_FDR exit 1 (no userclient); XPC `com.apple.runningboard` still x0=0 **after** IOGPU match; missing KRExperiments.framework + Trial NamespaceDescriptor paths. | out/logs/boot83 |

## Chunk 35 (boot84–85: newUserClient + KRExperiments)

- `lapis_iomfb.c`: `lapis_new_user_client{,_simple}` allocate/init/attach/start real `IOUserClient`.
- **boot84 ГИПОТЕЗА rejected**: runtime `ml_static_protect` on `__TEXT_EXEC` panics (`ml_static_protect.cold`).
- **Fix**: `patch_kernel_ramfs_replace.py` statically `B`-patches `IOService::newUserClient` dict@`0x…253c` + simple@`0x…27ec` onto the lapis stubs (PAC-safe: signed vtable still targets stock VA; body redirected).
- `tools/apply_krexperiments_seeds.sh`: KRExperiments.framework + Trial GMPOWER NamespaceDescriptors.
- GDB: `IOSOPEN` / `IOSOPEN_RET` on `_is_io_service_open_extended`.
Image.pth30 sha256 c29096ad1b5df320a2163660a4697ae69e7dbfcc7119d49fd6cb094ce11f5180.

| boot84 | Image.pth29 | runtime ml_static_protect nuc | qemu11 43G lva=off | **ПРОВЕРЕНО**: early PANIC in `ml_static_protect.cold` during `lapis_iomfb_register` | out/logs/boot84 |
| boot85 | Image.pth30 | static NUC B-patch + KRExperiments seeds | qemu11 43G lva=off | **ПРОВЕРЕНО**: no panic; metaclasses published; **IOSOPEN→nuc ok→x0=0** (powerd); IOGPU+IOMFB IOKMATCH; KRExperiments `.pb` found (only `_fbs.bin` was missing); **XPC `com.apple.runningboard` never looked up**; runningboardd silent after IOGPU match (no check-in). | out/logs/boot85 |

## Chunk 36 (boot86–87: AGX + no RX-blob statics)

- **boot86 ПРОВЕРЕНО**: PANIC writing `g_nours` — ramfs blob is RX `__TEXT_EXEC`; no writable statics allowed.
- Publish AGXAccelerator + IOAccelerator with MetalDevice props; keep attach+registerService (no start); ungated nuc (no pointer list); seed empty GMPOWER `_fbs.bin`.
Image.pth32 sha256 9e4f11e70ae4a73effabf7818abd4ee0d825404b0da96354942697e3f73d8334.

| boot86 | Image.pth31 | start+AGX+gated nuc (RX statics) | qemu11 43G lva=off | **ПРОВЕРЕНО**: PANIC on `g_nours` store in RX blob | out/logs/boot86 |
| boot87 | Image.pth32 | AGX+Metal props; no RX statics | qemu11 43G lva=off | **ПРОВЕРЕНО**: no panic; stubs published; IOGPU match; nuc ok; XPC runningboard still x0=0 after IOGPU. | out/logs/boot87 |

## Chunk 37 (boot88: IOGPUDevice + IOKMATCH_RET)

- Publish **IOGPUDevice** (Metal/IOGPU.framework open path).
- GDB: `IOKMATCH_RET` logs getMatchingService x0 (prove whether IOGPU match returns non-null).
Image.pth33 sha256 98e73734c385452ba5ab1ce062eb07d35ee4d0d27150300743b282d0434bb376.

| boot88 | Image.pth33 | IOGPUDevice + IOKMATCH_RET | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOKMATCH_RET IOGPU x0=0xe00002f0 (kIOReturnOffline) — stubs never successfully matched; same for AppleKeyStore. | out/logs/boot88 |

## Chunk 38 (boot89: attach IOResources + start + publishResource)

- **Root cause (boot88)**: `getMatchingService(IOGPU)` returns `kIOReturnOffline` (0xe00002f0). Prior IOKMATCH logs were call-only false positives.
- Fix: attach to `getResourceService()`, call `start()`, `registerService` + `publishResource(name)`.
Image.pth34 sha256 ba865e29eae43313004de830e057bb8bef83af48aa7528290466944e6e2ba9c9.

| boot89 | Image.pth34 | IOResources attach+start+publishResource | qemu11 43G lva=off | **ПРОВЕРЕНО**: still IOKMATCH_RET Offline 0xe00002f0 for IOGPU after start+publishResource. | out/logs/boot89 |

## Chunk 39 (boot90: self-match probe)

- Still Offline after IOResources attach/start/publishResource.
- Add kernel self-test: `serviceMatching` / `nameMatching` / `resourceMatching` + `copyMatchingService("IOGPU")` at publish time.
Image.pth35 sha256 99718069db4ed8099be5f5d09cef02d2d82823cbad0ef9e1785f9bcd38fd5a81.

| boot90 | Image.pth35 | self-match IOGPU probe | qemu11 43G lva=off | **ПРОВЕРЕНО**: serviceMatching/nameMatching FAIL; **resourceMatching OK**. | out/logs/boot90 |

## Chunk 40 (boot91: copyMatchingService → resourceMatching fallback)

- Hand-rolled metaclass breaks `metaCast` / serviceMatching; `publishResource` + resourceMatching works.
- B-patch `IOService::copyMatchingService` onto `lapis_copyMatchingService` which falls back to resourceMatching for our stub names.
Image.pth36 sha256 a023870197e968fed3c9ab44a2cd088dfc0f58cd0e0ee616b4078000d1a34ba6.

| boot91 | Image.pth36 | copyMatchingService resource fallback | qemu11 43G lva=off | **ПРОВЕРЕНО**: self service/name/resourceMatching IOGPU ok; runningboardd IOGPU match+open x0=0 but **no nuc** (opened IOResources publisher); SB past SBSTATIC; SpringBoard respawns on cfprefsd. | out/logs/boot91 |

## Chunk 41 (boot92: return publishResource stub, not IOResources)

- resourceMatching/`copyMatchingService` returns **IOResources**, so open hits `IOResources::newUserClient` (Unsupported) — RB never gets a real IOGPU client.
- Hook now `copyProperty(IOResources, name)` for known stubs; also B-patch `IOResources::newUserClient` → `lapis_new_user_client`.
Image.pth37 sha256 a671daafe9d06d944533e44dcfc3e2eda286af385bb332a0a65bea1ce8716d2b.

| boot92 | Image.pth37 | published-stub match + IOResources NUC | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOGPU match → **nuc ok** (real stub, not IOResources); backboardd still XPC `com.apple.runningboard` x0=0 after open; SB SIGTRAP respawn. | out/logs/boot92 |

## Chunk 42 (boot93: IOUserClient::externalMethod success stub)

- After open, clients `IOConnectCall*` → stock `IOUserClient::externalMethod` returns Unsupported.
- B-patch `externalMethod` → `mov w0,#0; ret`; GDB logs selectors; fix `cur_pname` truncation-on-error (`proc=C`).
Image.pth38 sha256 65f761b218384a3e86e8d79d52cb22ce5d1fa0e4dcb82f4622515d9dd11bba6d.

| boot93 | Image.pth38 | externalMethod success + pname fix | qemu11 43G lva=off | **ПРОВЕРЕНО**: runningboardd IOGPU match→nuc→IOEXTMETHOD sel 2/0/4 x0=0; then **getMatchingServices IONameMatch IOCoreSurfaceRoot** (plural path, no copyMatchingService). | out/logs/boot93 |

## Chunk 43 (boot94: getMatchingServices + IOCoreSurfaceRoot)

- Publish `IOCoreSurfaceRoot`; hook `getMatchingServices` to return a one-element `OSCollectionIterator` over the published stub.
Image.pth39 sha256 9d794a40f61a4497d57d6c2c95d3543fbbfbd7909d47810748cd074b23a5cc8a.

| boot94 | Image.pth39 | GMS iterator + IOCoreSurfaceRoot | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOCoreSurfaceRoot GMS→nuc→IOEXTMETHOD sel=13; XPC runningboard still x0=0. | out/logs/boot94 |

## Chunk 44 (boot95: clientMemoryForType buffer stub)

- After IOSurface open, map shared memory via .
- B-patch onto  → 64KiB .
Image.pth40 sha256 d640fc21f71a3b4d8e2abc5ef3e572f6466321165470c312a950ba0d726be10e.

| boot95 | Image.pth40 | clientMemoryForType buffer stub | qemu11 43G lva=off | **ПРОВЕРЕНО**: no IOCMEM after IOSurface sel=13; XPC still x0=0 — bare externalMethod success left output buffers untouched. | out/logs/boot95 |

## Chunk 45 (boot96: externalMethod zeros outputs)

- Replace movz-success with  that zeros scalar/structure outputs.
Image.pth41 sha256 598df825fda1cc948902d8bc6430029fdd5bdc242b5efdee30a055c567f01b29.

| boot96 | Image.pth41 | externalMethod zero-fill outputs | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOGPU sel=2 stout=536, sel=0/4=64/32, IOSurface sel=13 stout=40; all-zero still no XPC. | out/logs/boot96 |

## Chunk 46 (boot97: seed non-zero GPU/surface method outputs)

- Seed version/present/flags in structure outputs (esp. IOGPU sel=2 536-byte caps).
Image.pth42 sha256 692dc69df8a4ccef242eb01167fb9d0d748728394168d6284008a8790c499abb.

| boot97 | Image.pth42 | seed GPU caps in externalMethod | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOGPU sel=2/0/4 + IOSurface sel=13 all x0=0; still no post-sel13 RB activity. RE: IOSurface connect requires `*(uint16*)(limits+0x22)==1` else closes; prior seed left 0. XPC_LOOKUP x0=0 is likely KERN_SUCCESS (out-port), not failure. | out/logs/boot97 |

## Chunk 47 (boot98: IOSurface limits gate + real IOGPU layouts)

- `lapis_external_method`: sel=13 sets halfword @0x22=1; sel=2 event_machine count@0x14=0; sel=0 seeds `IOGPUDeviceConfigData`; sel=4 non-zero filter.
- GDB: richer `xpc_look_up_endpoint` arg dump (kr vs port*).
Image.pth43 sha256 d4f568f9ad4d0462d4922f32744dc0c000fdcdf8fd43dfd4ca0949299cbeb56c.

| boot98 | Image.pth43 | IOSurface +0x22=1 + IOGPU struct layouts | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOGPU sel=2/0/4 → IOSurface sel=13 → **sel=32** (connect held). IOMFB_FDR: IOKMATCH notify then **SYS_EXIT 1**, no IOSOPEN. RE root cause: notify → `installNotification` → `copyExistingServices` (not GMS); CES uses matchPassive/metaCast and misses hand-rolled metaclasses. cfprefsd[103] **burning CPU** (~57% / 180s, mostly sys) while SpringBoard keeps looking up cfprefsd. | out/logs/boot98 |

## Chunk 48 (boot99: copyExistingServices hook for IOMFB notify)

- `lapis_copyExistingServices`: same publishResource stub iterator as GMS; Image B-patches `IOService::copyExistingServices`.
- Patcher accepts stale B from prior blob (symbol shift on ramfs growth).
Image.pth44 sha256 f34b4b10dd516d5f236c0ff0d6d31acb40e88c7efd4f7a181bcb9af378e9a095 (ramfs 554af144…).

| boot99 | Image.pth44 | CES hook for AddMatchingNotification | qemu11 43G lva=off | **ПРОВЕРЕНО**: **CES via resource** on IOMFB notify (hook works). RB IOGPU→IOSurface sel=13→32 still OK. FDR **hung** (no IOSOPEN, no exit) — MIG `addMatchingNotification` invokes in-kernel handlers on CES existing; skips `+0x48` bit0 inactive. cfprefsd still burns CPU. | out/logs/boot99 |

## Chunk 49 (boot100: clear IOMFB inactive state for notify invoke)

- After `registerService`, force `__state` at +0x48 to registered|matched|firstPublish|firstMatch (0x1e), clear inactive.
- CES path re-asserts the same before returning the stub iterator.
Image.pth45 sha256 243c21885ed0bd55517150482219531b5eed983c54db6bf61ec6614ab0af2f14.

| boot100 | Image.pth45 | IOMFB state bits for notify invoke | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES via resource + RET x0=0; RB sel=32 OK; state@+0x48 confirmed by stock CES `ldr w8,[x21,#0x48]`. FDR still **hung** (no IOSOPEN) — root cause NOT inactive: `OSCollectionIterator::withCollection` leaves `valid=false`; in-kernel `getNextObject` (addMatchingNotification existing drain @0xfffffe0007ac0974) returns NULL until `reset()`. GMS looked fine because userland `IOUserIterator` resets. cfprefsd still burns CPU; shm_open names OK, oflag was read from wrong uap offset (@0x10=mode, not @8). | out/logs/boot100 |

## Chunk 50 (boot101: OSCollectionIterator reset for CES invoke)

- `lapis_stub_iterator`: call `OSCollectionIterator::reset` after `withCollection` so CES→addMatchingNotification→invokeNotifier sees the stub.
- gdb `shm_open` oflag @uap+8, mode @uap+0x10.
Image.pth46 (ramfs cdd66f32…).

| boot101 | Image.pth46 | iterator reset before CES return | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES+RET ok; RB sel=32 ok; shm oflag@+8 now 0 (plausible). FDR still **hung** (no IOSOPEN) — reset insufficient alone. | out/logs/boot101 |

## Chunk 51 (boot102: force match handler + OSSet CES)

- `lapis_addMatchingNotification`: after stock drain, call handler(target,ref,stub,notifier) for lapis match names.
- CES stub iterator uses OSSet (stock) + peek/re-reset; Image B-patches AMN @0xfffffe0007ac0930.
Image.pth47 (ramfs 19ea0455…).

| boot102 | Image.pth47 | force match handler after AMN | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES peek ok + **forced match handler** ran; RB sel=32 ok. FDR still hung — handler queues without ping because `armed` defaults 0 (`ldrb [userNotify,#0x48]`). | out/logs/boot102 |

## Chunk 52 (boot103: arm userNotify before forced handler)

- Before calling match handler, set `IOServiceUserNotification::armed` @+0x48 so first queue sendPings and wakes FDR runloop.
Image.pth48 (ramfs 10c39b40…).

| boot103 | Image.pth48 | arm userNotify before force handler | qemu11 43G lva=off | **ПРОВЕРЕНО**: forced handler ran; still no IOSOPEN. Post-orig arm is too late — stock CES drain already queued with armed=0 (count>0 → force won't ping). | out/logs/boot103 |

## Chunk 53 (boot104: arm userNotify BEFORE stock AMN drain)

- Set armed@+0x48 on IOMobileFramebuffer notify target before calling orig addMatchingNotification so first invoke sendPings.
Image.pth49 (ramfs a9e828e9…).

| boot104 | Image.pth49 | pre-arm userNotify before AMN orig | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES + forced handler + RET x0=0; still no FDR IOSOPEN. Pre-arm pings *inside* MIG before FDR waits on the port — message lost. `handler` only sendPings when `armed && count==0`; `getNextObject` sets armed only after userland empties the queue. | out/logs/boot104 |

## Chunk 54 (boot105: deferred ping after AMN returns)

- After force-queue, `thread_call_allocate` + `thread_call_enter` → sleep 200ms → drain (arms) → `handler` re-add → sendPing once FDR listens.
Image.pth50 (ramfs a11cef40…).

| boot105 | Image.pth50 | deferred IOMFB notify ping | qemu11 43G lva=off | **ПРОВЕРЕНО**: scheduled deferred ping ran; **PANIC** `brk #0xbffc` at `getNextObject` symbol `0x7b11668` (poison stub → `IOUserClient.cpp:1094`). Real drain is `copyNextObject` @`0x7b11678`. lr in lapis deferred callback. | out/logs/boot105 |

## Chunk 55 (boot106: deferred ping via copyNextObject)

- Same deferred path but call `copyNextObject` (retained) instead of poisoned `getNextObject`.
Image.pth51.

| boot106 | Image.pth51 | deferred ping via copyNextObject | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES+forced+scheduled+**deferred ping** (no panic); still **no FDR IOSOPEN**. 200ms still before FDR mach_msg wait under QEMU — ping lost; queue holds stub, nobody wakes. cfprefsd still burns CPU; SpringBoard respawns. | out/logs/boot106 |

## Chunk 56 (boot107: long deferred ping + retries)

- Keep copyNextObject drain; initial IOSleep **3000ms**, then **8×** drain→arm→AMN-handler requeue→ping with 1s gaps (heap ctx with handler/ref/notifier — same path as force).
- Tag IOMFB stub `IOMFBMainDisplay` + `IOMFBDisplayExternal=0` for GetMainDisplay/embedded match.
Image.pth52 (ramfs 14e4fa7b…).

| boot107 | Image.pth52 | long deferred IOMFB notify retries | qemu11 43G lva=off | **ПРОВЕРЕНО (partial)**: superseded mid-boot. RE: `is_io_iterator_next` uses OSIterator vtable+0x148 = `IOServiceUserNotification::getNextObject` which is **poison brk** @`0x7b11668` (boot105). Even a perfect ping would panic on FDR's IOIteratorNext. | out/logs/boot107 |

## Chunk 57 (boot108: getNextObject → copyNextObject)

- Patch poison `getNextObject` stubs (`IOServiceUserNotification` + `IOUserIterator`) to `b copyNextObject`.
- Keep boot107 long deferred ping retries + IOMFBMainDisplay props.
Image.pth53.

| boot108 | Image.pth53 | getNextObject→copyNextObject + long ping | qemu11 43G lva=off | **ПРОВЕРЕНО**: getNextObject fix holds (**8× deferred ping**, **no brk panic**); CES+forced+pings all ran; **still no FDR IOSOPEN**. RB IOGPU→sel=13→32 OK. cfprefsd still burns CPU (never creates `apple.cfprefs.*.daemonv1`; clients O_CREAT it). | out/logs/boot108 |

## Chunk 58 (boot109: deferred-only IOMFB delivery + IOITER probe)

- CES **skips** IOMobileFramebuffer (stock empty during AMN); no force-queue — deferred alone delivers after FDR can arm on empty Next.
- gdb: `_is_io_iterator_next` logs for IOMFB_FDR/backboardd.
Image.pth54.

| boot109 | Image.pth54 | deferred-only IOMFB + IOITER probe | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES skip + empty Next (arm) + deferred ping → **IOITER_NEXT x0=0** (stub delivered); **still no FDR IOSOPEN**. RE: `IOMobileFramebufferGetMainDisplay` does NOT open the notify object — it needs `AppleParavirtGPU.DisplayPortCount` (getParavirtDisplayCount) + FB enumerate wait, then open. Without ParavirtGPU Ref_FB stays wrong → drain-without-open. RB IOGPU→sel OK. cfprefsd still burns CPU. | out/logs/boot109 |

## Chunk 59 (boot110: AppleParavirtGPU DisplayPortCount=1)

- Publish `AppleParavirtGPU` stub with `DisplayPortCount=1` so GetMainDisplay's getParavirtDisplayCount expects exactly one FB (our IOMFB).
- Keep boot109 deferred-only IOMFB delivery + getNextObject→copyNextObject.
Image.pth55 (ramfs 4a86bb4e…).

| boot110 | Image.pth55 | AppleParavirtGPU + DisplayPortCount=1 | qemu11 43G lva=off | **ПРОВЕРЕНО**: Paravirt IOService published; FDR still Next-ok / no IOSOPEN. **No** `IOKMATCH` for AppleParavirtGPU — GetMainDisplay only calls GMS(Paravirt) when an `/arm-io` child has `compatible=AppleParavirtGPU`. Existing `disp0` was `lapis,display` so the Paravirt block was skipped. cfprefsd still burns CPU. | out/logs/boot110 |

## Chunk 60 (boot111: ADT disp0 compatible=AppleParavirtGPU)

- ADT `/arm-io/disp0/compatible` → `AppleParavirtGPU` so getDisplayListNumber enters Paravirt GMS + reads our stub's `DisplayPortCount=1`.
- Keep deferred IOMFB ping + named match logs (ParavirtGPU/IOMFB).
Image.pth56.

| boot111 | Image.pth56 | ADT disp0 compatible=AppleParavirtGPU | qemu11 43G lva=off | **ПРОВЕРЕНО**: still no Paravirt GMS / no FDR IOSOPEN. RE of GetMainDisplay XOR-compare: DT `compatible` must be **`paravirtualizedgraphics,gpu`** (not `AppleParavirtGPU`); that string is only the IOServiceMatching class name after the DT hit. | out/logs/boot111 |

## Chunk 61 (boot112: ADT compatible=paravirtualizedgraphics,gpu)

- `/arm-io/disp0/compatible` → `paravirtualizedgraphics,gpu` (matches GetMainDisplay inline compare).
- Keep AppleParavirtGPU IOService + DisplayPortCount=1 + deferred IOMFB ping.
Image.pth57.

| boot112 | Image.pth57 | ADT disp0 compatible=paravirtualizedgraphics,gpu | qemu11 43G lva=off | **ПРОВЕРЕНО**: **IOKMATCH services AppleParavirtGPU** + match via resource ParavirtGPU + Next ok; IOMFB notify + deferred + Next ok; **still no FDR IOSOPEN**. Likely Ref_FB=2 (`device_type=display-subsystem` increment + `DisplayPortCount=1`) while only one IOMFB stub is delivered. | out/logs/boot112 |

## Chunk 62 (boot113: drop disp0 device_type — Ref_FB=1)

- Keep `compatible=paravirtualizedgraphics,gpu`; **remove** `device_type` so getDisplayListNumber only counts Paravirt `DisplayPortCount=1`.
Image.pth58.

| boot113 | Image.pth58 | disp0 no device_type; Paravirt count=1 | qemu11 43G lva=off | **ПРОВЕРЕНО**: Paravirt GMS + IOMFB Next still ok; **still no FDR IOSOPEN**. Ref_FB double-count was not the blocker — after Next, framework likely drops the stub via failed `IOObjectConformsTo` (hand-rolled metaCast). | out/logs/boot113 |

## Chunk 63 (boot114: force IOObjectConformsTo for lapis stubs)

- B-patch `is_io_object_conforms_to` → if stock metaCast fails and classname is a lapis match name and `obj` is our published stub, set `*out=1`.
- Keep Paravirt DT compatible + no device_type + deferred IOMFB.
Image.pth59.

| boot114 | Image.pth59 | conformsTo force (pointer-eq only) | qemu11 43G lva=off | **ПРОВЕРЕНО**: no `conformsTo force` log — either hook not hit or pointer-eq failed. Still Paravirt+Next, no FDR IOSOPEN. | out/logs/boot114 |

## Chunk 64 (boot115: conformsTo force-any + gdb probe)

- Force `*out=1` for any lapis match classname when a published stub exists (ignore pointer eq).
- gdb: log `_is_io_object_conforms_to` for FDR/backboardd.
Image.pth60.

| boot115 | Image.pth60 | conformsTo force-any + CONFORMS probe | qemu11 43G lva=off | **ПРОВЕРЕНО**: zero `CONFORMS` / no `conformsTo force*` — FDR never calls IOObjectConformsTo. Paravirt GMS + IOMFB deferred Next still ok; **still no FDR IOSOPEN**. RE: after Next, `iomfb_match_callback` increments Act_FB but only fulfills `pthread_dependency` when Act==Ref; if DisplayPortCount CFNumber fails Ref stays 0 → GetMainDisplay waits forever → never Open. | out/logs/boot115 |

## Chunk 65 (boot116: DSC fulfill NOP + external bool + faster ping)

- `tools/patch_dsc_iomfb_fulfill.py`: NOP `b.ne` @ unslid `0x2288058c8` in DSC `.44` so Act_FB increment always fulfills (single-display).
- IOMFB stub: `external=false` bool (match_callback SearchCFProperty key); deferred ping first sleep 500ms ×12.
- Image.pth61; DSC CD rehash after .44 patch.
Image.pth61.

| boot116 | Image.pth61 | DSC fulfill NOP + external bool | qemu11 43G lva=off | **ПРОВЕРЕНО**: 500ms ping raced inside AMN (all deferred before `IOKMATCH_RET`); post-AMN Next got prequeued stub; **still no FDR IOSOPEN**. Fulfill-only NOP insufficient — either Act++ gated by w22 or main-flag (+0x98) never set so GetMainDisplay returns NoDevice without Open. | out/logs/boot116 |

## Chunk 66 (boot117: Act++ + main-check force + 2s ping)

- DSC `.44`: also NOP match_callback `cbz w22` before Act_FB++; replace GetMainDisplay `b.eq` main-flag test with unconditional `b` (first Disp slot always opens).
- Deferred ping first sleep 2s (avoid AMN race); gdb always logs FDR/backboard/SB IOSOPEN.
- Image.pth62 + CD rehash.
Image.pth62.

| boot117 | Image.pth62 | Act++/main force + 2s ping | qemu11 43G lva=off | **ПРОВЕРЕНО**: 2s ping after AMN + Next ok; **still no FDR IOSOPEN**. Act/main force insufficient — GetMainDisplay still not reaching Open after populate. | out/logs/boot117 |

## Chunk 67 (boot118: GetMainDisplay → Matching/Open cave)

- Replace `IOMobileFramebufferGetMainDisplay` with cave: Matching → GetMatchingServices → Next → `IOMobileFramebufferOpen` (bypasses populate wait/main-flag).
- Image.pth62 + DSC `.44` cave + CD rehash.
Image.pth62.

| boot118 | Image.pth62 | GetMainDisplay open cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: cave Matching→GetMatchingServices hit (`IOKMATCH services` IOMFB + resource match x0=0); then **EXC FAR=0x574** (null+0x574) before FDR IOSOPEN. Master GOT used d48; AppleDisplayManagerOpen uses **d50** + singular `GetMatchingService` @0x22c6ce7d0. | out/logs/boot118 |

## Chunk 68 (boot119: singular GetMatchingService + master d50)

- Cave: Matching → GetMatchingService (0x22c6ce7d0) → Open; master GOT **0xd50** (not d48); no Next/Release.
- Image.pth63 (=pth62 kernel) + DSC `.44` cave + CD rehash.
Image.pth63.

| boot119 | Image.pth63 | singular GMS cave d50 | qemu11 43G lva=off | **ПРОВЕРЕНО**: singular `IOKMATCH service` IOMFB + resource x0=0; **no FAR=0x574**; **still no FDR IOSOPEN** — userspace service port likely 0 so cave returned NoDevice before Open. | out/logs/boot119 |

## Chunk 69 (boot120: singular then plural+Next, master d50)

- Cave: try GetMatchingService; on null, Matching again → GetMatchingServices → Next → Release → Open. Master GOT d50.
- Image.pth64 (=pth63 kernel) + DSC `.44` + CD rehash.
Image.pth64.

| boot120 | Image.pth64 | singular→plural Next cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: only singular IOKMATCH (no services); no FDR IOSOPEN. Root cause: `IOServiceGetMatchingServices` ABI is `(master, matching, &iterator)` → kern_return_t; cave treated x0 as iterator. Singular returns null userspace port for publishResource stubs. | out/logs/boot120 |

## Chunk 70 (boot121: correct GetMatchingServices &iterator ABI)

- Cave: Matching → GetMatchingServices(master, matching, &it) → Next → Release → Open; master d50.
- Image.pth65 + DSC `.44` + CD rehash.
Image.pth65.

| boot121 | Image.pth65 | plural &iterator ABI cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: `IOKMATCH services` + `IOITER_NEXT` (kr=0) but **no FDR IOSOPEN** — userspace Next object null (stub OSSet iterator likely empty/use-after-release). | out/logs/boot121 |

## Chunk 71 (boot122: stub iterator peek+retain)

- `lapis_stub_iterator`: peek getNextObject before hand-off; log EMPTY/ok; safer set retain.
- Image.pth66 ramfs replace; DSC cave unchanged from boot121.
Image.pth66.

| boot122 | Image.pth66 | stub iter peek + plural cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: `stub iter ok` then IOITER_NEXT kr=0 but **no FDR IOSOPEN** — peek consumed the sole OSSet entry; `reset()` did not re-arm for userspace Next. | out/logs/boot122 |

## Chunk 72 (boot123: fresh stub iterator after peek)

- After peek validates non-empty, rebuild a fresh `OSCollectionIterator` for the caller.
- Image.pth67 + DSC cave from boot121.
Image.pth67.

| boot123 | Image.pth67 | fresh stub iter after peek | qemu11 43G lva=off | **ПРОВЕРЕНО**: fresh `stub iter ok` + IOITER_NEXT kr=0; **still no FDR IOSOPEN** — Next object and/or IOMFB Open wrapper still failing before kernel open. | out/logs/boot123 |

## Chunk 73 (boot124: cave calls IOServiceOpen directly)

- After Next, call `IOServiceOpen` @0x22c6ce800 (skip IOMFB Open alloc wrapper); gdb logs IOITER *out obj.
- Image.pth68 (=pth67) + DSC cave.
Image.pth68.

| boot124 | Image.pth68 | direct IOServiceOpen cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: Next obj=0x3ba480 → **FDR IOSOPEN + nuc ok (x0=0)** — first FDR open of IOMFB stub. Raw connect in *out → FDR FAR=0x111f (needs IOMFB object wrap). | out/logs/boot124 |

## Chunk 74 (boot125: alloc IOMFB object + IOServiceOpen)

- Cave: Next → alloc helper @0x2288065e4 → str service@+0x10 → IOServiceOpen into +0x14 → *out=obj.
- Image.pth69 + DSC cave.
Image.pth69.

| boot125 | Image.pth69 | IOMFB alloc+Open wrap cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: Next obj=0x99a6d6c0 but **no FDR IOSOPEN** — full ALLOC helper @0x2288065e4 (typed-malloc + CFPrefs init) never returns a usable object to the cave. | out/logs/boot125 |

## Chunk 75 (boot126: light typed-malloc object + Open)

- Skip full ALLOC CFPrefs path. After Next: ensure malloc-type slot → typed-malloc(0xf30) → zero + service@+0x10 + flag@+0xc08 → IOServiceOpen into +0x14 → *out=obj. Null → raw IOSOPEN fallback (boot124).
- Image.pth70 (=pth69) + DSC cave.
Image.pth70.

| boot126 | Image.pth70 | light typed-malloc + Open cave | qemu11 43G lva=off | **ПРОВЕРЕНО**: Next → **FDR IOSOPEN + nuc ok (x0=0)**; **no FAR=0x111f** (heap object works). FDR then `SYS_EXIT rval=1` with no externalMethod — object fields still incomplete vs full ALLOC. | out/logs/boot126 |

## Chunk 76 (boot127: richer light-object field init)

- After typed-malloc+zero: set +0xc08/+0x838=1, +0xdc8=-1, copy 16B const from 0x228832f40 → +0x880, optional init at +0x840 via stub 0x22c6ceba0; then wire+Open.
- Image.pth71 (=pth70) + DSC cave.
Image.pth71.

| boot127 | Image.pth71 | richer IOMFB object init + Open | qemu11 43G lva=off | **ПРОВЕРЕНО**: FDR IOSOPEN+nuc, no FAR=0x111f (same as boot126). FDR `exit=1` is post-open (GetServiceObject ok path; missing AMFDR/panel cal). SpringBoard reaches `XPC_LOOKUP cfprefsd` (x0=0) — next wall. | out/logs/boot127 |

## Chunk 77 (boot128: cfprefsd daemon shm + XPC port dump)

- IOMFB open path holds. Next wall: cfprefsd never creates `apple.cfprefs.*.daemonv1` (clients get ENOENT on O_RDWR); FeatureFlags dir seeded for init_featureflags; XPC_LOOKUP_RET now dumps out-port words.
- Image.pth72 (=pth71) + FeatureFlags/ManagedPreferences dirs.
Image.pth72.

| boot128 | Image.pth72 | cfprefsd shm + XPC port diagnostics | qemu11 43G lva=off | **ПРОВЕРЕНО**: cfprefsd spawns; SB XPC ports ok (`x0=0`); **no daemonv1 create** (keybagd ENOENT). Cause: shmem extension path. | out/logs/boot128 |

## Chunk 78 (boot129: CFPrefsDaemon shmem O_CREAT force)

- boot128 **ПРОВЕРЕНО**: cfprefsd spawns (#147/#149); XPC ports live for SB (`x0=0`, port words); **cfprefsd never shm_open's `apple.cfprefs.*.daemonv1`**; keybagd `daemonv1` O_RDWR → ENOENT (`x0=2`). RE: `-[CFPrefsDaemon shmem]` @0x1835f32d4 `tbz` → sandbox_extension(`ipc-posix-shm-write-data`) when flag@0x1e9414068 bit0 clear; extension fails under Lapis → skip create.
- Fix: NOP tbz in DSC `.01` so daemon always uses oflag `0x202` (O_CREAT|O_RDWR). Tool `tools/patch_dsc_cfprefs_shmem.py`; wired into `reapply_dsc_bringup_patches.sh`.
- Also seed empty FeatureFlags Domain/Settings stubs (`tools/apply_featureflags_stubs.sh`) for init_featureflags ENOENTs.
- Image.pth73 (=pth72 kernel) + DSC .01 NOP + CD rehash + FF stubs.
Image.pth73.

| boot129 | Image.pth73 | CFPrefs shmem O_CREAT NOP + FF stubs | qemu11 43G lva=off | **ПРОВЕРЕНО**: `apple.cfprefs.user.daemonv1` created (keybagd oflag `0x202` → x0=0; boot128 was oflag `0x2` → ENOENT). FDR IOSOPEN holds. SB XPC cfprefsd ports ok. FeatureFlags NAMEI_FAIL 109→31. Next: backboardd/SB still respawn (EXC BRK); `system.daemonv1` not yet opened; HID/display after prefs. | out/logs/boot129 |

## Chunk 79 (boot130: backboardd/SB crash after prefs clear)

- boot129 cleared cfprefs user daemonv1 create wall via DSC `.01` shmem O_CREAT NOP.
- Next wall: backboardd respawns after `com.apple.runningboard` XPC x0=0; SpringBoard second SPAWN; EXC BRK (OS_REASON ns=2 code=5) near DSC `0x19a78515c` / elr `0x1afb6e654`.
- Plan: symbolicate BRK, ensure `system.daemonv1` create path, deepen HID/IOMFB open from backboardd.
Image.pth73 (reuse) + gdb EXC pname dump.

| boot130 | Image.pth73 | symbolicate BB/SB BRK + system.daemonv1 | qemu11 43G lva=off | **ПРОВЕРЕНО**: EXC `proc=SpringBoard` BRK (ns=2 code=5); DSC slide `0x1445c000`; unslid BRK `0x1a401e654` in BaseBoardUI (`UIViewController+BaseBoardUI.m` / BSUI failure format). Preceded by NAMEI `/var/mobile/Library/Caches/MappedImageCache/Wallpaper`. daemonv1 O_CREAT holds; FDR open holds; early BRK was `logd` not BB. | out/logs/boot130 |

## Chunk 80 (boot131: MappedImageCache/Wallpaper seed)

- Seed `private/var/mobile/Library/Caches/MappedImageCache/Wallpaper` (missing in boot130; SB BaseBoardUI assert).
- Image.pth73 + springboard seeds.
Image.pth73.

| boot131 | Image.pth73 | MappedImageCache/Wallpaper dir seed | qemu11 43G lva=off | **ПРОВЕРЕНО**: no SB Wallpaper NAMEI; SB advanced to chrono + `MappedImageCache/Session` ENOENT then same BaseBoardUI BRK (slide varies; unslid still `0x1a401e654`). FDR+daemonv1 hold. | out/logs/boot131 |

## Chunk 81 (boot132: MappedImageCache Session/Temporary/Icons)

- Seed Session (+ Temporary/Icons) under MappedImageCache to clear next BSUI assert.
Image.pth73.

| boot132 | Image.pth73 | MappedImageCache Session/Temporary/Icons | qemu11 43G lva=off | **ПРОВЕРЕНО**: no SB MappedImageCache NAMEI; **no SB BaseBoardUI EXC**; SB reaches chronoservices + runningboard XPC (ports ok). runningboardd IOGPU+IOCoreSurface open/extmethods ok. Next: SB stalls after RB lookups — deepen RB↔SB handshake / display-HID. | out/logs/boot132 |

## Chunk 82 (boot133: SpringBoard past runningboard → display/HID)

- boot132 cleared BSUI MappedImageCache asserts (Wallpaper/Session seeds).
- SB/backboardd obtain `com.apple.runningboard` ports but SB does not yet IOKMATCH IOMFB/HID.
- Plan: trace RB XPC replies / IOGPU sel2/0/4 output buffers; ensure backboard display services publish; watch SB for IOMobileFramebuffer/IOHIDEventService.
Image.pth73 (+ optional IOGPU caps tweak).

| boot133 | Image.pth73 | RB→SB handshake + display/HID probe | qemu11 43G lva=off | **ПРОВЕРЕНО**: UI IOKMATCH always-log: **0** SB/BB IOKMATCH for IOMFB/HID (not a logging gap). SB: chrono+RB ports, **no** MappedImageCache NAMEI, **no** SB EXC. BB: RB lookups then cfprefs then more RB — **never** HIDPreferences/HIDServiceSupport (last seen boot55). runningboardd IOGPU→sel2/0/4→IOCoreSurface→sel13→32 then silent; **0** `service state: running`. FDR+daemonv1 hold. Next: mach_msg2 trace RB↔SB/BB + HID prefs seeds. | out/logs/boot133 |

## Chunk 83 (boot134: mach_msg2 RB handshake + HID prefs seeds)

- boot133: SB/BB stall on RunningBoard after ports; BB never reaches HID notify.
- Plan: log `mach_msg2_trap` for SB/BB/RB; harden `cur_pname` TPIDR cache; seed `com.apple.iohid.plist` + HIDPlugins dirs.
Image.pth73 (reuse).

| boot134 | Image.pth73 | MACHMSG2 UI + ioHID seeds | qemu11 43G lva=off | **ПРОВЕРЕНО**: RB IOGPU→sel32 holds; BB→cfprefs; **0 HID**; **0 MACHMSG2** — cap counted *all* procs so early launchd burned 400 before UI (hook bug). ioHID seeds applied. Next: filter-first MACHMSG2 (boot135). | out/logs/boot134 |

## Chunk 84 (boot135: MACHMSG2 filter-first)

- boot134: MACHMSG2 hook useless (global 400-cap). Same RB→sel32 then BB stall; no HID.
- Fix: only count/log UI procs; ret-hook sync send+rcv only.
Image.pth73.

| boot135 | Image.pth73 | MACHMSG2 UI-only filter | qemu11 43G lva=off | **ПРОВЕРЕНО**: MACHMSG2 fires for SB/BB/RB (600-cap hit *before* RB sel32). RETs: mostly x0=0; **18× MACH_RCV_TIMED_OUT (0x10004003)**. Register decode was wrong (classic mach_msg ABI). Same wall: RB IOGPU→sel32 → SB RB lookups → no HID. Next: packed mach_msg2 ABI + gate after RB spawn; Image.pth74 sel32 scalar=0. | out/logs/boot135 |

## Chunk 85 (boot136: packed MACHMSG2 + IOSurface sel32 scalar0)

- Fix mach_msg2 register unpack; gate logs until runningboardd; Image.pth74 (`scalar_out[0]=0` on sel32).
Image.pth74.

| boot136 | Image.pth74 | packed MACHMSG2 + sel32 scalar0 | qemu11 43G lva=off | **ПРОВЕРЕНО**: MACHMSG2 still garbage — `mach_msg2_trap` takes **args struct\*** in x0 (not 8 fields in regs); 400-cap burned; **0 RET**. SB got RB port; no IOGPU/sel32 yet in window; **0 HID**. Next: deref args; force BB ACCHW+RB lookup → 0x9f (boot55 HID path). | out/logs/boot136 |

## Chunk 86 (boot137: BB XPC 0x9f skip + args* MACHMSG2)

- boot55: BB hit HIDServiceSupport after ACCHW x0=0x9f (no RB). Modern BB gets ports then stalls.
- Force backboardd `ACCHWComponentAuthService` + `com.apple.runningboard` lookups to return 0x9f; fix mach_msg2 args*.
Image.pth74.

| boot137 | Image.pth74 | BB FORCED_XPCERR_0x9f + args* MACHMSG2 | qemu11 43G lva=off | **ПРОВЕРЕНО**: `FORCED_XPCERR_0x9f` on ACCHW+RB; **IOKMATCH HIDServiceSupport** from backboardd (boot55 path restored). MACHMSG2 args* sane (opt=0x3, ports, 36× RET=0). IOITER_NEXT **empty** (0xe00002c0) — CES misses `{IOProviderClass=IOService, IOPropertyMatch=HIDServiceSupport}` because resolve only saw "IOService". Then BRK/SIGKILL. Next: resolve HID property-match → IOHIDEventService stub (Image.pth75). | out/logs/boot137 |

## Chunk 87 (boot138: HIDServiceSupport CES fulfill)

- Image.pth75: `lapis_resolve_match_stub` maps IOPropertyMatch.HIDServiceSupport → IOHIDEventService; richer HID props; logging/HIDPlugin seeds.

| boot138 | Image.pth75 | HID property-match CES + seeds | qemu11 43G lva=off | **ПРОВЕРЕНО**: CES+stub iter OK; IOITER obj=`0x939aa340`. Same unslid BRK as boot137 (`__pthread_mutex_firstfit_wake+96`, FAR crash_info; malloc freelist FP) — **not** empty-vs-filled HID. Forced 0x9f left out-port (`x7*=0x1903`). Next: clear out-port on force; ServicePlugins seeds. | out/logs/boot138 |

## Chunk 88 (boot139: clear forced XPC out-port)

| boot139 | Image.pth75 | FORCED_XPCERR + CLEARED out-port | qemu11 43G lva=off | **ПРОВЕРЕНО**: mutex/malloc BRK **gone**; ACCHW ports CLEARED; HID CES+IOITER OK; BB reached mobilegestalt/frontboard/locationd. Then **SIGABRT** ns=2 code=6: `CM_SYSPATH id=None` → NULL → objc `fileURLWithPath:nil` (boot47 wall). Next: force bundle-id into syspath stub. | out/logs/boot139 |

## Chunk 89 (boot140: CM_SYSPATH force id for BB)

| boot140 | Image.pth75 | CM_SYSPATH_FORCE_ID + HID CES | qemu11 43G lva=off | **ПРОВЕРЕНО**: `CM_SYSPATH_FORCE_ID` → path `/private/var/containers/Data/System/com.apple.backboardd`. **No BB SIGABRT**. HID CES OK. AppleParavirtGPU match → **IOMobileFramebuffer match → IOSOPEN type=0 x0=0 (nuc ok)** from backboardd. BB stayed up (analyticsd after open). **0** IOEXT/IOCMEM from BB post-open. SB alive (chronoservices+RB). Next: IOCONN/IOREGPROP/IOMAPMEM trace. | out/logs/boot140 |

## Chunk 90 (boot141: post-IOMFB-open IOKit trace)

| boot141 | Image.pth75 | IOCONN+IOREGPROP+IOMAPMEM hooks | qemu11 43G lva=off | **ПРОВЕРЕНО**: regression — **no CLEARED** on forced XPC (global `_xl_out` race); same unslid mutex BRK `0x1e261dd44` after HID CES; never reached IOMFB/CM_SYSPATH. IOCONN hooks unused. Next: embed out4/out6/out7 in RetBP tag. | out/logs/boot141 |

## Chunk 91 (boot142: race-free XPC out-port clear)

| boot142 | Image.pth75 | tag-embedded out-port CLEARED | qemu11 43G lva=off | **ПРОВЕРЕНО**: RB CLEARED ok; CM_SYSPATH path; **IOMFB IOSOPEN x0=0** restored; BB alive (no abort). Post-open: **0** IOCONN/IOREGPROP/IOEXT/IOCMEM from BB — silence after nuc. Next: hook all connect-method MIG variants + property bin_buf. | out/logs/boot142 |

## Chunk 92 (boot143: full IOConnect MIG + property variants)

| boot143 | Image.pth75 | all IOCONN_* + IOREGPROP buf/all | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOMFB open holds; **0 IOCONN** post-open (BB does not call connect methods). IOREGPROP_buf fires but key logged empty (was reading plane in x1; property is x2). DisplayPortCount path reaches open. SB pname mostly `?` (SBSTATIC pass) — missed SB XPC. Next: fix pname cache + property key ABI; CA/SB XPC path. | out/logs/boot143 |

## Chunk 93 (boot144: SB pname + property key + CA seeds)

| boot144 | Image.pth75 | SB TPIDR cache + IOREG key=x2 | qemu11 43G lva=off | **ПРОВЕРЕНО**: SB pname fixed (SBSTATIC+cfprefs+chrono+RB port). IOREG keys: DisplayPortCount OK → IOMFB open. **0 IOCONN** post-open. BB later WILL_SPAWN (RB still forced 0x9f — likely exits without RB). Next: force ACCHW only; let BB keep real RB. | out/logs/boot144 |

## Chunk 94 (boot145: ACCHW-only force, real RB for BB)

| boot145 | Image.pth75 | ACCHW 0x9f only; RB real | qemu11 43G lva=off | **ПРОВЕРЕНО**: ACCHW FORCED+CLEARED; RB real x0=0. BB hung after RB check-in MACHMSG — **never** reached HIDServiceSupport/IOMFB. RB 0x9f force is required for HID path. Post-open silence is a separate wall. Delayed launchd CON lines were not a BB relaunch. | out/logs/boot145 |

## Chunk 95 (boot146: restore RB force + IOCFPlugInTypes + NUC geometry)

| boot146 | Image.pth76 | RB+ACCHW 0x9f; empty IOCFPlugInTypes; NUC panel props; 8/16-byte IOMFB outs; IOSCLOSE log | qemu11 43G lva=off | **ПРОВЕРЕНО**: RB+ACCHW FORCED; HID→IOCFPlugInTypes **x0=0** (was 0xe00002f0); DisplayPortCount→IOMFB open+nuc. **0** IOCONN/IOSCLOSE/IOEXT from BB. BB alive post-open (mach msgid `w00t`). NAMEI `/usr/appleinternal/lib/HIDPlugins` miss. FDR open→exit1 (known cal). SB chrono+RB, no backboard XPC yet. | out/logs/boot146 |

## Chunk 96 (boot147: HIDPlugins path + UC class + async/note MIG)

| boot147 | Image.pth77 | usr/appleinternal HIDPlugins; UC IOClass+LapisUC; NOTE/ASYNC/GETSVC hooks | qemu11 43G lva=off | **ПРОВЕРЕНО**: `/usr/appleinternal/lib/HIDPlugins` NAMEI gone. IOMFB open+nuc, conn obj non-null. **0** IOCONN/NOTE/ASYNC/conformsTo. RE: `IOMobileFramebufferOpen` post-path uses canvas-width/height + sel8 GetDisplaySize — BB raw-opens only (no Open post-path). | out/logs/boot147 |

## Chunk 97 (boot148: canvas-* props + sel8 + get_class/map_shared hooks)

| boot148 | Image.pth78 | canvas-width/height + MaxSrc*; sel8 size; IOGETCLASS/MAPSH hooks | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOMFB open+nuc holds; **0** canvas IOREGPROP / IOCONN / IOGETCLASS — BB still raw-opens and stops. Empty IOCFPlugInTypes likely blocks HID CFPlugin → display path never uses the connection. | out/logs/boot148 |

## Chunk 98 (boot149: real HID IOCFPlugInTypes → IOHIDLib)

| boot149 | Image.pth79 | HID UUID→IOHIDLib.plugin; seed IOHIDEventServicePlugin | qemu11 43G lva=off | **ПРОВЕРЕНО**: EventServicePlugin loads; HID IOSOPEN→IOCONN sel0/1→**NOTE64 0xe00002c7**→**IOSCLOSE**. Then IOMFB open (still 0 post-open methods). Next: stub registerNotificationPort. | out/logs/boot149 |

## Chunk 99 (boot150: registerNotificationPort success)

| boot150 | Image.pth80 | lapis_register_notification_port → jj/jy B-patch | qemu11 43G lva=off | **ПРОВЕРЕНО**: NOTE64 **x0=0** (`regnote ok`). Then still **IOSCLOSE**. Root: after sel0, `QueueSize` **0xe00002f0**; `IOMAPMEM type0` → cmap ok but `vm_map_remap_extract` **"rejecting attempt to extract from kernel_object"** → **0xe00002c2**; later EXC/BRK `elr=0x1fe08dd44` FAR=`0x2079f4010`, BB SIGKILL. | out/logs/boot150 |

## Chunk 100 (boot151: HID QueueSize + IOSharedDataQueue map)

| boot151 | Image.pth81 | QueueSize=0x4000; clientMemory→IOSharedDataQueue; regnote→setNotificationPort | qemu11 43G lva=off | **ПРОВЕРЕНО**: early **PWTKS** `Unexpected fault in kernel static region` pc=`lapis_hid_queue+0x20` FAR=`g_hid_shared_queue` — ramfs blob is `.text`-only (RX); static BSS write faulted during publish. Never reached HID. | out/logs/boot151 |

## Chunk 101 (boot152: stash HID queue on UC property, no BSS)

| boot152 | Image.pth82 | LapisHIDQ via copyProperty/setProperty; QueueSize; SharedDataQueue map | qemu11 43G lva=off | **ПРОВЕРЕНО**: no PWTKS. QueueSize **x0=0**; **IOMAPMEM cmap hidq x0=0**; NOTE64 **x0=0**; **no IOSCLOSE / no BB BRK**. HID props (DeviceUsagePairs/Transport/…) still **0xe00002f0**. Then ParavirtGPU→DisplayPortCount→**IOMFB IOSOPEN+nuc** with **0** post-open IOCONN. BB→frontboard XPC. SB at cfprefs only so far. | out/logs/boot152 |

## Chunk 102 (boot153: HID digitizer identity + UC class split)

| boot153 | Image.pth83 | DeviceUsagePairs/DisplayIntegrated/…; HID vs IOMFB IOClass on NUC | qemu11 43G lva=off | **ПРОВЕРЕНО**: HID identity props **x0=0**; map/NOTE64 hold; **no IOSCLOSE**. IOMFB **IOSOPEN+nuc** still **0** post-open IOCONN. BB→frontboard; SB→cfprefs→chrono→RB. BB **CPU burn** >50%/180s after open (digitizer sentinel / display bind). device_type often miss. | out/logs/boot153 |

## Chunk 103 (boot154: digitizer surface dims + device_type)

| boot154 | Image.pth84 | DigitizerSurfaceWidth/Height; device_type/compatible on IOMFB+ParavirtGPU | qemu11 43G lva=off | **ПРОВЕРЕНО**: DigitizerSurface **never queried**. device_type still miss on walk. HID+IOMFB open hold; **0** post-open IOCONN. BB **CPU burn** + cfprefsd burn remain. SB→chrono+RB; BB→frontboard. | out/logs/boot154 |

## Chunk 104 (boot155: HID queue pulse enqueue after NOTE64)

| boot155 | Image.pth85 | IOSharedDataQueue::enqueue 8B pulse after setNotificationPort | qemu11 43G lva=off | **ПРОВЕРЕНО**: log said `enqfail` but Boolean **true=success** (polarity bug). Still BB **CPU burn**; IOMFB open **0** IOCONN; BB→frontboard wait; SB→chrono+RB. Pulse alone insufficient. | out/logs/boot155 |

## Chunk 105 (boot156: force BB frontboard XPC 0x9f)

| boot156 | Image.pth86 | enqueue log polarity fix; BB frontboard.systemappservices→0x9f; MACHMSG2 BB uncapped | qemu11 43G lva=off | **ПРОВЕРЕНО**: FORCED frontboard **0x9f**; `regnote ok enq`. IOMFB open still **0** IOCONN. Post-open = **w00t/0x40000000** mach spin (2200+); uncapped MACHMSG2 GDB traps likely worsen burn. No canvas. | out/logs/boot156 |

## Chunk 106 (boot157: drop w00t flood + full IOREGPROP hooks)

| boot157 | Image.pth87 | no HID pulse; filter w00t MACHMSG2; hook get_property/bytes/rec | qemu11 43G lva=off | **ПРОВЕРЕНО**: w00t flood gone. **REGRESSION**: BB **BRK** `elr=0x1f0a1dd44` immediately after NOTE64 (no pulse) → respawn; never reached IOMFB. Pulse required. No canvas plain/bytes. | out/logs/boot157 |

## Chunk 107 (boot158: restore HID pulse + keep w00t filter)

| boot158 | Image.pth88 | restore enqueue pulse; keep w00t filter + IOREGPROP hooks | qemu11 43G lva=off | **ПРОВЕРЕНО**: `regnote ok enq` but BB still **BRK** `elr=…dd44` after NOTE64 (kernel==pth86). New get_property* GDB hooks implicated. No IOMFB. | out/logs/boot158 |

## Chunk 108 (boot159: drop get_property* GDB hooks)

| boot159 | Image.pth89 | same kernel as pth88; remove plain/bytes/rec IOREGPROP BPs | qemu11 43G lva=off | **ПРОВЕРЕНО**: BRK gone (get_property* BPs were the cause). `regnote ok enq`; frontboard **FORCED 0x9f**; IOMFB **IOSOPEN+nuc**; still **0** post-open IOCONN/canvas. SB→chrono+RB. | out/logs/boot159 |

## Chunk 109 (boot160: digitizer-shaped HID pulse + MACHMSG2 post-IOMFB window)

| boot160 | Image.pth90 | 64B digitizer IOHIDEvent pulse; VendorID/ProductID; MACHMSG2 reopen | qemu11 43G lva=off | **ПРОВЕРЕНО**: `regnote ok enq`×3; NOTE64/map/QueueSize/VendorID **x0=0**; frontboard **FORCED 0x9f**; Paravirt→DisplayPortCount→**IOMFB IOSOPEN+nuc**; `MACHMSG2_WINDOW` fired. **0** post-open IOMFB IOCONN/canvas/sel8 (HID IOCONN only). BB→msgid `0x10000000` spin + CPU burn; SB→chrono+RB. DigitizerSurface never queried; `HIDServiceAccessEntitlement`/`Unregistered` **0xe00002f0**; device_type miss on DT walk (non-blocking). | out/logs/boot160 |

## Chunk 110 (boot161: HID entitlement + IOMFB messageClients + spin probe)

| boot161 | Image.pth91 | HIDServiceAccessEntitlement; Unregistered=0; richer digitizer; IOMFB messageClients; BB spin LR | qemu11 43G lva=off | **ПРОВЕРЕНО**: HID entitlement/`Unregistered` now **x0=0** (was 0xe00002f0). Then **REGRESSION**: after frontboard **FORCED+8-byte CLEARED**, BB **EXC_BAD_ACCESS** FAR=`0x1536050` elr=`0x1885525e4` → exit — **never** reached Paravirt/IOMFB (0 DisplayPortCount / 0 MACHMSG2_WINDOW). Richer digitizer alone not proven; CLEARED width smash is the smoking gun. | out/logs/boot161 |

## Chunk 111 (boot162: restore 4-byte CLEARED + boot160 pulse; keep entitlement/messageClients)

| boot162 | Image.pth92 | 4-byte CLEARED; boot160 digitizer pulse; keep HID entitlement + IOMFB messageClients | qemu11 43G lva=off | **ПРОВЕРЕНО**: BB **alive** past frontboard 4-byte CLEARED (no EXC). HID entitlement/`Unregistered` **x0=0**; Paravirt→DisplayPortCount→**IOMFB IOSOPEN+nuc**; `MACHMSG2_WINDOW` ok. Still **0** post-open IOMFB IOCONN/canvas/sel8. Post-open: one mach msgid **11602**→`0x1903` then `MACHMSG2_SPIN` lr=`mach_syscall+0x190`. SB→chrono+RB. | out/logs/boot162 |

## Chunk 112 (boot163: post-IOMFB real RB + AppleBacklightDisplay)

| boot163 | Image.pth93 | stop RB/frontboard force after IOMFB open; AppleBacklightDisplay stub | qemu11 43G lva=off | **ПРОВЕРЕНО**: `msgclients property-change` on IOMFB NUC; backlight/wrangler metaclasses published (**0** BB IOKMATCH). IOMFB open+WINDOW hold. Post-open BB mach richer (msgid **1026**/1018, analyticsd) but still **0** IOMFB IOCONN/canvas/sel8. No post-IOMFB RB re-lookup (force gate unused). SB→chrono+RB + msgid 1026 flood. | out/logs/boot163 |

## Chunk 113 (boot164: SPIN userspace frames + IOMFB fb map + GetMainDisplay hook)

| boot164 | Image.pth94 | MACHMSG2_SPIN uframes; IOMFB cmap RGBA fb; DSC GetMainDisplay log | qemu11 43G lva=off | **ПРОВЕРЕНО**: `SPIN_U` pc unslid≈`0x232236cd4` (libsystem near abort_with_reason) via xpc/`0x197f2*` — **pre-IOMFB** (CM_SYSPATH). `GMD_ENTER` **only FDR** (x0=0), never BB. `cmap iomfb fb` **0** (hidq only). IOMFB open+msgclients+WINDOW; still **0** post-open IOCONN/canvas. Post-open BB: analyticsd + msgid 1026/1018 then quiet. CLEARED all MemoryError. | out/logs/boot164 |

## Chunk 114 (boot165: post-IOMFB uframes + CLEARED via set + cave size fields)

| boot165 | Image.pth95 | post-IOMFB BB uframes; CLEARED set{int}; GMD cave store 2420×1668 | qemu11 43G lva=off | **ПРОВЕРЕНО**: CLEARED restored (ACCHW/RB/frontboard). GMD only FDR x0=0; FDR exit=1. IOMFB open+msgclients; **0** post-open IOCONN. `POST_IOMFB_U` unused (BB only sends filtered msgid `0x10000000`). Post-open `SPIN_U` n=2/3: same stack libsystem_kernel→dispatch→**xpc**→**pthread** (`0x1e261d3b0`) — XPC/cond wait, not IOKit. | out/logs/boot165 |

## Chunk 115 (boot166: HID re-pulse + post-IOMFB BB XPC log)

| boot166 | Image.pth96 | delayed HID enqueue pulses; log all BB XPC after IOMFB; SPIN tag post_iomfb | qemu11 43G lva=off | **ПРОВЕРЕНО**: `regnote`×3 + `repulse enq`×12 ok. CLEARED ok. IOMFB open+msgclients+WINDOW. `POST_IOMFB_U` msgid **1026/1018** (analyticsd). Post-open BB XPC: only analyticsd RET — **no new named wait**. Still **0** IOMFB IOCONN/canvas/sel8 (HID IOCONN only). `POST_IOMFB_SPIN` msgid `0x10000000`→remote `0x7c03` (xpc/pthread). GMD FDR-only. HID re-pulse alone does not unlock display path. | out/logs/boot166 |

## Chunk 116 (boot167: richer digitizer + deferred IOMFB wake + SPIN hdr)

| boot167 | Image.pth97 | boot161-style digitizer fields; deferred IOMFB messageClients; SPIN msg hdr dump | qemu11 43G lva=off | **ПРОВЕРЕНО**: richer `regnote`/`repulse` + `deferred wake msgclients` ok; CLEARED ok; IOMFB open+WINDOW. Still **0** IOMFB IOCONN/canvas/sel8. `SPIN_HDR` send=2 data=0 opt=`0x31`/`0x40231` (**MACH_SEND_NOTIFY**+timeout) → remote `0x7c03` — service/notify wait, not IOKit. Post-open mach: XPC `0x40000388`/`1026`. Missing props: `IOClassNameOverride`/`Hidden`/`ProtectedAccess`/`SensorPropertySupported` **0xe00002f0**. | out/logs/boot167 |

## Chunk 117 (boot168: real frontboard + seed missing HID/IOMFB props)

| boot168 | Image.pth98 | drop frontboard 0x9f force; seed IOClassNameOverride/Hidden/… | qemu11 43G lva=off | **ПРОВЕРЕНО**: seeded props **x0=0** (was 0xe00002f0). Frontboard **real x0=0 but x7*=None** (null endpoint — worse than 0x9f). Still reached HID→IOMFB open+WINDOW. Still **0** IOMFB IOCONN/canvas/sel8. Pre-IOMFB SPIN_HDR now readable (`msgh_bits=0x080809b8` size=1 opt=`0x231`). No post-IOMFB SPIN samples (spin cap burned pre-open). | out/logs/boot168 |

## Chunk 118 (boot169: restore frontboard force + richer FB wake msgs + SPIN reset)

| boot169 | Image.pth99 | frontboard 0x9f again; wake msgs power/resume/gfx; reset spin1 at post_iomfb | qemu11 43G lva=off | **ПРОВЕРЕНО**: frontboard FORCED; CLEARED ok; SPIN reset → `POST_IOMFB_SPIN`×4 remote `0x4803` HDR opt=`0x40231`. IOMFB open+WINDOW; still **0** IOMFB IOCONN/canvas/sel8. `locationd.registration` **x0=0 x7*=None** (null port) just before open — likely feeds MACH_SEND_NOTIFY wait. Wake msgs ran (IOLog `%x` garbled; values are in-blob). | out/logs/boot169 |

## Chunk 119 (boot170: force locationd null-port deny)

| boot170 | Image.pth100 | force BB locationd.registration 0x9f; fixed wake log strings | qemu11 43G lva=off | **ПРОВЕРЕНО**: locationd **FORCED** (+ACCHW/RB/frontboard CLEARED). Wake fixed strings ok. IOMFB open+WINDOW; still **0** IOMFB IOCONN/canvas/sel8. `POST_IOMFB_SPIN`→`0x4903` persists — locationd deny insufficient. analyticsd still real port pre-open. | out/logs/boot170 |

## Chunk 120 (boot171: deny analyticsd/gestalt + IOSurface wake)

| boot171 | Image.pth101 | force analyticsd+mobilegestalt 0x9f; surface wake+props on IOMFB NUC | qemu11 43G lva=off | **ПРОВЕРЕНО**: gestalt/locationd/frontboard FORCED; **surface wake** ok; IOMFB open+WINDOW; still **0** IOMFB IOCONN. analyticsd looked up **after** post_iomfb → force gate skipped → **x0=0/null port**; `POST_IOMFB_SPIN`→`0x5903` remains. | out/logs/boot171 |

## Chunk 121 (boot172: always-force analyticsd + post-IOMFB null-port deny)

| boot172 | Image.pth102 | analyticsd always 0x9f for BB; FORCED_NULLPORT for post-IOMFB null XPC | qemu11 43G lva=off | **ПРОВЕРЕНО**: analyticsd **FORCED** post-open; **POST_IOMFB_SPIN=0** (unblocked). Still **0** IOMFB IOCONN/canvas/sel8. BB CPU burn → launchd respawn; SB msgid 11602 flood. Null-port deny unused. | out/logs/boot172 |

## Chunk 122 (boot173: DSC IOSOPEN→sel8 wrap + Open-wrap sel8)

| boot173 | Image.pth103 | wrap IOServiceOpen stub → sel8 GetDisplaySize; Open-wrap w1=#8; W00T sample | qemu11 43G lva=off | **ПРОВЕРЕНО**: IOMFB open x0=0 then **BB EXC** FAR=0 (wrap ABI); **0** IOEXTMETHOD sel8. W00T samples post-crash/respawn. Wrap **reverted**. | out/logs/boot173 |

## Chunk 123 (boot174: revert wrap; fake post-IOMFB W00T success)

| boot174 | Image.pth104 | restore IOSOPEN stub; GDB W00T_FAKE x0=0/pc=lr after IOMFB open | qemu11 43G lva=off | **ПРОВЕРЕНО**: wrap reverted (no BB EXC). **Never reached IOMFB** — BB `cfprefsd.daemon` **x0=0/null** then msgid 11602 flood; W00T_FAKE unused. | out/logs/boot174 |

## Chunk 124 (boot175: pre-IOMFB BB null-port deny)

| boot175 | Image.pth105 | FORCED_NULLPORT for BB anytime (incl. cfprefs); keep W00T_FAKE | qemu11 43G lva=off | **ПРОВЕРЕНО**: cfprefs NULLPORT → early BB **BRK EXC**/respawn; WINDOW fired on **HID** open (bb≥2 bug); IOMFB open later; locationd real port→W00T; still **0** sel8/canvas. | out/logs/boot175 |

## Chunk 125 (boot176: IOMFB-match WINDOW; skip cfprefs NULLPORT; always locationd)

| boot176 | Image.pth106 | post_iomfb only after BB IOMobileFramebuffer match; no cfprefs NULLPORT; always force locationd | qemu11 43G lva=off | **ПРОВЕРЕНО**: MATCH→WINDOW ok; no BB EXC; locationd FORCED; IOMFB open; user cfprefs port ok. Post-open msgid **0x40000388**→0x80b **x0=0x10004004** then quiet — **0** IOMFB IOCONN/sel8. | out/logs/boot176 |

## Chunk 126 (boot177: fake post-IOMFB XPC 0x40000388 + system cfprefs NULLPORT)

| boot177 | Image.pth107 | W00T_FAKE includes 0x40000388/384; NULLPORT cfprefs.system only | qemu11 43G lva=off | **ПРОВЕРЕНО**: never reached IOMFB — BB XPC as **proc=?** so ACCHW/RB **not forced**; real RB port → hang; later speculative respawn. | out/logs/boot177 |

## Chunk 127 (boot178: force XPC names for proc=?)

| boot178 | Image.pth108 | treat proc=? as BB for force-name + NULLPORT lists | qemu11 43G lva=off | **ПРОВЕРЕНО**: forces ok (proc=backboardd); cfprefs.system NULLPORT → **BRK**/respawn; MATCH→WINDOW→IOMFB open then **quiet** (0 W00T/sel8). | out/logs/boot178 |

## Chunk 128 (boot179: skip all cfprefs NULLPORT; keep 0x40000388 fake)

| boot179 | Image.pth109 | no cfprefs NULLPORT (boot176); proc=? forces; W00T_FAKE 0x40000388 | qemu11 43G lva=off | **ПРОВЕРЕНО**: clean MATCH→WINDOW; XPC388+w00t FAKE fired; then analytics + **BB respawn**; still **0** sel8/canvas. cfprefs both null (left alone). | out/logs/boot179 |

## Chunk 129 (boot180: fake only 0x40000388; real analyticsd post-open)

| boot180 | Image.pth110 | XPC388_FAKE only; stop w00t fake; analyticsd real after IOMFB; NULLPORT BB-only | qemu11 43G lva=off | **ПРОВЕРЕНО**: MATCH→WINDOW→IOMFB open then **quiet** (0 XPC388); analyticsd forced pre-match; speculative BB spawn; still **0** sel8. | out/logs/boot180 |

## Chunk 130 (boot181: fixed IOSOPEN→sel8 wrap ABI)

| boot181 | Image.pth111 | DSC wrap inputCnt=0+outCnt=2; Open-wrap sel8; always analyticsd; XPC388_FAKE | qemu11 43G lva=off | **ПРОВЕРЕНО**: **sel=8 x0=0** on GMD/IOMFB open (proc=?); no BB_IOMFB_MATCH; BB HID NOTE64 then **BRK** (pacibsp/retab?). Never BB IOMFB WINDOW. | out/logs/boot181 |

## Chunk 131 (boot182: plain-ret wrap + MATCH for proc=?)

| boot182 | Image.pth112 | wrap without pacibsp/retab; BB_IOMFB_MATCH for proc=?; keep sel8 ABI | qemu11 43G lva=off | **ПРОВЕРЕНО**: FDR+**BB sel=8 x0=0** (sout=2!); MATCH→WINDOW→open; then BB **FAR=0** (scalar[0]=1 not WxH). | out/logs/boot182 |

## Chunk 132 (boot183: sel8 scalar outs = 2420×1668)

| boot183 | Image.pth113 | lapis_external_method sel8 → scalar_out[0]=2420,[1]=1668 | qemu11 43G lva=off | **ПРОВЕРЕНО**: BB/FDR still **FAR=0** after sel8; crash is `str x21,[x19]` in GMD (out* null) — WxH hardcoded in userspace; nested wrap sel8 likely clobbers x19. | out/logs/boot183 |

## Chunk 133 (boot184: identity IOSOPEN wrap; restore Open sel5)

| boot184 | Image.pth114 | wrap = open-only (no nested sel8); Open-wrap w1=#5 UCAL; keep scalar WxH | qemu11 43G lva=off | **ПРОВЕРЕНО**: **no FAR=0**; FDR **GMD_RET x0=0**; BB MATCH→IOMFB open; XPC388_FAKE; then analytics force → quiet; **0** sel8 (GMD seeds WxH). | out/logs/boot184 |

## Chunk 134 (boot185: revert IOSOPEN stub; real analyticsd post-open)

| boot185 | Image.pth115 | restore stub ADRP/ADD/BR; analyticsd real after IOMFB; SURFACE_MATCH log | qemu11 43G lva=off | **ПРОВЕРЕНО**: no FAR=0; GMD via xpcproxy x0=0; BB open+XPC388; analyticsd **x0=1 null ports** → quiet; **0** SURFACE/sel8. | out/logs/boot185 |

## Chunk 135 (boot186: force analyticsd; IOSurface deferred wake; GMD_BODY log)

| boot186 | Image.pth116 | always-force analyticsd; deferred wake nudges IOSurfaceRoot; GMD_BODY hook | qemu11 43G lva=off | **ПРОВЕРЕНО**: **BB GMD_BODY_RET x0=0**; FDR GMD ok; no FAR=0; still **0** SURFACE/sel8; SB got **null RB** port; BB later msgid 3228. | out/logs/boot186 |

## Chunk 136 (boot187: force SB runningboard; dump GMD *out)

| boot187 | Image.pth117 | force RB/frontboard/analytics for SpringBoard; GMD_BODY_RET *out+size log | qemu11 43G lva=off | **ПРОВЕРЕНО**: SB RB **FORCED**; BB **BRK after NOTE64** (rich pulse) — never GMD; FDR GMD dump failed (userspace *out via m64). | out/logs/boot187 |

## Chunk 137 (boot188: soft HID NOTE64 pulse; fix GMD *out uread)

| boot188 | Image.pth118 | NOTE64→8B zeros; rich pulse after 2s; uread GMD *out; keep SB RB force | qemu11 43G lva=off | **ПРОВЕРЕНО**: soft pulse → **no BB BRK**; **BB GMD_BODY_RET *out size=2420×1668**; MATCH→WINDOW→IOSOPEN; SB RB **FORCED**; SB got **system-app-server** port. Still **0 SURFACE/sel8/sel13**; post-GMD HID flood + XPC `0x10004004`. | out/logs/boot188 |

## Chunk 138 (boot189: deepen IOSurface NUC/sel13 after GMD)

| boot189 | Image.pth119 | IOSurfaceRootUserClient NUC; republish+link on IOMFB open; SURFACE_MATCH any; GMD obj40 dump | qemu11 43G lva=off | **ПРОВЕРЕНО**: MATCH→WINDOW→IOSOPEN+surface wake; **GMD *out=0** (regression vs boot188 WxH) — in-NUC publishResource/setProperty suspected; SB system-app-server **null**; **0 SURFACE/sel13**. | out/logs/boot189 |

## Chunk 139 (boot190: revert in-NUC surface publish; keep UC tag + deferred republish)

| boot190 | Image.pth120 | NUC surface wake = message only (boot188); keep IOSurfaceRootUserClient + deferred republish; GMD_OUT_NULL tag | qemu11 43G lva=off | **ПРОВЕРЕНО**: GMD **restored** `*out size=2420×1668 conn=0x7e0b`; SB system-app-server port ok; NOTE64 ok. **obj40 isa=0** (cave zeroes typed-malloc header); still **0 SURFACE/sel13**. | out/logs/boot190 |

## Chunk 140 (boot191: GMD cave preserve isa/refcount)

| boot191 | Image.pth121 | DSC cave zero skips +0x00..+0x0f; GMD isa/f838/fc08 dump | qemu11 43G lva=off | **ПРОВЕРЕНО**: cave rewrite → FDR `cs_invalid_page` on GMD; first BB mostly `proc=?` never NOTE64/GMD; BB respawn too late for T=900. **No BB GMD/isa sample.** | out/logs/boot191 |

## Chunk 141 (boot192: retest isa-preserve cave, longer timeout)

| boot192 | Image.pth122 | same DSC isa-preserve; T=1100; catch BB GMD isa + SURFACE | qemu11 43G lva=off | **ГИПОТЕЗА**: settled cave page + more time → BB GMD with isa≠0 → SURFACE. | out/logs/boot192 |
