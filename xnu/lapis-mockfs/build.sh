#!/bin/sh
set -e; cd "$(dirname "$0")"
CC=aarch64-linux-gnu-gcc
$CC -Os -ffreestanding -nostdlib -fno-pic -fno-pie -mgeneral-regs-only -mstrict-align \
    -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common -Wall -Wno-unused-parameter \
    -c lapis_mockfs.c -o lapis_mockfs.o
# symbol addresses (kernel Image VAs)
DS="
--defsym=IOMallocZero=0xfffffe0007aa5ec4
--defsym=IOFree=0xfffffe0007aa5eb0
--defsym=kalloc_data=0xfffffe000739c964
--defsym=kfree_data=0xfffffe000739d1a0
--defsym=vnode_create=0xfffffe00075f10f8
--defsym=vnode_get=0xfffffe00075f0e28
--defsym=vnode_put=0xfffffe00075ee558
--defsym=vnode_fsnode=0xfffffe000761de00
--defsym=vnode_clearfsnode=0xfffffe000761de08
--defsym=vnode_vtype=0xfffffe000761ddf8
--defsym=vnode_mount=0xfffffe000761dd58
--defsym=vfs_devvp=0xfffffe000761ced0
--defsym=vfs_fsprivate=0xfffffe000761cdcc
--defsym=vfs_setfsprivate=0xfffffe000761cdd4
--defsym=vfs_devblocksize=0xfffffe000761ce0c
--defsym=vfs_fsadd=0xfffffe000761cfc4
--defsym=mdevgetrange=0xfffffe00075cb8e0
--defsym=cluster_read=0xfffffe00075e2f18
--defsym=cluster_pagein=0xfffffe00075e07e0
--defsym=ubc_upl_map=0xfffffe0007938700
--defsym=ubc_upl_unmap=0xfffffe000793873c
--defsym=ubc_upl_commit_range=0xfffffe00079387ac
--defsym=ubc_upl_abort_range=0xfffffe0007937db8
--defsym=ubc_msync=0xfffffe00079381a8
--defsym=vnode_setnocache=0xfffffe000761e0b0
--defsym=buf_strategy=0xfffffe00075d6980
--defsym=buf_vnode=0xfffffe00075d5e58
--defsym=buf_blkno=0xfffffe00075d6514
--defsym=buf_count=0xfffffe00075d5df8
--defsym=buf_flags=0xfffffe00075d5dbc
--defsym=buf_map=0xfffffe00075d673c
--defsym=buf_unmap=0xfffffe00075d685c
--defsym=buf_setresid=0xfffffe00075d5e20
--defsym=buf_biodone=0xfffffe00075d6bec
--defsym=buf_seterror=0xfffffe00075d5d70
--defsym=memcpy=0xfffffe0007c48550
--defsym=memset=0xfffffe0007c487a0
--defsym=strncmp=0xfffffe0007574d90
--defsym=IOLog=0xfffffe0007aa6fdc
--defsym=vn_default_error=0xfffffe00075e8a10
--defsym=_vfs_mountroot=0xfffffe00075eeb14
--defsym=err_create=0xfffffe0007613074
--defsym=err_open=0xfffffe00076130a4
--defsym=nop_open=0xfffffe000761309c
--defsym=nop_close=0xfffffe00076130ac
--defsym=err_mknod=0xfffffe0007613094
--defsym=err_close=0xfffffe00076130b4
--defsym=err_access=0xfffffe00076130c4
--defsym=err_setattr=0xfffffe00076130e4
--defsym=err_write=0xfffffe0007613104
--defsym=err_ioctl=0xfffffe0007613114
--defsym=err_select=0xfffffe0007613124
--defsym=err_mmap=0xfffffe0007613180
--defsym=nop_fsync=0xfffffe0007613188
--defsym=err_remove=0xfffffe00076131a0
--defsym=err_link=0xfffffe00076131b0
--defsym=err_rename=0xfffffe00076131c0
--defsym=err_mkdir=0xfffffe00076131d0
--defsym=err_rmdir=0xfffffe00076131e0
--defsym=err_symlink=0xfffffe00076131f0
--defsym=err_readdir=0xfffffe0007613200
--defsym=err_readlink=0xfffffe0007613240
--defsym=err_inactive=0xfffffe0007613250
--defsym=err_pathconf=0xfffffe0007613280
--defsym=err_advlock=0xfffffe0007613290
--defsym=err_bwrite=0xfffffe00076132c0
--defsym=err_pageout=0xfffffe00076133a0
--defsym=err_copyfile=0xfffffe0007613410
--defsym=err_blktooff=0xfffffe000761342c
--defsym=err_offtoblk=0xfffffe0007613454
--defsym=vnop_default_desc=0xfffffe0007c865e8
--defsym=vnop_lookup_desc=0xfffffe0007c86630
--defsym=vnop_create_desc=0xfffffe0007c866c0
--defsym=vnop_open_desc=0xfffffe0007c86798
--defsym=vnop_mknod_desc=0xfffffe0007c86750
--defsym=vnop_close_desc=0xfffffe0007c867e0
--defsym=vnop_access_desc=0xfffffe0007c86828
--defsym=vnop_getattr_desc=0xfffffe0007c86870
--defsym=vnop_setattr_desc=0xfffffe0007c868b8
--defsym=vnop_read_desc=0xfffffe0007c86900
--defsym=vnop_write_desc=0xfffffe0007c86948
--defsym=vnop_ioctl_desc=0xfffffe0007c86990
--defsym=vnop_select_desc=0xfffffe0007c869d8
--defsym=vnop_mmap_desc=0xfffffe0007c86c20
--defsym=vnop_fsync_desc=0xfffffe0007c86cb0
--defsym=vnop_remove_desc=0xfffffe0007c86d00
--defsym=vnop_link_desc=0xfffffe0007c86d98
--defsym=vnop_rename_desc=0xfffffe0007c86df0
--defsym=vnop_mkdir_desc=0xfffffe0007c86ee8
--defsym=vnop_rmdir_desc=0xfffffe0007c86f80
--defsym=vnop_symlink_desc=0xfffffe0007c87010
--defsym=vnop_readdir_desc=0xfffffe0007c87058
--defsym=vnop_readlink_desc=0xfffffe0007c87130
--defsym=vnop_inactive_desc=0xfffffe0007c87178
--defsym=vnop_reclaim_desc=0xfffffe0007c871c0
--defsym=vnop_strategy_desc=0xfffffe0007c87728
--defsym=vnop_pathconf_desc=0xfffffe0007c87208
--defsym=vnop_advlock_desc=0xfffffe0007c87250
--defsym=vnop_bwrite_desc=0xfffffe0007c87770
--defsym=vnop_pagein_desc=0xfffffe0007c872e0
--defsym=vnop_pageout_desc=0xfffffe0007c87328
--defsym=vnop_copyfile_desc=0xfffffe0007c873c0
--defsym=vnop_blktooff_desc=0xfffffe0007c87578
--defsym=vnop_offtoblk_desc=0xfffffe0007c875c0
--defsym=vnop_blockmap_desc=0xfffffe0007c87608
"
# fix copyfile desc if wrong
aarch64-linux-gnu-ld -T mockfs.ld --no-warn-rwx-segments -e lapis_mountroot_wrap $DS \
    -o mockfs.elf lapis_mockfs.o
aarch64-linux-gnu-objcopy -O binary -j .text mockfs.elf mockfs.bin
sz=$(stat -c %s mockfs.bin)
echo "mockfs.bin $sz bytes"
[ $sz -le 31000 ] || { echo too big; exit 1; }
aarch64-linux-gnu-nm mockfs.elf | grep -E "lapis_mountroot_wrap|mockfs_mount|registered"
