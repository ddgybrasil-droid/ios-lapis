/* lapis-ramfs: writable in-kernel RAM filesystem, registered as "tmpfs" so the
 * stock /sbin/mount_tmpfs (fstab `ramdisk /private/var tmpfs rw ...`) works.
 * See DESIGN.md. Freestanding blob (same model as lapis-mockfs): no writable
 * data sections; all state lives in heap allocations hung off the mount.
 * File data: array of 16 KiB pages; read/write/pagein/pageout go through
 * cluster_* + blockmap/strategy over memory, so mmap and write(2) share UBC. */
#include <stdint.h>
#include <stddef.h>

typedef void *vnode_t, *mount_t, *vfs_context_t, *buf_t, *upl_t, *uio_t;
typedef int64_t off_t;
typedef int64_t daddr64_t;

enum { VNON=0, VREG=1, VDIR=2, VLNK=5 };
enum { LOOKUP=0, CREATE=1, DELETE=2, RENAME=3 };
#define ISLASTCN 0x8000
#define VNFS_NOCACHE 1
#define VNFS_CANTCACHE 2
#define VFS_TBLTHREADSAFE 1
#define VFS_TBLNOTYPENUM 8
#define VFS_TBLLOCALVOL 0x10
#define VFS_TBL64BITREADY 0x20
#define VFS_TBLGENERICMNTARGS 0x200   /* no LOCALARGS devpath copyin: tmpfs args are not a device path */
#define MFSNAMELEN 15
#define ENOENT 2
#define EIO 5
#define ENOMEM 12
#define EEXIST 17
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOSPC 28
#define ENAMETOOLONG 63
#define ENOTEMPTY 66
#define ENOTSUP 45
#define EJUSTRETURN (-2)
#define B_READ 0x1
#define IO_APPEND 0x2
#define PG 0x4000ull
#define NAMEMAX 255

#define VA_rdev (1ULL<<0)
#define VA_nlink (1ULL<<1)
#define VA_total_size (1ULL<<2)
#define VA_total_alloc (1ULL<<3)
#define VA_data_size (1ULL<<4)
#define VA_data_alloc (1ULL<<5)
#define VA_iosize (1ULL<<6)
#define VA_uid (1ULL<<7)
#define VA_gid (1ULL<<8)
#define VA_mode (1ULL<<9)
#define VA_flags (1ULL<<10)
#define VA_create_time (1ULL<<12)
#define VA_access_time (1ULL<<13)
#define VA_modify_time (1ULL<<14)
#define VA_change_time (1ULL<<15)
#define VA_fileid (1ULL<<17)
#define VA_parentid (1ULL<<19)
/* struct vnode_attr offsets (xnu-12377 arm64) */
#define VAO_supported 0
#define VAO_active 8
#define VAO_nlink 24
#define VAO_total_size 32
#define VAO_total_alloc 40
#define VAO_data_size 48
#define VAO_data_alloc 56
#define VAO_iosize 64
#define VAO_uid 68
#define VAO_gid 72
#define VAO_mode 76
#define VAO_flags 80
#define VAO_create 96
#define VAO_access 112
#define VAO_modify 128
#define VAO_change 144
#define VAO_fileid 176
#define VAO_parentid 192
#define F(vap,off,T) (*(T *)((char *)(vap) + (off)))

struct timespec64 { int64_t s, ns; };
struct componentname {
	uint32_t cn_nameiop, cn_flags;
	void *cn_r1, *cn_r2;
	char *cn_pnbuf; int cn_pnlen;
	char *cn_nameptr; int cn_namelen;
	uint32_t cn_hash, cn_consume;
};
struct vnode_fsparam {
	mount_t vnfs_mp; int vnfs_vtype; const char *vnfs_str; vnode_t vnfs_dvp;
	void *vnfs_fsnode; int (**vnfs_vops)(void *); int vnfs_markroot, vnfs_marksystem;
	int32_t vnfs_rdev; off_t vnfs_filesize; struct componentname *vnfs_cnp; uint32_t vnfs_flags;
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
	struct vfsops *vfe_vfsops; int vfe_vopcnt; struct vnodeopv_desc **vfe_opvdescs;
	int vfe_fstypenum; char vfe_fsname[MFSNAMELEN]; uint32_t vfe_flags; void *vfe_reserv[2];
};
struct vnodeopv_entry_desc { void *opve_op; int (*opve_impl)(void *); };
struct vnodeopv_desc { int (***opv_desc_vector_p)(void *); const struct vnodeopv_entry_desc *opv_desc_ops; };

struct a_lookup { void *d; vnode_t dvp; vnode_t *vpp; struct componentname *cnp; vfs_context_t ctx; };
struct a_create { void *d; vnode_t dvp; vnode_t *vpp; struct componentname *cnp; void *vap; vfs_context_t ctx; };
struct a_symlink { void *d; vnode_t dvp; vnode_t *vpp; struct componentname *cnp; void *vap; char *target; vfs_context_t ctx; };
struct a_remove { void *d; vnode_t dvp; vnode_t vp; struct componentname *cnp; int flags; vfs_context_t ctx; };
struct a_rmdir { void *d; vnode_t dvp; vnode_t vp; struct componentname *cnp; vfs_context_t ctx; };
struct a_rename { void *d; vnode_t fdvp, fvp; struct componentname *fcnp; vnode_t tdvp, tvp; struct componentname *tcnp; vfs_context_t ctx; };
struct a_attr { void *d; vnode_t vp; void *vap; vfs_context_t ctx; };
struct a_rw { void *d; vnode_t vp; uio_t uio; int ioflag; vfs_context_t ctx; };
struct a_readdir { void *d; vnode_t vp; uio_t uio; int flags; int *eofflag; int *numdirent; vfs_context_t ctx; };
struct a_readlink { void *d; vnode_t vp; uio_t uio; vfs_context_t ctx; };
struct a_vp { void *d; vnode_t vp; vfs_context_t ctx; };
struct a_page { void *d; vnode_t vp; upl_t pl; uint64_t pl_offset; off_t f_offset; size_t size; int flags; vfs_context_t ctx; };
struct a_strategy { void *d; buf_t bp; };
struct a_blockmap { void *d; vnode_t vp; off_t foffset; size_t size; daddr64_t *bpn; size_t *run; void *poff; int flags; vfs_context_t ctx; };
struct a_pathconf { void *d; vnode_t vp; int name; int32_t *retval; vfs_context_t ctx; };

