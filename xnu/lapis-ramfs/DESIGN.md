# lapis-ramfs: writable in-kernel RAM filesystem for /private/var (implemented: lapis_ramfs.c, chunk 5)

Why: this kernel's built-in vfs table has only devfs and nullfs (vfstbllist @0xfffffe0007c85fd0);
lapisfs (lapis-mockfs) is read-only; tmpfs/APFS/HFS are kexts and absent. iOS writes /private/var
(launchd logs, /var/run, /var/mobile, logd, keybagd, containermanagerd), so a writable fs is needed.

Shape (same build/injection model as lapis-mockfs: freestanding aarch64-linux-gnu-gcc blob,
--defsym kernel symbols, placed in a dead kernel region, registered with vfs_fsadd at mountroot time):
- vfs name "tmpfs" so the stock mount(8) path works: fstab `ramdisk /private/var tmpfs rw,size=…,template=…`
  -> /sbin/mount_tmpfs (IPSW binary, now in the rootfs) -> mount(2) "tmpfs" with its tmpfs_mount_args.
  Alternative: name "lapisramfs" + mount from a phase-2 helper.
- nodes: {type, mode, uid, gid, times, nlink, parent, name, children list (dir), data pages (reg) via
  kalloc_data, symlink target}; per-mount size limit from mount args.
- vfsops: mount (parse tmpfs args: max size, root mode/uid/gid), unmount, root, getattr (statfs), sync.
- vnops: lookup (incl. "..", cache_enter), create, mkdir, symlink, readlink, remove, rmdir, rename,
  link, open/close, access, getattr, setattr (mode/uid/gid/times/size truncate), read/write via
  uiomove, readdir (struct dirent + VNODE_READDIR_EXTENDED off), fsync (no-op), inactive, reclaim,
  pathconf, mmap + pagein/pageout through cluster_* with a memory "device" or ubc_upl copy, so that
  mmap of files under /var (sqlite, dyld caches of data containers) works.
- template= (mount_tmpfs copies a template tree into the new mount) is done by mount_tmpfs in userland.
Test plan: boot with fstab ramdisk entry for /private/var; expect NAMEI_FAIL /private/var/mobile/tmp
and /private/var/log/com.apple.xpc.launchd/launchd.log to disappear (create succeeds).
