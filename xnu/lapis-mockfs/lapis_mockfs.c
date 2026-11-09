/* lapisfs (s11): read-only multi-file fs over md0 (was: single-file mockfs).
 * Image format (tools/ramdisk/mklapisfs.py), all little-endian:
 *   0x00 "LAPISFS1"  0x08 u32 nent  0x0c u32 ver(2)  0x10 u64 ent_off  0x18 u64 str_off  0x20 u64 total
 *   entry (32B): u32 parent, u16 type(1 dir,2 reg), u16 mode, u64 data_off, u64 size,
 *                u32 name_off, u32 name_len.   entry 0 = root. data 16K aligned.
 * Metadata is read straight from the md0 mapping (mdevgetrange returns a kernel VA);
 * file data goes through cluster_read/cluster_pagein -> blockmap -> md0 strategy.
 * Registered at runtime via vfs_fsadd(VFS_TBLCANMOUNTROOT). The kernel's
 * built-in HFS/APFS are kexts, so without a kernelcache mockfs is the only
 * way to mountroot from rd=md0. Freestanding; no writable data sections. */
#include <stdint.h>
#include <stddef.h>

typedef void *vnode_t;
typedef void *mount_t;
typedef void *vfs_context_t;
typedef void *buf_t;
typedef void *upl_t;
typedef int64_t off_t;
typedef int64_t daddr64_t;
typedef int32_t errno_t;
typedef uint32_t mode_t;

enum { VNON=0, VREG=1, VDIR=2 };
enum { LOOKUP=0 };
enum {
	VNFS_NOCACHE=1, VNFS_CANTCACHE=2,
	VFS_TBLTHREADSAFE=1, VFS_TBLNOTYPENUM=8, VFS_TBL64BITREADY=0x20,
	VFS_TBLCANMOUNTROOT=0x20000,
	VNCREATE_FLAVOR=0,
	FORCECLOSE=2,
};
#define MFSNAMELEN 15
#define EINVAL 22
#define ENOENT 2
#define ENOMEM 12
#define ENOTSUP 45
#define EIO 5
#define ENOTDIR_ 20
#define EISDIR_ 21

#define DKIOCGETBLOCKSIZE  0x40046418u
#define DKIOCGETBLOCKCOUNT 0x40086419u

#define VA_nlink       (1ULL<<1)
#define VA_total_size  (1ULL<<2)
#define VA_total_alloc (1ULL<<3)
#define VA_data_size   (1ULL<<4)
#define VA_data_alloc  (1ULL<<5)
#define VA_mode        (1ULL<<9)
#define VA_fileid      (1ULL<<17)
#define VA_type        (1ULL<<24)
#define VREAD 0400
#define VWRITE 0200
#define VEXEC 0100


struct componentname {
	uint32_t cn_nameiop, cn_flags;
	void *cn_r1, *cn_r2;
	char *cn_pnbuf;
	int cn_pnlen;
	char *cn_nameptr;
	int cn_namelen;
	uint32_t cn_hash, cn_consume;
};

struct vnode_fsparam {
	mount_t vnfs_mp;
	int vnfs_vtype;
	const char *vnfs_str;
	vnode_t vnfs_dvp;
	void *vnfs_fsnode;
	int (**vnfs_vops)(void *);
	int vnfs_markroot, vnfs_marksystem;
	int32_t vnfs_rdev;
	off_t vnfs_filesize;
	struct componentname *vnfs_cnp;
	uint32_t vnfs_flags;
};

struct vfsops {
	int (*vfs_mount)(mount_t, vnode_t, uint64_t, vfs_context_t);
	int (*vfs_start)(mount_t, int, vfs_context_t);
	int (*vfs_unmount)(mount_t, int, vfs_context_t);
	int (*vfs_root)(mount_t, vnode_t *, vfs_context_t);
	int (*vfs_quotactl)(mount_t, int, uint32_t, void *, vfs_context_t);
	int (*vfs_getattr)(mount_t, void *, vfs_context_t);
	int (*vfs_sync)(mount_t, int, vfs_context_t);
	int (*vfs_vget)(mount_t, uint64_t, vnode_t *, vfs_context_t);
	int (*vfs_fhtovp)(mount_t, int, unsigned char *, vnode_t *, vfs_context_t);
	int (*vfs_vptofh)(vnode_t, int *, unsigned char *, vfs_context_t);
	int (*vfs_init)(void *);
	int (*vfs_sysctl)(int *, unsigned, uint64_t, size_t *, uint64_t, size_t, vfs_context_t);
	int (*vfs_setattr)(mount_t, void *, vfs_context_t);
	int (*vfs_ioctl)(mount_t, unsigned long, void *, int, vfs_context_t);
	int (*vfs_vget_snapdir)(mount_t, vnode_t *, vfs_context_t);
	void *r5, *r4, *r3, *r2, *r1;
};