/* kernel imports (--defsym) */
extern void *IOMallocZero(size_t);
extern void IOFree(void *, size_t);
extern int vnode_create(uint32_t, size_t, void *, vnode_t *);
extern int vnode_getwithvid(vnode_t, uint32_t);
extern uint32_t vnode_vid(vnode_t);
extern void *vnode_fsnode(vnode_t);
extern void vnode_clearfsnode(vnode_t);
extern int vnode_vtype(vnode_t);
extern mount_t vnode_mount(vnode_t);
extern int vnode_recycle(vnode_t);
extern void *vfs_fsprivate(mount_t);
extern void vfs_setfsprivate(mount_t, void *);
extern int vfs_fsadd(struct vfs_fsentry *, void **);
extern int vflush(mount_t, vnode_t, int);
extern int cluster_read(vnode_t, uio_t, off_t, int);
extern int cluster_write(vnode_t, uio_t, off_t, off_t, off_t, off_t, int);
extern int cluster_pagein(vnode_t, upl_t, uint64_t, off_t, size_t, off_t, int);
extern int cluster_pageout(vnode_t, upl_t, uint64_t, off_t, size_t, off_t, int);
extern int ubc_setsize(vnode_t, off_t);
extern vnode_t buf_vnode(buf_t);
extern daddr64_t buf_blkno(buf_t);
extern uint32_t buf_count(buf_t), buf_flags(buf_t);
extern int buf_map(buf_t, char **), buf_unmap(buf_t);
extern void buf_setresid(buf_t, uint32_t), buf_biodone(buf_t), buf_seterror(buf_t, int);
extern int uiomove(const char *, int, uio_t);
extern off_t uio_offset(uio_t);
extern int64_t uio_resid(uio_t);
extern void uio_setoffset(uio_t, off_t);
extern void *vfs_context_ucred(vfs_context_t);
extern uint32_t kauth_cred_getuid(void *), kauth_cred_getgid(void *);
extern void nanotime(struct timespec64 *);
extern void *lck_grp_alloc_init(const char *, void *);
extern void *lck_mtx_alloc_init(void *, void *);
extern void lck_mtx_lock(void *), lck_mtx_unlock(void *);
extern void *memcpy(void *, const void *, size_t), *memset(void *, int, size_t);
extern int IOLog(const char *, ...);
extern int vn_default_error(void *);
extern int nop_open(void *), nop_close(void *), nop_fsync(void *);
extern int err_mknod(void *), err_ioctl(void *), err_select(void *), err_link(void *);
extern int err_advlock(void *), err_bwrite(void *), err_copyfile(void *);
extern int err_blktooff(void *), err_offtoblk(void *);
extern int lapis_mountroot_wrap_target(void);   /* lapisfs register+mountroot */
extern char vnop_default_desc, vnop_lookup_desc, vnop_create_desc, vnop_open_desc;
extern char vnop_mknod_desc, vnop_close_desc, vnop_access_desc, vnop_getattr_desc;
extern char vnop_setattr_desc, vnop_read_desc, vnop_write_desc, vnop_ioctl_desc;
extern char vnop_select_desc, vnop_mmap_desc, vnop_fsync_desc, vnop_remove_desc;
extern char vnop_link_desc, vnop_rename_desc, vnop_mkdir_desc, vnop_rmdir_desc;
extern char vnop_symlink_desc, vnop_readdir_desc, vnop_readlink_desc, vnop_inactive_desc;
extern char vnop_reclaim_desc, vnop_strategy_desc, vnop_pathconf_desc, vnop_advlock_desc;
extern char vnop_bwrite_desc, vnop_pagein_desc, vnop_pageout_desc, vnop_copyfile_desc;
extern char vnop_blktooff_desc, vnop_offtoblk_desc, vnop_blockmap_desc;

/* writable 8-byte slot (DEVELOPMENT ktest counters, unused at runtime) */
#define RAMFS_VNODEOP_P ((int (***)(void *))0xfffffe0007d260b8)

extern int ramfs_log(const char *fmt, uint64_t a, uint64_t b, uint64_t c);
__asm__("	.text\n	.global ramfs_log\n	.type ramfs_log,%function\nramfs_log:\n"
"	stp x29, x30, [sp, #-16]!\n	mov x29, sp\n	sub sp, sp, #32\n	stp x1, x2, [sp]\n	str x3, [sp, #16]\n"
"	bl IOLog\n	add sp, sp, #32\n	ldp x29, x30, [sp], #16\n	ret\n");

struct rnode {
	struct rnode *parent, *next, *child;
	vnode_t vp;
	uint32_t vid;
	uint8_t type, dead;
	uint16_t mode, nlen;
	uint32_t uid, gid, flags;
	uint64_t nlink, fileid, size;
	char *name;
	char **pages;      /* VREG: page array; VLNK: pages[0] = target */
	uint64_t npages;
	struct timespec64 t_birth, t_acc, t_mod, t_chg;
};
struct rmnt {
	void *mtx;
	struct rnode *root;
	uint64_t nextid, bytes, maxbytes;
	mount_t mp;
};
#define MNT(n_mp) ((struct rmnt *)vfs_fsprivate(n_mp))
#define VN(vp) ((struct rnode *)vnode_fsnode(vp))

static void lk(struct rmnt *m) { lck_mtx_lock(m->mtx); }
static void ulk(struct rmnt *m) { lck_mtx_unlock(m->mtx); }
static int veq(const char *a, const char *b, int n) { while (n--) if (*a++ != *b++) return 0; return 1; }
static void now(struct timespec64 *t) { nanotime(t); }

