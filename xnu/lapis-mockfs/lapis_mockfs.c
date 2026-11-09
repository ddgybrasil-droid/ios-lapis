/* Minimal mockfs for lapis: root + /dev + /launchd(=entire md device).
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
enum { MOCKFS_ROOT=0, MOCKFS_DEV=1, MOCKFS_FILE=2 };
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

struct fsnode {
	uint64_t size;
	uint8_t type;
	mount_t mnt;
	vnode_t vp;
	struct fsnode *parent, *child_a, *child_b;
};
struct mnt_data {
	void *lock; /* unused (single CPU) */
	struct fsnode *root;
	uint64_t filesize;
};

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

static struct fsnode *node_new(mount_t mp, uint8_t type, uint64_t size)
{
	struct fsnode *n = IOMallocZero(sizeof(*n));
	if (!n) return 0;
	n->type = type; n->mnt = mp; n->size = size;
	return n;
}

static int node_adopt(struct fsnode *p, struct fsnode *c)
{
	if (!p || !c || c->parent) return EINVAL;
	if (!p->child_a) { p->child_a = c; c->parent = p; return 0; }
	if (!p->child_b) { p->child_b = c; c->parent = p; return 0; }
	return ENOMEM;
}

static struct fsnode *child_by_type(struct fsnode *p, uint8_t t)
{
	if (p->child_a && p->child_a->type == t) return p->child_a;
	if (p->child_b && p->child_b->type == t) return p->child_b;
	return 0;
}

static int node_vnode(struct fsnode *n, vnode_t *vpp)
{
	struct vnode_fsparam p;
	int err;
	if (n->vp) {
		err = vnode_get(n->vp);
		if (!err) *vpp = n->vp;
		return err;
	}
	p.vnfs_mp = n->mnt;
	p.vnfs_vtype = (n->type == MOCKFS_FILE) ? VREG : VDIR;
	p.vnfs_str = "mockfs";
	p.vnfs_dvp = (n->type == MOCKFS_ROOT) ? 0 : (n->parent ? n->parent->vp : 0);
	p.vnfs_fsnode = n;
	p.vnfs_vops = *MOCKFS_VNODEOP_P;
	p.vnfs_markroot = (n->type == MOCKFS_ROOT);
	p.vnfs_marksystem = 0;
	p.vnfs_rdev = 0;
	p.vnfs_filesize = (off_t)n->size;
	p.vnfs_cnp = 0;
	p.vnfs_flags = VNFS_CANTCACHE | VNFS_NOCACHE;
	err = vnode_create(VNCREATE_FLAVOR, sizeof(p), &p, &n->vp);
	if (!err) *vpp = n->vp;
	return err;
}

static uint64_t device_size(vnode_t rvp)
{
	uint64_t base = 0, sz = 0;
	(void)rvp;
	if (mdevgetrange(0, &base, &sz) == 0 && sz && sz < (1ull << 30))
		return sz;
	/* Fallback: our launchd.macho ramdisk is one 16K page. */
	return 0x4000;
}

static int mockfs_mount(mount_t mp, vnode_t rvp, uint64_t data, vfs_context_t ctx)
{
	struct mnt_data *md;
	struct fsnode *root, *dev, *file;
	uint64_t sz;
	(void)data; (void)ctx;
	sz = device_size(rvp);
	IOLog("LAPIS mockfs: mounting md root, size=0x%llx\n", (unsigned long long)sz);
	md = IOMallocZero(sizeof(*md));
	root = node_new(mp, MOCKFS_ROOT, 0);
	dev  = node_new(mp, MOCKFS_DEV, 0);
	file = node_new(mp, MOCKFS_FILE, sz);
	if (!md || !root || !dev || !file) return ENOMEM;
	node_adopt(root, dev);
	node_adopt(root, file);
	md->root = root;
	md->filesize = sz;
	vfs_setfsprivate(mp, md);
	return 0;
}

static int mockfs_unmount(mount_t mp, int flags, vfs_context_t ctx)
{
	(void)mp; (void)flags; (void)ctx;
	return 0; /* bring-up: never unmounted */
}

static int mockfs_root(mount_t mp, vnode_t *vpp, vfs_context_t ctx)
{
	struct mnt_data *md = vfs_fsprivate(mp);
	(void)ctx;
	return node_vnode(md->root, vpp);
}

static int mockfs_sync(mount_t mp, int w, vfs_context_t ctx) { (void)mp;(void)w;(void)ctx; return 0; }
static int mockfs_init(void *v) { (void)v; return 0; }

static int mockfs_lookup(struct vnop_lookup_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_dvp);
	struct fsnode *tgt = 0;
	struct componentname *cn = ap->a_cnp;
	char held;
	int r = ENOENT;
	if (cn->cn_nameiop != LOOKUP || fsn->type != MOCKFS_ROOT) return ENOENT;
	held = cn->cn_nameptr[cn->cn_namelen];
	cn->cn_nameptr[cn->cn_namelen] = 0;
	if (!strncmp(cn->cn_nameptr, "sbin", 5)) tgt = fsn;
	else if (!strncmp(cn->cn_nameptr, "dev", 4)) tgt = child_by_type(fsn, MOCKFS_DEV);
	else if (!strncmp(cn->cn_nameptr, "launchd", 8)) tgt = child_by_type(fsn, MOCKFS_FILE);
	cn->cn_nameptr[cn->cn_namelen] = held;
	if (tgt) r = node_vnode(tgt, ap->a_vpp);
	return r;
}