struct vfs_fsentry {
	struct vfsops *vfe_vfsops;
	int vfe_vopcnt;
	struct vnodeopv_desc **vfe_opvdescs;
	int vfe_fstypenum;
	char vfe_fsname[MFSNAMELEN];
	uint32_t vfe_flags;
	void *vfe_reserv[2];
};

struct vnodeopv_entry_desc {
	void *opve_op;
	int (*opve_impl)(void *);
};
struct vnodeopv_desc {
	int (***opv_desc_vector_p)(void *);
	const struct vnodeopv_entry_desc *opv_desc_ops;
};

struct vnop_lookup_args { void *a_desc; vnode_t a_dvp; vnode_t *a_vpp; struct componentname *a_cnp; vfs_context_t a_context; };
struct vnop_getattr_args { void *a_desc; vnode_t a_vp; void *a_vap; vfs_context_t a_context; };
struct vnop_read_args { void *a_desc; vnode_t a_vp; void *a_uio; int a_ioflag; vfs_context_t a_context; };
struct vnop_pagein_args { void *a_desc; vnode_t a_vp; upl_t a_pl; uint64_t a_pl_offset; off_t a_f_offset; size_t a_size; int a_flags; vfs_context_t a_context; };
struct vnop_strategy_args { void *a_desc; buf_t a_bp; };
struct vnop_blockmap_args { void *a_desc; vnode_t a_vp; off_t a_foffset; size_t a_size; daddr64_t *a_bpn; size_t *a_run; void *a_poff; int a_flags; vfs_context_t a_context; };
struct vnop_reclaim_args { void *a_desc; vnode_t a_vp; vfs_context_t a_context; };

struct lfs_ent { uint32_t parent; uint16_t type, mode; uint64_t data_off, size; uint32_t name_off, name_len; };
struct fsnode {
	uint32_t idx;
	uint8_t type;
	mount_t mnt;
	vnode_t vp;
	const struct lfs_ent *e;
};
struct mnt_data {
	const uint8_t *base;
	uint32_t nent;
	const struct lfs_ent *ents;
	const char *strs;
	struct fsnode **nodes;
	uint64_t total;
};
enum { LFS_DIR = 1, LFS_REG = 2 };

/* LapisBlk hand-off (drivers BSS, fixed VA — see drivers/drv.ld). */
#define LAPIS_BLKOPS_VA   ((struct lapis_blkops *)0xfffffe0007daf000)
#define LAPIS_BLKOPS_MAGIC 0x4c424c4b
struct lapis_blkops {
	uint32_t magic;
	volatile int32_t state;
	int (*read)(uint64_t off, uint64_t len, void *va);
	uint64_t root_size;
	char root_name[32];
	uint64_t reserved;
};

struct vnop_readlink_args { void *a_desc; vnode_t a_vp; void *a_uio; vfs_context_t a_context; };