static struct rnode *nalloc(struct rmnt *m, int type, uint16_t mode, const char *nm, int nl)
{
	struct rnode *n = IOMallocZero(sizeof(*n));
	if (!n) return 0;
	if (nl) {
		n->name = IOMallocZero((size_t)nl + 1);
		if (!n->name) { IOFree(n, sizeof(*n)); return 0; }
		memcpy(n->name, nm, (size_t)nl);
	}
	n->nlen = (uint16_t)nl; n->type = (uint8_t)type; n->mode = mode & 07777;
	n->nlink = (type == VDIR) ? 2 : 1;
	n->fileid = ++m->nextid + 1;
	now(&n->t_birth); n->t_acc = n->t_mod = n->t_chg = n->t_birth;
	return n;
}
static void freedata(struct rmnt *m, struct rnode *n)
{
	uint64_t i;
	for (i = 0; i < n->npages; i++) if (n->pages[i]) { IOFree(n->pages[i], PG); m->bytes -= PG; }
	if (n->pages) IOFree(n->pages, n->npages * sizeof(char *));
	n->pages = 0; n->npages = 0;
}
static void nfree(struct rmnt *m, struct rnode *n)
{
	freedata(m, n);
	if (n->name) IOFree(n->name, (size_t)n->nlen + 1);
	IOFree(n, sizeof(*n));
}
/* grow page array to cover `size` bytes (pages allocated lazily in strategy) */
static int ensure(struct rmnt *m, struct rnode *n, uint64_t size)
{
	uint64_t need = (size + PG - 1) / PG, cap;
	char **np;
	if (need <= n->npages) return 0;
	cap = n->npages ? n->npages : 4;
	while (cap < need) cap *= 2;
	np = IOMallocZero(cap * sizeof(char *));
	if (!np) return ENOMEM;
	if (n->pages) { memcpy(np, n->pages, n->npages * sizeof(char *)); IOFree(n->pages, n->npages * sizeof(char *)); }
	n->pages = np; n->npages = cap;
	return 0;
}
static void unlink_child(struct rnode *d, struct rnode *c)
{
	struct rnode **pp = &d->child;
	while (*pp && *pp != c) pp = &(*pp)->next;
	if (*pp) *pp = c->next;
	c->next = 0; c->parent = 0;
}
static void link_child(struct rnode *d, struct rnode *c) { c->parent = d; c->next = d->child; d->child = c; }
static struct rnode *find(struct rnode *d, const char *nm, int nl)
{
	struct rnode *c;
	for (c = d->child; c; c = c->next) if (c->nlen == nl && veq(c->name, nm, nl)) return c;
	return 0;
}

static int node_vget(struct rmnt *m, struct rnode *n, vnode_t dvp, struct componentname *cnp, vnode_t *vpp)
{
	struct vnode_fsparam p;
	vnode_t vp;
	uint32_t vid;
	int err;
	for (;;) {
		lk(m);
		if (n->vp) {
			vp = n->vp; vid = n->vid; ulk(m);
			if (!vnode_getwithvid(vp, vid)) { *vpp = vp; return 0; }
			continue;   /* being reclaimed; retry */
		}
		ulk(m);
		break;
	}
	memset(&p, 0, sizeof(p));
	p.vnfs_mp = m->mp;
	p.vnfs_vtype = n->type;
	p.vnfs_str = "tmpfs";
	p.vnfs_dvp = (n == m->root) ? 0 : dvp;
	p.vnfs_fsnode = n;
	p.vnfs_vops = *RAMFS_VNODEOP_P;
	p.vnfs_markroot = (n == m->root);
	p.vnfs_filesize = (off_t)n->size;
	p.vnfs_cnp = 0;
	p.vnfs_flags = VNFS_NOCACHE | VNFS_CANTCACHE;
	err = vnode_create(0, sizeof(p), &p, &vp);
	if (err) return err;
	lk(m); n->vp = vp; n->vid = vnode_vid(vp); ulk(m);
	*vpp = vp;
	return 0;
}

/* ---------------- vfsops ---------------- */
static int r_mount(mount_t mp, vnode_t devvp, uint64_t data, vfs_context_t ctx)
{
	struct rmnt *m;
	void *grp;
	if (vfs_fsprivate(mp)) return 0;              /* MNT_UPDATE: nothing to change */
	m = IOMallocZero(sizeof(*m));
	if (!m) return ENOMEM;
	grp = lck_grp_alloc_init("lapisramfs", 0);
	m->mtx = lck_mtx_alloc_init(grp, 0);
	m->mp = mp;
	m->maxbytes = 512ull << 20;                   /* 512 MiB cap (TODO: from tmpfs args) */
	m->root = nalloc(m, VDIR, 0755, 0, 0);
	if (!m->mtx || !m->root) return ENOMEM;
	vfs_setfsprivate(mp, m);
	ramfs_log("LAPIS ramfs: mounted tmpfs mp=%llx max=%llx %llx\n", (uint64_t)mp, m->maxbytes, 0);
	return 0;
}
static void tree_free(struct rmnt *m, struct rnode *n)
{
	struct rnode *c, *nx;
	for (c = n->child; c; c = nx) { nx = c->next; tree_free(m, c); }
	nfree(m, n);
}
/* xnu dounmount flushes with SKIPROOT; the fs must release the rest (root vnode)
 * itself, otherwise mount_refdrain() waits forever ("mount_drain"). */
static int r_unmount(mount_t mp, int flags, vfs_context_t ctx)
{
	struct rmnt *m = MNT(mp);
	int err = vflush(mp, 0, (flags & 0x80000 /* MNT_FORCE */) ? 2 /* FORCECLOSE */ : 0);
	if (err) return err;
	if (m) {
		tree_free(m, m->root);
		vfs_setfsprivate(mp, 0);
		IOFree(m, sizeof(*m));   /* mutex/group intentionally leaked (tiny, bring-up) */
	}
	return 0;
}
static int r_root(mount_t mp, vnode_t *vpp, vfs_context_t ctx) { struct rmnt *m = MNT(mp); return node_vget(m, m->root, 0, 0, vpp); }
static int r_vfsgetattr(mount_t mp, void *fsap, vfs_context_t ctx) { return 0; }
static int r_sync(mount_t mp, int w, vfs_context_t ctx) { return 0; }
static int r_init(void *v) { return 0; }

