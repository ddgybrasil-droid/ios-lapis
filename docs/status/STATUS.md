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

| boot68 | Image.pth23 | mnt2 + systemgroup seeds + AKS stub | qemu11 43G lva=off | running… | out/logs/boot68 |