/* kernel imports */
extern void *IOMallocZero(size_t);
extern void IOFree(void *, size_t);
extern void *kalloc_data(size_t, int);
extern void kfree_data(void *, size_t);
extern int vnode_create(uint32_t, size_t, void *, vnode_t *);
extern int vnode_get(vnode_t);
extern int vnode_put(vnode_t);
extern void *vnode_fsnode(vnode_t);
extern void vnode_clearfsnode(vnode_t);
extern int vnode_vtype(vnode_t);
extern mount_t vnode_mount(vnode_t);
extern vnode_t vfs_devvp(mount_t);
extern void *vfs_fsprivate(mount_t);
extern void vfs_setfsprivate(mount_t, void *);
extern uint32_t vfs_devblocksize(mount_t);
extern int vfs_fsadd(struct vfs_fsentry *, void **);
extern int mdevgetrange(int, uint64_t *, uint64_t *);
extern int cluster_read(vnode_t, void *, off_t, int);
extern int cluster_pagein(vnode_t, upl_t, uint64_t, off_t, size_t, off_t, int);
extern int buf_strategy(vnode_t, void *);
extern daddr64_t buf_blkno(buf_t);
extern uint32_t buf_count(buf_t), buf_flags(buf_t);
extern int buf_map(buf_t, char **), buf_unmap(buf_t);
extern void buf_setresid(buf_t, uint32_t), buf_biodone(buf_t), buf_seterror(buf_t, int);
extern void *memcpy(void *, const void *, size_t), *memset(void *, int, size_t);
#define B_READ_ 0x1
extern vnode_t buf_vnode(buf_t);
extern int strncmp(const char *, const char *, size_t);
extern int IOLog(const char *, ...);
extern int vn_default_error(void *);
extern int err_create(void*), err_open(void*), err_mknod(void*), err_close(void*);
extern int nop_open(void*), nop_close(void*);
extern int err_access(void*), err_setattr(void*), err_write(void*), err_ioctl(void*);
extern int err_select(void*), err_mmap(void*), nop_fsync(void*), err_remove(void*);
extern int err_link(void*), err_rename(void*), err_mkdir(void*), err_rmdir(void*);
extern int err_symlink(void*), err_readdir(void*), err_readlink(void*), err_inactive(void*);
extern int err_pathconf(void*), err_advlock(void*), err_bwrite(void*), err_pageout(void*);
extern int err_copyfile(void*), err_blktooff(void*), err_offtoblk(void*);
extern int _vfs_mountroot(void);
/* vnodeop_desc symbols */
extern char vnop_default_desc, vnop_lookup_desc, vnop_create_desc, vnop_open_desc;
extern char vnop_mknod_desc, vnop_close_desc, vnop_access_desc, vnop_getattr_desc;
extern char vnop_setattr_desc, vnop_read_desc, vnop_write_desc, vnop_ioctl_desc;
extern char vnop_select_desc, vnop_mmap_desc, vnop_fsync_desc, vnop_remove_desc;
extern char vnop_link_desc, vnop_rename_desc, vnop_mkdir_desc, vnop_rmdir_desc;
extern char vnop_symlink_desc, vnop_readdir_desc, vnop_readlink_desc, vnop_inactive_desc;
extern char vnop_reclaim_desc, vnop_strategy_desc, vnop_pathconf_desc, vnop_advlock_desc;
extern char vnop_bwrite_desc, vnop_pagein_desc, vnop_pageout_desc, vnop_copyfile_desc;
extern char vnop_blktooff_desc, vnop_offtoblk_desc, vnop_blockmap_desc;

/* Writable slots in DEVELOPMENT ktest BSS (TEXT is RX even with CTRR off). */
#define MOCKFS_VNODEOP_P  ((int (***)(void *))0xfffffe0007d260a0)  /* _ktest_temp1 */
#define MOCKFS_REGISTERED ((int *)0xfffffe0007d260a8)               /* _ktest_temp2 */
#define MOCKFS_VFSHANDLE  ((void **)0xfffffe0007d260b0)             /* _ktest_temp3 */


/* Apple arm64 ABI passes variadic args on the stack; GCC uses registers.
 * lapis_log(fmt, a, b, c): put a,b,c in stack slots and call IOLog. */
extern int lapis_log(const char *fmt, uint64_t a, uint64_t b, uint64_t c);
__asm__(
"	.text\n	.global lapis_log\n	.type lapis_log,%function\n"
"lapis_log:\n"
"	stp x29, x30, [sp, #-16]!\n	mov x29, sp\n"
"	sub sp, sp, #32\n	stp x1, x2, [sp]\n	str x3, [sp, #16]\n"
"	bl IOLog\n"
"	add sp, sp, #32\n	ldp x29, x30, [sp], #16\n	ret\n");