/* ---------------- vnops ---------------- */
static int r_lookup(struct a_lookup *ap)
{
	struct rnode *d = VN(ap->dvp), *c;
	struct rmnt *m = MNT(vnode_mount(ap->dvp));
	struct componentname *cn = ap->cnp;
	int nl = cn->cn_namelen;
	if (d->type != VDIR) return ENOTDIR;
	if (nl == 1 && cn->cn_nameptr[0] == '.') return node_vget(m, d, ap->dvp, cn, ap->vpp);
	if (nl == 2 && cn->cn_nameptr[0] == '.' && cn->cn_nameptr[1] == '.')
		return node_vget(m, d->parent ? d->parent : d, ap->dvp, cn, ap->vpp);
	lk(m); c = find(d, cn->cn_nameptr, nl); ulk(m);
	if (c) return node_vget(m, c, ap->dvp, cn, ap->vpp);
	if ((cn->cn_flags & ISLASTCN) && (cn->cn_nameiop == CREATE || cn->cn_nameiop == RENAME)) return EJUSTRETURN;
	return ENOENT;
}
static void setnew(struct rnode *n, void *vap, vfs_context_t ctx)
{
	void *cr = vfs_context_ucred(ctx);
	uint64_t act = F(vap, VAO_active, uint64_t);
	n->uid = (act & VA_uid) ? F(vap, VAO_uid, uint32_t) : kauth_cred_getuid(cr);
	n->gid = (act & VA_gid) ? F(vap, VAO_gid, uint32_t) : (n->parent ? n->parent->gid : kauth_cred_getgid(cr));
	if (act & VA_mode) n->mode = F(vap, VAO_mode, uint16_t) & 07777;
	F(vap, VAO_supported, uint64_t) |= VA_mode | VA_uid | VA_gid | VA_flags;
}
static int mk(struct a_create *ap, int type, const char *target)
{
	struct rnode *d = VN(ap->dvp), *n;
	struct rmnt *m = MNT(vnode_mount(ap->dvp));
	struct componentname *cn = ap->cnp;
	uint16_t mode = (ap->vap && (F(ap->vap, VAO_active, uint64_t) & VA_mode)) ? F(ap->vap, VAO_mode, uint16_t) : 0755;
	int err;
	if (cn->cn_namelen > NAMEMAX) return ENAMETOOLONG;
	lk(m);
	if (find(d, cn->cn_nameptr, cn->cn_namelen)) { ulk(m); return EEXIST; }
	n = nalloc(m, type, mode, cn->cn_nameptr, cn->cn_namelen);
	if (!n) { ulk(m); return ENOMEM; }
	if (type == VLNK) {
		uint64_t l = 0;
		while (target[l]) l++;
		if (ensure(m, n, l + 1) || !(n->pages[0] = IOMallocZero(PG)) || l >= PG) { ulk(m); nfree(m, n); return l >= PG ? ENAMETOOLONG : ENOMEM; }
		m->bytes += PG; memcpy(n->pages[0], target, l); n->size = l;
	}
	link_child(d, n);
	if (type == VDIR) d->nlink++;
	now(&d->t_mod); d->t_chg = d->t_mod;
	if (ap->vap) setnew(n, ap->vap, ap->ctx);
	ulk(m);
	err = node_vget(m, n, ap->dvp, cn, ap->vpp);
	return err;
}
static int r_create(struct a_create *ap) { return mk(ap, VREG, 0); }
static int r_mkdir(struct a_create *ap) { return mk(ap, VDIR, 0); }
static int r_symlink(struct a_symlink *ap)
{
	struct a_create c = { ap->d, ap->dvp, ap->vpp, ap->cnp, ap->vap, ap->ctx };
	return mk(&c, VLNK, ap->target);
}
static void drop(struct rmnt *m, struct rnode *d, struct rnode *n, vnode_t vp)
{
	unlink_child(d, n);
	if (n->type == VDIR) { d->nlink--; n->nlink = 0; } else if (n->nlink) n->nlink--;
	now(&d->t_mod); d->t_chg = d->t_mod;
	if (!n->nlink) n->dead = 1;
	if (n->dead && !n->vp) nfree(m, n);   /* no vnode: free now; else on reclaim */
	(void)vp;
}
static int r_remove(struct a_remove *ap)
{
	struct rnode *d = VN(ap->dvp), *n = VN(ap->vp);
	struct rmnt *m = MNT(vnode_mount(ap->dvp));
	if (n->type == VDIR) return EISDIR;
	lk(m); drop(m, d, n, ap->vp); ulk(m);
	vnode_recycle(ap->vp);
	return 0;
}
static int r_rmdir(struct a_rmdir *ap)
{
	struct rnode *d = VN(ap->dvp), *n = VN(ap->vp);
	struct rmnt *m = MNT(vnode_mount(ap->dvp));
	if (n->type != VDIR) return ENOTDIR;
	lk(m);
	if (n->child) { ulk(m); return ENOTEMPTY; }
	drop(m, d, n, ap->vp); ulk(m);
	vnode_recycle(ap->vp);
	return 0;
}
static int r_rename(struct a_rename *ap)
{
	struct rnode *fd = VN(ap->fdvp), *f = VN(ap->fvp), *td = VN(ap->tdvp), *t = ap->tvp ? VN(ap->tvp) : 0, *p;
	struct rmnt *m = MNT(vnode_mount(ap->fdvp));
	struct componentname *tc = ap->tcnp;
	char *nn;
	if (tc->cn_namelen > NAMEMAX) return ENAMETOOLONG;
	if (t == f) return 0;
	lk(m);
	for (p = td; p; p = p->parent) if (p == f) { ulk(m); return EINVAL; }   /* into own subtree */
	if (t) {
		if (t->type == VDIR && (f->type != VDIR || t->child)) { ulk(m); return t->child ? ENOTEMPTY : EISDIR; }
		if (t->type != VDIR && f->type == VDIR) { ulk(m); return ENOTDIR; }
		drop(m, td, t, ap->tvp);
	}
	nn = IOMallocZero((size_t)tc->cn_namelen + 1);
	if (!nn) { ulk(m); return ENOMEM; }
	memcpy(nn, tc->cn_nameptr, (size_t)tc->cn_namelen);
	unlink_child(fd, f);
	if (f->type == VDIR) { fd->nlink--; td->nlink++; }
	if (f->name) IOFree(f->name, (size_t)f->nlen + 1);
	f->name = nn; f->nlen = (uint16_t)tc->cn_namelen;
	link_child(td, f);
	now(&f->t_chg); fd->t_mod = fd->t_chg = td->t_mod = td->t_chg = f->t_chg;
	ulk(m);
	if (ap->tvp) vnode_recycle(ap->tvp);
	return 0;
}
static int r_getattr(struct a_attr *ap)
{
	struct rnode *n = VN(ap->vp);
	void *v = ap->vap;
	uint64_t alloc = (n->size + PG - 1) & ~(PG - 1);
	F(v, VAO_nlink, uint64_t) = n->nlink;
	F(v, VAO_total_size, uint64_t) = n->size; F(v, VAO_data_size, uint64_t) = n->size;
	F(v, VAO_total_alloc, uint64_t) = alloc;   F(v, VAO_data_alloc, uint64_t) = alloc;
	F(v, VAO_iosize, uint32_t) = (uint32_t)PG;
	F(v, VAO_uid, uint32_t) = n->uid; F(v, VAO_gid, uint32_t) = n->gid;
	F(v, VAO_mode, uint16_t) = n->mode; F(v, VAO_flags, uint32_t) = n->flags;
	F(v, VAO_create, struct timespec64) = n->t_birth; F(v, VAO_access, struct timespec64) = n->t_acc;
	F(v, VAO_modify, struct timespec64) = n->t_mod;   F(v, VAO_change, struct timespec64) = n->t_chg;
	F(v, VAO_fileid, uint64_t) = n->fileid;
	F(v, VAO_parentid, uint64_t) = n->parent ? n->parent->fileid : n->fileid;
	F(v, VAO_supported, uint64_t) |= VA_nlink | VA_total_size | VA_data_size | VA_total_alloc | VA_data_alloc |
		VA_iosize | VA_uid | VA_gid | VA_mode | VA_flags | VA_create_time | VA_access_time |
		VA_modify_time | VA_change_time | VA_fileid | VA_parentid;
	return 0;
}
static int truncate(struct rmnt *m, struct rnode *n, vnode_t vp, uint64_t len)
{
	uint64_t i, keep;
	if (n->type != VREG) return EISDIR;
	if (len > n->size) {
		if (m->bytes + (len - n->size) > m->maxbytes) return ENOSPC;
		if (ensure(m, n, len)) return ENOMEM;
	} else {
		keep = (len + PG - 1) / PG;
		for (i = keep; i < n->npages; i++) if (n->pages[i]) { IOFree(n->pages[i], PG); n->pages[i] = 0; m->bytes -= PG; }
		if (len % PG && n->pages[len / PG]) memset(n->pages[len / PG] + len % PG, 0, PG - len % PG);
	}
	n->size = len;
	return 0;
}
static int r_setattr(struct a_attr *ap)
{
	struct rnode *n = VN(ap->vp);
	struct rmnt *m = MNT(vnode_mount(ap->vp));
	void *v = ap->vap;
	uint64_t act = F(v, VAO_active, uint64_t), sup = 0;
	int err = 0;
	lk(m);
	if (act & VA_data_size) { err = truncate(m, n, ap->vp, F(v, VAO_data_size, uint64_t)); sup |= VA_data_size; now(&n->t_mod); }
	if (act & VA_mode)  { n->mode = F(v, VAO_mode, uint16_t) & 07777; sup |= VA_mode; }
	if (act & VA_uid)   { n->uid = F(v, VAO_uid, uint32_t); sup |= VA_uid; }
	if (act & VA_gid)   { n->gid = F(v, VAO_gid, uint32_t); sup |= VA_gid; }
	if (act & VA_flags) { n->flags = F(v, VAO_flags, uint32_t); sup |= VA_flags; }
	if (act & VA_access_time) { n->t_acc = F(v, VAO_access, struct timespec64); sup |= VA_access_time; }
	if (act & VA_modify_time) { n->t_mod = F(v, VAO_modify, struct timespec64); sup |= VA_modify_time; }
	if (act & VA_create_time) { n->t_birth = F(v, VAO_create, struct timespec64); sup |= VA_create_time; }
	if (act & VA_change_time) sup |= VA_change_time;
	now(&n->t_chg);
	ulk(m);
	if (!err && (act & VA_data_size)) ubc_setsize(ap->vp, (off_t)n->size);
	F(v, VAO_supported, uint64_t) |= sup;
	return err;
}
static int r_read(struct a_rw *ap)
{
	struct rnode *n = VN(ap->vp);
	if (n->type == VDIR) return EISDIR;
	if (n->type != VREG) return EINVAL;
	return cluster_read(ap->vp, ap->uio, (off_t)n->size, ap->ioflag);
}
static int r_write(struct a_rw *ap)
{
	struct rnode *n = VN(ap->vp);
	struct rmnt *m = MNT(vnode_mount(ap->vp));
	off_t osize, nsize, off;
	int err;
	if (n->type != VREG) return EISDIR;
	lk(m);
	osize = (off_t)n->size;
	if (ap->ioflag & IO_APPEND) uio_setoffset(ap->uio, osize);
	off = uio_offset(ap->uio);
	nsize = off + uio_resid(ap->uio);
	if (nsize > osize) {
		if (m->bytes + (uint64_t)(nsize - osize) > m->maxbytes) { ulk(m); return ENOSPC; }
		if (ensure(m, n, (uint64_t)nsize)) { ulk(m); return ENOMEM; }
	} else nsize = osize;
	ulk(m);
	err = cluster_write(ap->vp, ap->uio, osize, nsize, 0, 0, ap->ioflag);
	if (!err && nsize > osize) { lk(m); if ((uint64_t)nsize > n->size) n->size = (uint64_t)nsize; ulk(m); ubc_setsize(ap->vp, nsize); }
	now(&n->t_mod); n->t_chg = n->t_mod;
	return err;
}
/* data path: block numbers are file offsets / 512 (per-vnode, memory "device") */
static int r_blockmap(struct a_blockmap *ap)
{
	struct rnode *n = VN(ap->vp);
	if (n->type != VREG) return ENOTSUP;
	*ap->bpn = ap->foffset / 512;
	if (ap->run) *ap->run = ap->size;
	if (ap->poff) *(int *)ap->poff = 0;
	return 0;
}
static int r_strategy(struct a_strategy *ap)
{
	buf_t bp = ap->bp;
	vnode_t vp = buf_vnode(bp);
	struct rnode *n = VN(vp);
	struct rmnt *m = MNT(vnode_mount(vp));
	uint64_t off = (uint64_t)buf_blkno(bp) * 512, cnt = buf_count(bp), done = 0, pg, po, k;
	int rd = buf_flags(bp) & B_READ, err = 0;
	char *va;
	if (buf_map(bp, &va)) { buf_seterror(bp, EIO); buf_biodone(bp); return EIO; }
	lk(m);
	if (!rd && ensure(m, n, off + cnt)) err = ENOMEM;
	while (!err && done < cnt) {
		pg = (off + done) / PG; po = (off + done) % PG;
		k = PG - po; if (k > cnt - done) k = cnt - done;
		if (rd) {
			if (pg < n->npages && n->pages[pg]) memcpy(va + done, n->pages[pg] + po, k);
			else memset(va + done, 0, k);
		} else {
			if (!n->pages[pg]) {
				if (m->bytes + PG > m->maxbytes + PG * 64) { err = ENOSPC; break; }
				if (!(n->pages[pg] = IOMallocZero(PG))) { err = ENOMEM; break; }
				m->bytes += PG;
			}
			memcpy(n->pages[pg] + po, va + done, k);
		}
		done += k;
	}
	ulk(m);
	buf_unmap(bp);
	if (err) buf_seterror(bp, err);
	buf_setresid(bp, (uint32_t)(cnt - done));
	buf_biodone(bp);
	return err;
}
static int r_pagein(struct a_page *ap)
{
	struct rnode *n = VN(ap->vp);
	return cluster_pagein(ap->vp, ap->pl, ap->pl_offset, ap->f_offset, ap->size, (off_t)n->size, ap->flags);
}
static int r_pageout(struct a_page *ap)
{
	struct rnode *n = VN(ap->vp);
	return cluster_pageout(ap->vp, ap->pl, ap->pl_offset, ap->f_offset, ap->size, (off_t)n->size, ap->flags);
}
static int r_readlink(struct a_readlink *ap)
{
	struct rnode *n = VN(ap->vp);
	if (n->type != VLNK) return EINVAL;
	return uiomove(n->pages[0], (int)n->size, ap->uio);
}
/* legacy struct dirent (vnode_readdir64 converts to direntry for getdirentries64) */
struct dirent32 { uint32_t d_ino; uint16_t d_reclen; uint8_t d_type, d_namlen; char d_name[NAMEMAX + 1]; };
static uint8_t dtype(int t) { return t == VDIR ? 4 : t == VLNK ? 10 : 8; }
static int r_readdir(struct a_readdir *ap)
{
	struct rnode *d = VN(ap->vp), *c;
	struct rmnt *m = MNT(vnode_mount(ap->vp));
	struct dirent32 de;
	off_t idx = uio_offset(ap->uio), i = 0;
	int err = 0, cnt = 0, eof = 0, reclen, nl;
	const char *nm;
	uint64_t ino;
	uint8_t ty;
	if (d->type != VDIR) return ENOTDIR;
	if (ap->flags & 0x0001 /* VNODE_READDIR_EXTENDED */) return EINVAL;
	lk(m);
	c = d->child;
	for (;; i++) {
		if (i == 0) { nm = "."; nl = 1; ino = d->fileid; ty = 4; }
		else if (i == 1) { nm = ".."; nl = 2; ino = d->parent ? d->parent->fileid : d->fileid; ty = 4; }
		else { if (!c) { eof = 1; break; } nm = c->name; nl = c->nlen; ino = c->fileid; ty = dtype(c->type); c = c->next; }
		if (i < idx) continue;
		reclen = (8 + nl + 1 + 3) & ~3;
		if (uio_resid(ap->uio) < reclen) break;
		memset(&de, 0, sizeof(de));
		de.d_ino = (uint32_t)ino; de.d_reclen = (uint16_t)reclen; de.d_type = ty; de.d_namlen = (uint8_t)nl;
		memcpy(de.d_name, nm, (size_t)nl);
		err = uiomove((const char *)&de, reclen, ap->uio);   /* may fault: user buffer, lock is a mutex */
		if (err) break;
		cnt++;
	}
	ulk(m);
	uio_setoffset(ap->uio, i);
	if (ap->eofflag) *ap->eofflag = eof;
	if (ap->numdirent) *ap->numdirent = cnt;
	return err;
}