static int mockfs_getattr(struct vnop_getattr_args *ap)
{
	/* offsetof(vnode_attr, ...) from xnu-12377 arm64. Only va_supported is
	 * ours to set; va_active is the caller's request mask. */
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	char *vap = (char *)ap->a_vap;
	uint64_t *supported = (uint64_t *)(vap + 0);
	*(uint64_t *)(vap + 24) = 1;            *supported |= VA_nlink;
	*(uint64_t *)(vap + 32) = fsn->size;    *supported |= VA_total_size;
	*(uint64_t *)(vap + 40) = fsn->size;    *supported |= VA_total_alloc;
	*(uint64_t *)(vap + 48) = fsn->size;    *supported |= VA_data_size;
	*(uint64_t *)(vap + 56) = fsn->size;    *supported |= VA_data_alloc;
	*(uint32_t *)(vap + 68) = 0;            *supported |= (1ULL << 7);  /* va_uid root */
	*(uint32_t *)(vap + 72) = 0;            *supported |= (1ULL << 8);  /* va_gid wheel */
	*(uint16_t *)(vap + 76) = (fsn->type == MOCKFS_FILE) ? 0755 : 0755;
	*supported |= VA_mode;
	*(uint64_t *)(vap + 176) = fsn->type + 2; *supported |= VA_fileid;
	return 0;
}

static int mockfs_read(struct vnop_read_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	if (vnode_vtype(ap->a_vp) != VREG) return ENOTSUP;
	return cluster_read(ap->a_vp, ap->a_uio, (off_t)fsn->size, ap->a_ioflag);
}

static int mockfs_reclaim(struct vnop_reclaim_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	if (fsn) { fsn->vp = 0; vnode_clearfsnode(ap->a_vp); }
	return 0;
}

static int mockfs_strategy(struct vnop_strategy_args *ap)
{
	vnode_t dvp = vfs_devvp(vnode_mount(buf_vnode(ap->a_bp)));
	int r;
	if (!dvp) return EIO;
	r = buf_strategy(dvp, ap);
	vnode_put(dvp);
	return r;
}

static int mockfs_pagein(struct vnop_pagein_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	return cluster_pagein(ap->a_vp, ap->a_pl, ap->a_pl_offset, ap->a_f_offset,
	                      ap->a_size, (off_t)fsn->size, ap->a_flags);
}

static int mockfs_blockmap(struct vnop_blockmap_args *ap)
{
	struct fsnode *fsn = vnode_fsnode(ap->a_vp);
	uint32_t blk = vfs_devblocksize(fsn->mnt);
	if (!blk) blk = 512;
	if (vnode_vtype(ap->a_vp) != VREG) return ENOTSUP;
	*ap->a_bpn = ap->a_foffset / blk;
	if (ap->a_run) {
		uint64_t remain = (fsn->size > (uint64_t)ap->a_foffset)
		                  ? fsn->size - (uint64_t)ap->a_foffset : 0;
		*ap->a_run = remain < ap->a_size ? (size_t)remain : ap->a_size;
	}
	if (ap->a_poff) *(int*)ap->a_poff = 0;
	return 0;
}

static const struct vnodeopv_entry_desc mockfs_entries[] = {
	{ &vnop_default_desc,  (int(*)(void*))vn_default_error },
	{ &vnop_lookup_desc,   (int(*)(void*))mockfs_lookup },
	{ &vnop_create_desc,   (int(*)(void*))err_create },
	{ &vnop_open_desc,     (int(*)(void*))nop_open },
	{ &vnop_mknod_desc,    (int(*)(void*))err_mknod },
	{ &vnop_close_desc,    (int(*)(void*))nop_close },
	{ &vnop_access_desc,   (int(*)(void*))err_access },
	{ &vnop_getattr_desc,  (int(*)(void*))mockfs_getattr },
	{ &vnop_setattr_desc,  (int(*)(void*))err_setattr },
	{ &vnop_read_desc,     (int(*)(void*))mockfs_read },
	{ &vnop_write_desc,    (int(*)(void*))err_write },
	{ &vnop_ioctl_desc,    (int(*)(void*))err_ioctl },
	{ &vnop_select_desc,   (int(*)(void*))err_select },
	{ &vnop_mmap_desc,     (int(*)(void*))err_mmap },
	{ &vnop_fsync_desc,    (int(*)(void*))nop_fsync },
	{ &vnop_remove_desc,   (int(*)(void*))err_remove },
	{ &vnop_link_desc,     (int(*)(void*))err_link },
	{ &vnop_rename_desc,   (int(*)(void*))err_rename },
	{ &vnop_mkdir_desc,    (int(*)(void*))err_mkdir },
	{ &vnop_rmdir_desc,    (int(*)(void*))err_rmdir },
	{ &vnop_symlink_desc,  (int(*)(void*))err_symlink },
	{ &vnop_readdir_desc,  (int(*)(void*))err_readdir },
	{ &vnop_readlink_desc, (int(*)(void*))err_readlink },
	{ &vnop_inactive_desc, (int(*)(void*))err_inactive },
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
		vfe.vfe_fsname[0]='m'; vfe.vfe_fsname[1]='o'; vfe.vfe_fsname[2]='c';
		vfe.vfe_fsname[3]='k'; vfe.vfe_fsname[4]='f'; vfe.vfe_fsname[5]='s';
		vfe.vfe_flags = VFS_TBLTHREADSAFE | VFS_TBLNOTYPENUM | VFS_TBL64BITREADY | VFS_TBLCANMOUNTROOT;
		if (vfs_fsadd(&vfe, MOCKFS_VFSHANDLE))
			IOLog("LAPIS mockfs: vfs_fsadd FAILED\n");
		else {
			IOLog("LAPIS mockfs: registered\n");
			*MOCKFS_REGISTERED = 1;
		}
	}
	return _vfs_mountroot();
}