static int node_get(struct mnt_data *md, mount_t mp, uint32_t idx, vnode_t dvp, vnode_t *vpp)
{
	struct fsnode *n = md->nodes[idx];
	struct vnode_fsparam p;
	int err;
	if (!n) {
		n = IOMallocZero(sizeof(*n));
		if (!n) return ENOMEM;
		n->idx = idx; n->mnt = mp; n->e = &md->ents[idx]; n->type = (uint8_t)n->e->type;
		md->nodes[idx] = n;
	}
	if (n->vp) {
		err = vnode_get(n->vp);
		if (!err) *vpp = n->vp;
		return err;
	}
	p.vnfs_mp = mp;
	p.vnfs_vtype = (n->type == LFS_REG) ? VREG : VDIR;
	p.vnfs_str = "lapisfs";
	p.vnfs_dvp = idx ? dvp : 0;
	p.vnfs_fsnode = n;
	p.vnfs_vops = *MOCKFS_VNODEOP_P;
	p.vnfs_markroot = (idx == 0);
	p.vnfs_marksystem = 0;
	p.vnfs_rdev = 0;
	p.vnfs_filesize = (off_t)n->e->size;
	p.vnfs_cnp = 0;
	p.vnfs_flags = VNFS_CANTCACHE | VNFS_NOCACHE;
	err = vnode_create(VNCREATE_FLAVOR, sizeof(p), &p, &n->vp);
	if (!err) *vpp = n->vp;
	return err;
}

static int mockfs_mount(mount_t mp, vnode_t rvp, uint64_t data, vfs_context_t ctx)
{
	struct mnt_data *md;
	uint64_t base = 0, sz = 0;
	const uint8_t *b;
	struct lapis_blkops *bo = LAPIS_BLKOPS_VA;
	int use_blk = 0;
	(void)rvp; (void)data; (void)ctx;
	/* Prefer a real disk root published by LapisBlk (UFS/SD). Wait up to ~3s. */
	if (bo->magic == LAPIS_BLKOPS_MAGIC) {
		int i; for (i = 0; i < 300 && bo->state == 0; i++) {
			/* IOSleep not linked here; busy-wait via a tiny pause */
			volatile int spin; for (spin = 0; spin < 100000; spin++) ;
		}
		if (bo->state == 1 && bo->read && bo->root_size >= 0x4000) use_blk = 1;
	}
	if (use_blk) {
		/* Stage the image header (and later file data) via bo->read into a
		 * kernel buffer for metadata; file data goes through strategy. */
		uint8_t *hdr = IOMallocZero(0x4000);
		if (!hdr || bo->read(0, 0x4000, hdr)) { if (hdr) IOFree(hdr, 0x4000); return EIO; }
		if (strncmp((const char *)hdr, "LAPISFS1", 8) || *(const uint32_t *)(hdr + 12) != 2) {
			IOLog("LAPIS lapisfs: bad magic on blk root\n"); IOFree(hdr, 0x4000); return EINVAL;
		}
		md = IOMallocZero(sizeof(*md));
		if (!md) { IOFree(hdr, 0x4000); return ENOMEM; }
		md->base = hdr;   /* only the first page staged; strategy uses bo->read */
		md->nent = *(const uint32_t *)(hdr + 8);
		/* For blk roots we need the full metadata (ents+strs). Read them too. */
		{
			uint64_t ent_off = *(const uint64_t *)(hdr + 16);
			uint64_t str_off = *(const uint64_t *)(hdr + 24);
			uint64_t total = *(const uint64_t *)(hdr + 32);
			uint64_t meta_end = str_off + 0x10000; /* generous; clamp */
			if (meta_end > total) meta_end = total;
			if (meta_end > 0x4000) {
				uint8_t *full = IOMallocZero((size_t)meta_end);
				if (!full) { IOFree(hdr, 0x4000); IOFree(md, sizeof(*md)); return ENOMEM; }
				if (bo->read(0, meta_end, full)) { IOFree(full,(size_t)meta_end); IOFree(hdr,0x4000); IOFree(md,sizeof(*md)); return EIO; }
				IOFree(hdr, 0x4000); hdr = full; md->base = hdr;
			}
			md->ents = (const struct lfs_ent *)(hdr + ent_off);
			md->strs = (const char *)(hdr + str_off);
			md->total = total;
		}
		md->nodes = IOMallocZero(sizeof(void *) * md->nent);
		if (!md->nodes) return ENOMEM;
		vfs_setfsprivate(mp, md);
		/* Stash blkops pointer in reserved via a side channel: encode use_blk in nent top? 
		 * Instead: strategy checks LAPIS_BLKOPS_VA->state. */
		lapis_log("LAPIS lapisfs: mounted blk root size=%llx entries=%llu state=%llu\n", md->total, md->nent, (uint64_t)bo->state);
		return 0;
	}
	if (mdevgetrange(0, &base, &sz) != 0 || sz < 0x4000) return EINVAL;
	b = (const uint8_t *)base;
	if (strncmp((const char *)b, "LAPISFS1", 8) || *(const uint32_t *)(b + 12) != 2) { IOLog("LAPIS lapisfs: bad magic on md0\n"); return EINVAL; }
	md = IOMallocZero(sizeof(*md));
	if (!md) return ENOMEM;
	md->base = b;
	md->nent = *(const uint32_t *)(b + 8);
	md->ents = (const struct lfs_ent *)(b + *(const uint64_t *)(b + 16));
	md->strs = (const char *)(b + *(const uint64_t *)(b + 24));
	md->total = *(const uint64_t *)(b + 32);
	md->nodes = IOMallocZero(sizeof(void *) * md->nent);
	if (!md->nodes) return ENOMEM;
	vfs_setfsprivate(mp, md);
	lapis_log("LAPIS lapisfs: mounted md0 va=%llx image=%llx entries=%llu\n", base, md->total, md->nent);
	return 0;
}

