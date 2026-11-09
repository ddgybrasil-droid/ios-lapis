/* blk.h - lapis block layer: disks from LapisUFS / LapisSDHCI -> GPT partitions ->
 * BSD block+char devices (/dev/diskN, /dev/diskNsM, /dev/rdisk...) and the lapisfs
 * root locator.  There is no IOStorageFamily (kext) in this kernel, so no IOMedia. */
#pragma once
#include "kern.h"

struct lapis_disk {
	char      name[16];          /* "ufs-lun0", "sd0" */
	uint32_t  bs;                /* logical block size */
	uint64_t  nblocks;
	int       writable;          /* write path enabled (boot-arg gated by the driver) */
	/* synchronous transfer; va may be any kernel VA, n*bs bytes; 0 = ok */
	int     (*xfer)(void *ctx, uint64_t lba, uint32_t n, void *va, int write);
	void     *ctx;
	uint32_t  max_blocks;        /* per xfer() call */
};

/* registered disks are scanned (GPT or raw LAPISFS1 at LBA 0) and published */
int  blk_register(struct lapis_disk *d);
/* drivers that may still register disks hold a "pending" count while probing */
void blk_pending_add(int delta);
void blk_start(void);                /* called once from lapis_drivers_start */

/* fixed-address hand-off to lapisfs (xnu/lapis-mockfs): first bytes of the drivers BSS */
#define LAPIS_BLKOPS_MAGIC 0x4c424c4b   /* 'LBLK' */
struct lapis_blkops {
	uint32_t magic;
	volatile int32_t state;      /* 0 probing, 1 root found, -1 no root (all drivers done) */
	int    (*read)(uint64_t off, uint64_t len, void *va);   /* byte offset in root image */
	uint64_t root_size;          /* bytes of the partition / raw disk holding LAPISFS1 */
	char     root_name[32];      /* "disk0s2" */
	uint64_t reserved;
};