/* ---- lapisfs readdir (the kernel's lapisfs blob has err_readdir; its vnodeop table
 * slot is pointed here by tools/patch_kernel_ramfs.py). Layout verified against the
 * kernel's own lapisfs lookup (0xfffffe000743d2d8): fsnode{u32 idx; u8 type; mount mnt@8;
 * vnode vp@0x10; ent *e@0x18}, mnt_data{base; u32 nent@8; ents@0x10; strs@0x18},
 * ent(32B){u32 parent; u16 type, mode; u64 data_off, size; u32 name_off, name_len}. ---- */
struct lfs_ent { uint32_t parent; uint16_t type, mode; uint64_t data_off, size; uint32_t name_off, name_len; };
struct lfs_node { uint32_t idx; uint8_t type; mount_t mnt; vnode_t vp; const struct lfs_ent *e; };
struct lfs_mnt { const uint8_t *base; uint32_t nent; const struct lfs_ent *ents; const char *strs; };
/* lapisfs symlinks: a regular (type 2) entry whose mode carries S_IFLNK; data extent = target. */
#define LFS_ISLNK(e) ((e)->type == 2 && ((e)->mode & 0170000) == 0120000)
/* kernel lapisfs device reader: fills dst[off..end) from the md root (lapisfs @0x743d464) */
extern int lapisfs_devread(uint8_t *dst, uint64_t off, uint64_t end);
/* vtype for lapisfs vnode_create (called from lapisfs vget @0x743d268, x19 = lfs_node).
   Must preserve x1-x3 (already set up by the caller): uses only x0, x16, x17. */