static int mockfs_unmount(mount_t mp, int flags, vfs_context_t ctx)
{
	(void)mp; (void)flags; (void)ctx;
	return 0; /* bring-up: never unmounted */
}

static int mockfs_root(mount_t mp, vnode_t *vpp, vfs_context_t ctx)
{
	(void)ctx;
	return node_get(vfs_fsprivate(mp), mp, 0, 0, vpp);
}

static int mockfs_sync(mount_t mp, int w, vfs_context_t ctx) { (void)mp;(void)w;(void)ctx; return 0; }
static int mockfs_init(void *v) { (void)v; return 0; }

static int mockfs_lookup(struct vnop_lookup_args *ap)
{
	struct fsnode *d = vnode_fsnode(ap->a_dvp);
	struct mnt_data *md = vfs_fsprivate(d->mnt);
	struct componentname *cn = ap->a_cnp;
	uint32_t i, nl = (uint32_t)cn->cn_namelen;
	if (d->type != LFS_DIR) return ENOTDIR_;
	if (nl == 1 && cn->cn_nameptr[0] == '.') return node_get(md, d->mnt, d->idx, ap->a_dvp, ap->a_vpp);
	if (nl == 2 && cn->cn_nameptr[0] == '.' && cn->cn_nameptr[1] == '.')
		return node_get(md, d->mnt, d->e->parent, ap->a_dvp, ap->a_vpp);
	for (i = 1; i < md->nent; i++) {
		const struct lfs_ent *e = &md->ents[i];
		if (e->parent == d->idx && e->name_len == nl && !strncmp(md->strs + e->name_off, cn->cn_nameptr, nl))
			return node_get(md, d->mnt, i, ap->a_dvp, ap->a_vpp);
	}
	return ENOENT;
}

static int mockfs_getattr(struct vnop_getattr_args *ap)
{
	/* offsetof(vnode_attr, ...) from xnu-12377 arm64. Only va_supported is
	 * ours to set; va_active is the caller's request mask. */
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	char *vap = (char *)ap->a_vap;
	uint64_t *supported = (uint64_t *)(vap + 0);
	uint64_t sz = fsn->e->size;
	*(uint64_t *)(vap + 24) = (fsn->type == LFS_DIR) ? 2 : 1; *supported |= VA_nlink;
	*(uint64_t *)(vap + 32) = sz;    *supported |= VA_total_size;
	*(uint64_t *)(vap + 40) = sz;    *supported |= VA_total_alloc;
	*(uint64_t *)(vap + 48) = sz;    *supported |= VA_data_size;
	*(uint64_t *)(vap + 56) = sz;    *supported |= VA_data_alloc;
	*(uint32_t *)(vap + 68) = 0;     *supported |= (1ULL << 7);  /* va_uid root */
	*(uint32_t *)(vap + 72) = 0;     *supported |= (1ULL << 8);  /* va_gid wheel */
	*(uint16_t *)(vap + 76) = fsn->e->mode & 07777; *supported |= VA_mode;
	*(uint64_t *)(vap + 176) = fsn->idx + 2; *supported |= VA_fileid;
	return 0;
}

static int mockfs_read(struct vnop_read_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	if (vnode_vtype(ap->a_vp) != VREG) return EISDIR_;
	return cluster_read(ap->a_vp, ap->a_uio, (off_t)fsn->e->size, ap->a_ioflag);
}

static int mockfs_reclaim(struct vnop_reclaim_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	if (fsn) { fsn->vp = 0; vnode_clearfsnode(ap->a_vp); }
	return 0;
}

/* md0 can't address >4 GiB (memdev computes uint32 mdSize << 12), so file I/O
 * bypasses the md strategy and copies straight from the ramdisk mapping. */
static int mockfs_strategy(struct vnop_strategy_args *ap)
{
	buf_t bp = ap->a_bp;
	struct fsnode *f = vnode_fsnode(buf_vnode(bp));
	struct mnt_data *md = vfs_fsprivate(f->mnt);
	uint32_t blk = vfs_devblocksize(f->mnt);
	uint64_t off, cnt, n;
	char *va;
	struct lapis_blkops *bo = LAPIS_BLKOPS_VA;
	if (!blk) blk = 512;
	off = (uint64_t)buf_blkno(bp) * blk;
	cnt = buf_count(bp);
	if (!(buf_flags(bp) & B_READ_) || buf_map(bp, &va)) {
		buf_seterror(bp, EIO); buf_biodone(bp); return EIO;
	}
	n = (off < md->total) ? md->total - off : 0;
	if (n > cnt) n = cnt;
	if (n) {
		/* If LapisBlk owns the root, file data lives on the disk — use its read. */
		if (bo->magic == LAPIS_BLKOPS_MAGIC && bo->state == 1 && bo->read &&
		    off + n > 0x10000) {          /* past the staged metadata window */
			if (bo->read(off, n, va)) { buf_unmap(bp); buf_seterror(bp, EIO); buf_biodone(bp); return EIO; }
		} else {
			memcpy(va, md->base + off, (size_t)n);
		}
	}
	if (n < cnt) memset(va + n, 0, (size_t)(cnt - n));
	buf_unmap(bp);
	buf_setresid(bp, 0);
	buf_biodone(bp);
	return 0;
}

static int mockfs_pagein(struct vnop_pagein_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	return cluster_pagein(ap->a_vp, ap->a_pl, ap->a_pl_offset, ap->a_f_offset,
	                      ap->a_size, (off_t)fsn->e->size, ap->a_flags);
}

static int mockfs_blockmap(struct vnop_blockmap_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	uint32_t blk = vfs_devblocksize(fsn->mnt);
	uint64_t sz = fsn->e->size;
	if (!blk) blk = 512;
	if (vnode_vtype(ap->a_vp) != VREG) return ENOTSUP;
	*ap->a_bpn = (fsn->e->data_off + (uint64_t)ap->a_foffset) / blk;
	if (ap->a_run) {
		/* data is 16K-aligned and padded, so whole pages past EOF are in-image */
		uint64_t end = (sz + 0x3fff) & ~0x3fffull;
		uint64_t remain = (end > (uint64_t)ap->a_foffset) ? end - (uint64_t)ap->a_foffset : 0;
		*ap->a_run = remain < ap->a_size ? (size_t)remain : ap->a_size;
	}
	if (ap->a_poff) *(int*)ap->a_poff = 0;
	return 0;
}

static int lfs_zero(void *ap) { (void)ap; return 0; }