__asm__(".text\n.globl lapisfs_vtype\nlapisfs_vtype:\n"
	"ldr x16, [x19, #0x18]\n ldrh w0, [x16, #4]\n cmp w0, #2\n b.eq 1f\n mov w0, #2\n ret\n"
	"1: ldrh w17, [x16, #6]\n and w17, w17, #0xf000\n cmp w17, #0xa000\n mov w0, #1\n b.ne 2f\n mov w0, #5\n2: ret\n");
int lapisfs_readlink(struct a_readlink *ap)
{
	struct lfs_node *n = vnode_fsnode(ap->vp);
	const struct lfs_ent *e;
	char buf[1024];
	if (!n || !(e = n->e) || !LFS_ISLNK(e)) return EINVAL;
	if (e->size == 0 || e->size > sizeof(buf)) return EIO;
	if (lapisfs_devread((uint8_t *)buf - e->data_off, e->data_off, e->data_off + e->size)) return EIO;
	return uiomove(buf, (int)e->size, ap->uio);
}
static int lfs_emit(uio_t uio, uint64_t ino, uint8_t ty, const char *nm, int nl)
{
	struct dirent32 de;
	int reclen = (8 + nl + 1 + 3) & ~3;
	if (nl > NAMEMAX) return 0;   /* skip */
	if (uio_resid(uio) < reclen) return -1;
	memset(&de, 0, sizeof(de));
	de.d_ino = (uint32_t)ino; de.d_reclen = (uint16_t)reclen; de.d_type = ty; de.d_namlen = (uint8_t)nl;
	memcpy(de.d_name, nm, (size_t)nl);
	return uiomove((const char *)&de, reclen, uio) ? -2 : 1;
}
int lapisfs_readdir(struct a_readdir *ap)
{
	struct lfs_node *d = vnode_fsnode(ap->vp);
	struct lfs_mnt *md = vfs_fsprivate(d->mnt);
	off_t off = uio_offset(ap->uio);
	uint32_t i;
	int r = 0, cnt = 0, eof = 0;
	if (d->type != 1) return ENOTDIR;
	if (ap->flags & 0x0001) return EINVAL;
	if (off == 0) { if ((r = lfs_emit(ap->uio, d->idx + 2, 4, ".", 1)) < 0) goto out; cnt++; off = 1; }
	if (off == 1) { if ((r = lfs_emit(ap->uio, d->e->parent + 2, 4, "..", 2)) < 0) goto out; cnt++; off = 2; }
	for (i = (uint32_t)(off - 2) > 0 ? (uint32_t)(off - 2) : 1; i < md->nent; i++) {
		const struct lfs_ent *e = &md->ents[i];
		if (e->parent != d->idx || i == d->idx) continue;
		r = lfs_emit(ap->uio, i + 2, e->type == 1 ? 4 : LFS_ISLNK(e) ? 10 : 8, md->strs + e->name_off, (int)e->name_len);
		if (r < 0) { off = i + 2; goto out; }
		if (r > 0) cnt++;
	}
	off = (off_t)md->nent + 2; eof = 1; r = 0;
out:
	uio_setoffset(ap->uio, off);
	if (ap->eofflag) *ap->eofflag = eof;
	if (ap->numdirent) *ap->numdirent = cnt;
	return (r == -2) ? EIO : 0;
}