static const struct vnodeopv_entry_desc mockfs_entries[] = {
	{ &vnop_default_desc,  (int(*)(void*))vn_default_error },
	{ &vnop_lookup_desc,   (int(*)(void*))mockfs_lookup },
	{ &vnop_create_desc,   (int(*)(void*))err_create },
	{ &vnop_open_desc,     (int(*)(void*))nop_open },
	{ &vnop_mknod_desc,    (int(*)(void*))err_mknod },
	{ &vnop_close_desc,    (int(*)(void*))nop_close },
	{ &vnop_access_desc,   (int(*)(void*))lfs_zero },
	{ &vnop_getattr_desc,  (int(*)(void*))mockfs_getattr },
	{ &vnop_setattr_desc,  (int(*)(void*))err_setattr },
	{ &vnop_read_desc,     (int(*)(void*))mockfs_read },
	{ &vnop_write_desc,    (int(*)(void*))err_write },
	{ &vnop_ioctl_desc,    (int(*)(void*))err_ioctl },
	{ &vnop_select_desc,   (int(*)(void*))err_select },
	{ &vnop_mmap_desc,     (int(*)(void*))lfs_zero },
	{ &vnop_fsync_desc,    (int(*)(void*))nop_fsync },
	{ &vnop_remove_desc,   (int(*)(void*))err_remove },
	{ &vnop_link_desc,     (int(*)(void*))err_link },
	{ &vnop_rename_desc,   (int(*)(void*))err_rename },
	{ &vnop_mkdir_desc,    (int(*)(void*))err_mkdir },
	{ &vnop_rmdir_desc,    (int(*)(void*))err_rmdir },
	{ &vnop_symlink_desc,  (int(*)(void*))err_symlink },
	{ &vnop_readdir_desc,  (int(*)(void*))err_readdir },
	{ &vnop_readlink_desc, (int(*)(void*))err_readlink },
	{ &vnop_inactive_desc, (int(*)(void*))lfs_zero },
	{ &vnop_reclaim_desc,  (int(*)(void*))mockfs_reclaim },
	{ &vnop_strategy_desc, (int(*)(void*))mockfs_strategy },
	{ &vnop_pathconf_desc, (int(*)(void*))err_pathconf },
	{ &vnop_advlock_desc,  (int(*)(void*))err_advlock },
	{ &vnop_bwrite_desc,   (int(*)(void*))err_bwrite },
	{ &vnop_pagein_desc,   (int(*)(void*))mockfs_pagein },
	{ &vnop_pageout_desc,  (int(*)(void*))err_pageout },
	{ &vnop_copyfile_desc, (int(*)(void*))err_copyfile },
	{ &vnop_blktooff_desc, (int(*)(void*))err_blktooff },
	{ &vnop_offtoblk_desc, (int(*)(void*))err_offtoblk },
	{ &vnop_blockmap_desc, (int(*)(void*))mockfs_blockmap },
	{ 0, 0 }
};
static const struct vnodeopv_desc mockfs_opv_desc = {
	.opv_desc_vector_p = MOCKFS_VNODEOP_P,
	.opv_desc_ops = mockfs_entries
};

static struct vnodeopv_desc *mockfs_opv_descs[] = {
	(struct vnodeopv_desc *)&mockfs_opv_desc, 0
};

static const struct vfsops mockfs_vfsops = {
	.vfs_mount = mockfs_mount,
	.vfs_unmount = mockfs_unmount,
	.vfs_root = mockfs_root,
	.vfs_sync = mockfs_sync,
	.vfs_init = mockfs_init,
};


/* Called in place of the first vfs_mountroot from bsd_init. */
int lapis_mountroot_wrap(void)
{
	if (!*MOCKFS_REGISTERED) {
		struct vfs_fsentry vfe;
		int i;
		for (i = 0; i < (int)sizeof(vfe); i++) ((char*)&vfe)[i] = 0;
		vfe.vfe_vfsops = (struct vfsops *)&mockfs_vfsops;
		vfe.vfe_vopcnt = 1;
		vfe.vfe_opvdescs = mockfs_opv_descs;
		vfe.vfe_fstypenum = 0;
		vfe.vfe_fsname[0]='l'; vfe.vfe_fsname[1]='a'; vfe.vfe_fsname[2]='p';
		vfe.vfe_fsname[3]='i'; vfe.vfe_fsname[4]='s'; vfe.vfe_fsname[5]='f'; vfe.vfe_fsname[6]='s';
		vfe.vfe_flags = VFS_TBLTHREADSAFE | VFS_TBLNOTYPENUM | VFS_TBL64BITREADY | VFS_TBLCANMOUNTROOT;
		if (vfs_fsadd(&vfe, MOCKFS_VFSHANDLE))
			IOLog("LAPIS lapisfs: vfs_fsadd FAILED\n");
		else {
			IOLog("LAPIS lapisfs: registered\n");
			*MOCKFS_REGISTERED = 1;
		}
	}
	return _vfs_mountroot();
}

/* s11: AMFI is a kext (absent). bsd/osfmk call amfi->X(...) through the global
 * `amfi` (0xfffffe0007277430), often without NULL checks. Point it (static
 * binpatch) at this read-only table: every slot returns 0 (KERN_SUCCESS /
 * false / NULL). Bring-up only: no code-signing policy, no entitlements. */
static long lapis_amfi_ret0(void) { return 0; }
const void *const lapis_amfi_stub[256] = { [0 ... 255] = (const void *)lapis_amfi_ret0 };