/* ---- lapisfs getattr with owners (replaces the kernel lapisfs getattr @0x743caf4, which
 * reported root:wheel for everything; slot pointed here by tools/patch_kernel_ramfs.py).
 * Backward-compatible LAPISFS v2 extension: optional owner table in the metadata prefix,
 * between the 40-byte header and the entry table (ent_off is a header field, so old kernels
 * and tools that honour it just skip it; the kernel lapisfs mount requires root entry
 * data_off/size == 0, so the table cannot hang off the root entry):
 *   0x28 "LFSOWN01"  0x30 u32 count  0x34 u32 0  0x38 u64 table_off (0x40)
 *   table_off: count x { u32 uid, u32 gid }, indexed by entry number; ent_off >= table end.
 * The prefix [0, str_off) is resident in md->base for both md and blk roots.
 * Entries >= count, or images without the extension, stay 0:0. Written by
 * tools/lapisfs_owners.py, preserved by tools/lapisfs_add.py. ---- */
static void lfs_owner(const struct lfs_mnt *md, uint32_t idx, uint32_t *uid, uint32_t *gid)
{
	static const char mg[8] = { 'L','F','S','O','W','N','0','1' };
	const uint8_t *b = md->base;
	uint64_t eo = *(const uint64_t *)(b + 16), cnt, to;
	int k;
	*uid = 0; *gid = 0;
	if (eo < 0x40) return;
	for (k = 0; k < 8; k++) if (b[0x28 + k] != (uint8_t)mg[k]) return;
	cnt = *(const uint32_t *)(b + 0x30); to = *(const uint64_t *)(b + 0x38);
	if (to < 0x40 || to + cnt * 8 > eo || idx >= cnt) return;
	*uid = *(const uint32_t *)(b + to + (uint64_t)idx * 8);
	*gid = *(const uint32_t *)(b + to + 4 + (uint64_t)idx * 8);
}
int lapisfs_getattr(struct a_attr *ap)
{
	struct lfs_node *n = vnode_fsnode(ap->vp);
	char *v = (char *)ap->vap;
	uint64_t sz = n->type == 1 ? 0 : n->e->size;
	uint32_t uid, gid;
	lfs_owner(vfs_fsprivate(n->mnt), n->idx, &uid, &gid);
	*(uint64_t *)(v + 24) = n->type == 1 ? 2 : 1;           /* va_nlink */
	*(uint64_t *)(v + 32) = sz; *(uint64_t *)(v + 40) = sz; /* total size/alloc */
	*(uint64_t *)(v + 48) = sz; *(uint64_t *)(v + 56) = sz; /* data size/alloc */
	F(v, VAO_uid, uint32_t) = uid; F(v, VAO_gid, uint32_t) = gid;
	*(uint16_t *)(v + 76) = n->e->mode & 07777;             /* va_mode */
	*(uint64_t *)(v + 176) = (uint64_t)n->idx + 2;          /* va_fileid */
	*(uint64_t *)v |= VA_nlink | VA_total_size | VA_total_alloc | VA_data_size | VA_data_alloc |
		VA_uid | VA_gid | VA_mode | VA_fileid;
	return 0;
}

static int r_inactive(struct a_vp *ap)
{
	struct rnode *n = VN(ap->vp);
	if (n && n->dead) vnode_recycle(ap->vp);
	return 0;
}
static int r_reclaim(struct a_vp *ap)
{
	struct rnode *n = VN(ap->vp);
	struct rmnt *m;
	if (!n) return 0;
	m = MNT(vnode_mount(ap->vp));
	lk(m);
	n->vp = 0;
	vnode_clearfsnode(ap->vp);
	if (n->dead) nfree(m, n);
	ulk(m);
	return 0;
}
static int r_pathconf(struct a_pathconf *ap)
{
	switch (ap->name) {
	case 1: *ap->retval = 32767; return 0;     /* _PC_LINK_MAX */
	case 4: *ap->retval = NAMEMAX; return 0;   /* _PC_NAME_MAX */
	case 5: *ap->retval = 1024; return 0;      /* _PC_PATH_MAX */
	case 6: *ap->retval = 512; return 0;       /* _PC_PIPE_BUF */
	case 7: *ap->retval = 200112; return 0;    /* _PC_CHOWN_RESTRICTED */
	case 8: *ap->retval = 200112; return 0;    /* _PC_NO_TRUNC */
	case 18: *ap->retval = 64; return 0;       /* _PC_FILESIZEBITS */
	case 11: *ap->retval = 1; return 0;        /* _PC_CASE_SENSITIVE */
	case 12: *ap->retval = 1; return 0;        /* _PC_CASE_PRESERVING */
	}
	return EINVAL;
}
static int r_zero(void *ap) { return 0; }

static const struct vnodeopv_entry_desc r_entries[] = {
	{ &vnop_default_desc,  (int(*)(void*))vn_default_error },
	{ &vnop_lookup_desc,   (int(*)(void*))r_lookup },
	{ &vnop_create_desc,   (int(*)(void*))r_create },
	{ &vnop_open_desc,     (int(*)(void*))nop_open },
	{ &vnop_mknod_desc,    (int(*)(void*))err_mknod },
	{ &vnop_close_desc,    (int(*)(void*))nop_close },
	{ &vnop_access_desc,   (int(*)(void*))r_zero },
	{ &vnop_getattr_desc,  (int(*)(void*))r_getattr },
	{ &vnop_setattr_desc,  (int(*)(void*))r_setattr },
	{ &vnop_read_desc,     (int(*)(void*))r_read },
	{ &vnop_write_desc,    (int(*)(void*))r_write },
	{ &vnop_ioctl_desc,    (int(*)(void*))err_ioctl },
	{ &vnop_select_desc,   (int(*)(void*))err_select },
	{ &vnop_mmap_desc,     (int(*)(void*))r_zero },
	{ &vnop_fsync_desc,    (int(*)(void*))nop_fsync },
	{ &vnop_remove_desc,   (int(*)(void*))r_remove },
	{ &vnop_link_desc,     (int(*)(void*))err_link },
	{ &vnop_rename_desc,   (int(*)(void*))r_rename },
	{ &vnop_mkdir_desc,    (int(*)(void*))r_mkdir },
	{ &vnop_rmdir_desc,    (int(*)(void*))r_rmdir },
	{ &vnop_symlink_desc,  (int(*)(void*))r_symlink },
	{ &vnop_readdir_desc,  (int(*)(void*))r_readdir },
	{ &vnop_readlink_desc, (int(*)(void*))r_readlink },
	{ &vnop_inactive_desc, (int(*)(void*))r_inactive },
	{ &vnop_reclaim_desc,  (int(*)(void*))r_reclaim },
	{ &vnop_strategy_desc, (int(*)(void*))r_strategy },
	{ &vnop_pathconf_desc, (int(*)(void*))r_pathconf },
	{ &vnop_advlock_desc,  (int(*)(void*))err_advlock },
	{ &vnop_bwrite_desc,   (int(*)(void*))err_bwrite },
	{ &vnop_pagein_desc,   (int(*)(void*))r_pagein },
	{ &vnop_pageout_desc,  (int(*)(void*))r_pageout },
	{ &vnop_copyfile_desc, (int(*)(void*))err_copyfile },
	{ &vnop_blktooff_desc, (int(*)(void*))err_blktooff },
	{ &vnop_offtoblk_desc, (int(*)(void*))err_offtoblk },
	{ &vnop_blockmap_desc, (int(*)(void*))r_blockmap },
	{ 0, 0 }
};
static const struct vnodeopv_desc r_opv_desc = { RAMFS_VNODEOP_P, r_entries };
static struct vnodeopv_desc *const r_opv_descs[] = { (struct vnodeopv_desc *)&r_opv_desc, 0 };
static const struct vfsops r_vfsops = {
	.vfs_mount = r_mount, .vfs_unmount = r_unmount, .vfs_root = r_root,
	.vfs_getattr = r_vfsgetattr, .vfs_sync = r_sync, .vfs_init = r_init,
};

/* bsd_init's mountroot call lands here: register "tmpfs", then lapisfs register+mountroot. */
extern void lapis_iomedia_register(void);
extern void lapis_sandbox_register(void);
int lapis_ramfs_wrap(void)
{
	struct vfs_fsentry vfe;
	void *h;
	int i;
	if (!*RAMFS_VNODEOP_P) {
		for (i = 0; i < (int)sizeof(vfe); i++) ((char *)&vfe)[i] = 0;
		vfe.vfe_vfsops = (struct vfsops *)&r_vfsops;
		vfe.vfe_vopcnt = 1;
		vfe.vfe_opvdescs = (struct vnodeopv_desc **)r_opv_descs;
		vfe.vfe_fsname[0]='t'; vfe.vfe_fsname[1]='m'; vfe.vfe_fsname[2]='p'; vfe.vfe_fsname[3]='f'; vfe.vfe_fsname[4]='s';
		vfe.vfe_flags = VFS_TBLTHREADSAFE | VFS_TBLNOTYPENUM | VFS_TBL64BITREADY | VFS_TBLLOCALVOL | VFS_TBLGENERICMNTARGS;
		if (vfs_fsadd(&vfe, &h)) IOLog("LAPIS ramfs: vfs_fsadd FAILED\n");
		else IOLog("LAPIS ramfs: registered tmpfs\n");
		lapis_iomedia_register();
		lapis_sandbox_register();
	}
	return lapis_mountroot_wrap_target();
}
