/*
 * LapisBlk - BSD block+char devices over LapisUFS / LapisSDHCI (no IOStorageFamily).
 *
 * Publishes /dev/diskN (whole) and /dev/diskNsM (GPT partitions), plus the raw
 * /dev/rdisk* char nodes.  Scans every registered disk for a LAPISFS1 image
 * (LBA 0 or a GPT partition named "lapis-root"/typed APFS-root) and exposes it
 * to lapisfs through the fixed-address lapis_blkops in drivers BSS.
 */
#include "../include/kern.h"
#include "../include/blk.h"

#define LOG(fmt, ...) klog("LapisBlk: " fmt "\n", ##__VA_ARGS__)
#define DEVFS_BLOCK 1
#define DEVFS_CHAR  0
#define D_DISK      0x1000
#define B_READ      0x00000001
#define B_ERROR     0x00000004
#define MAX_DISKS   4
#define MAX_PARTS   32
#define UNIT_SHIFT  6                 /* minor = (unit<<6) | part ; part0 = whole */

struct part {
	char     name[24];
	uint64_t first, last;         /* inclusive LBAs */
	uint8_t  type[16];
	int      used;
};

struct unit {
	struct lapis_disk *d;
	struct part parts[MAX_PARTS];
	int nparts;
	int major_b, major_c;
	char names[MAX_PARTS + 1][16]; /* preformatted, no '%' for devfs_make_node */
};

static struct unit *U;
static int n_units;
static volatile int pending;
static void *lock;
static int started;
extern int k_devfs_ready;   /* actually _devfs_ready via --defsym; int flag */

/* fixed VA: first bytes of the drivers BSS (see drv.ld .bss.blkops) */
struct lapis_blkops lapis_blkops __attribute__((section(".bss.blkops"), used));


/* Darwin bdevsw / cdevsw (xnu-12377, verified by sizeof in bdevsw_add = 0x38 / 0x70) */
struct bdevsw {
	int (*d_open)(uint32_t, int, int, void *);
	int (*d_close)(uint32_t, int, int, void *);
	void (*d_strategy)(void *);
	int (*d_ioctl)(uint32_t, unsigned long, void *, int, void *);
	int (*d_dump)(void);
	int (*d_psize)(uint32_t);
	int   d_type;
};
struct cdevsw {
	int (*d_open)(uint32_t, int, int, void *);
	int (*d_close)(uint32_t, int, int, void *);
	int (*d_read)(uint32_t, void *, int, void *);
	int (*d_write)(uint32_t, void *, int, void *);
	int (*d_ioctl)(uint32_t, unsigned long, void *, int, void *);
	int (*d_stop)(void *, int);
	int (*d_reset)(void *);
	void *d_ttys;
	int (*d_select)(uint32_t, int, void *, void *);
	int (*d_mmap)(void);
	void (*d_strategy)(void *);
	void *d_reserved_1, *d_reserved_2;
	int   d_type;
};

static inline uint32_t makedev(int maj, int min) { return ((uint32_t)maj << 24) | ((uint32_t)min & 0xffffff); }
static inline int minor_of(uint32_t d) { return (int)(d & 0xffffff); }
static inline int unit_of(uint32_t d) { return minor_of(d) >> UNIT_SHIFT; }
static inline int part_of(uint32_t d) { return minor_of(d) & ((1 << UNIT_SHIFT) - 1); }

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }
static int memeq(const void *a, const void *b, size_t n)
{ const uint8_t *x = a, *y = b; for (; n; n--, x++, y++) if (*x != *y) return 0; return 1; }

static int disk_read(struct unit *u, uint64_t lba, uint32_t n, void *va)
{
	return u->d->xfer(u->d->ctx, lba, n, va, 0);
}

static int disk_write(struct unit *u, uint64_t lba, uint32_t n, const void *va)
{
	if (!u->d->writable) return -1;
	return u->d->xfer(u->d->ctx, lba, n, (void *)va, 1);
}

/* Resolve (unit,part) -> (lba0, nblocks). part 0 = whole disk. */
static int resolve(uint32_t dev, struct unit **uu, uint64_t *lba0, uint64_t *nbl)
{
	int un = unit_of(dev), pt = part_of(dev);
	if (un < 0 || un >= n_units) return -1;
	struct unit *u = &U[un];
	if (pt == 0) { *uu = u; *lba0 = 0; *nbl = u->d->nblocks; return 0; }
	if (pt < 1 || pt > u->nparts || !u->parts[pt - 1].used) return -1;
	*uu = u; *lba0 = u->parts[pt - 1].first;
	*nbl = u->parts[pt - 1].last - u->parts[pt - 1].first + 1;
	return 0;
}

static void blk_strategy(void *bp)
{
	uint32_t fl = buf_flags(bp);
	uint32_t cnt = buf_count(bp);
	uint32_t dev = (uint32_t)(uintptr_t)buf_device(bp);
	int64_t blk = buf_blkno(bp);
	struct unit *u; uint64_t lba0, nbl; char *va;
	if (resolve(dev, &u, &lba0, &nbl)) { buf_seterror(bp, 6); buf_biodone(bp); return; } /* ENXIO */
	uint32_t bs = u->d->bs;
	if (!bs || (cnt % bs)) { buf_seterror(bp, 22); buf_biodone(bp); return; }
	uint64_t lba = lba0 + (uint64_t)blk;
	uint32_t n = cnt / bs;
	if (lba + n > lba0 + nbl) { buf_seterror(bp, 5); buf_biodone(bp); return; }
	if (buf_map(bp, &va)) { buf_seterror(bp, 5); buf_biodone(bp); return; }
	int wr = !(fl & B_READ);
	int r = wr ? disk_write(u, lba, n, va) : disk_read(u, lba, n, va);
	buf_unmap(bp);
	if (r) { buf_seterror(bp, 5); }
	else { buf_setresid(bp, 0); }
	buf_biodone(bp);
}

static int blk_open(uint32_t dev, int f, int d, void *p) { (void)f;(void)d;(void)p; struct unit *u; uint64_t a,b; return resolve(dev,&u,&a,&b) ? 6 : 0; }
static int blk_close(uint32_t dev, int f, int d, void *p) { (void)dev;(void)f;(void)d;(void)p; return 0; }
static int blk_ioctl(uint32_t dev, unsigned long cmd, void *data, int f, void *p)
{
	(void)f; (void)p;
	struct unit *u; uint64_t lba0, nbl;
	if (resolve(dev, &u, &lba0, &nbl)) return 6;
	/* DKIOCGETBLOCKSIZE = 0x40046418, DKIOCGETBLOCKCOUNT = 0x40086419 */
	if (cmd == 0x40046418u) { *(uint32_t *)data = u->d->bs; return 0; }
	if (cmd == 0x40086419u) { *(uint64_t *)data = nbl; return 0; }
	return 25; /* ENOTTY */
}
static int blk_psize(uint32_t dev)
{
	struct unit *u; uint64_t lba0, nbl;
	if (resolve(dev, &u, &lba0, &nbl)) return -1;
	return (int)((nbl * u->d->bs) / 512);   /* Darwin d_psize is in 512-byte sectors */
}
static int blk_dump(void) { return 45; }
static int c_read(uint32_t dev, void *uio, int ioflag, void *p)
{ (void)ioflag;(void)p; return physio((void *)blk_strategy, NULL, dev, B_READ, (void *)minphys, uio); }
static int c_write(uint32_t dev, void *uio, int ioflag, void *p)
{ (void)ioflag;(void)p; return physio((void *)blk_strategy, NULL, dev, 0, (void *)minphys, uio); }
static int c_stop(void *t, int f) { (void)t;(void)f; return 0; }
static int c_reset(void *t) { (void)t; return 0; }
static int c_select(uint32_t d, int f, void *w, void *p) { (void)d;(void)f;(void)w;(void)p; return 1; }
static int c_mmap(void) { return -1; }

static struct bdevsw bsw;   /* filled by blk_start — must live in BSS, no .data */
static struct cdevsw csw;
static int majors_ready;
static int maj_b, maj_c;

static void fill_switches(void)
{
	bsw.d_open = blk_open; bsw.d_close = blk_close; bsw.d_strategy = blk_strategy;
	bsw.d_ioctl = blk_ioctl; bsw.d_dump = blk_dump; bsw.d_psize = blk_psize; bsw.d_type = D_DISK;
	csw.d_open = blk_open; csw.d_close = blk_close; csw.d_read = c_read; csw.d_write = c_write;
	csw.d_ioctl = blk_ioctl; csw.d_stop = c_stop; csw.d_reset = c_reset; csw.d_ttys = NULL;
	csw.d_select = c_select; csw.d_mmap = c_mmap; csw.d_strategy = blk_strategy;
	csw.d_reserved_1 = (void *)(uintptr_t)enodev; csw.d_reserved_2 = (void *)(uintptr_t)enodev;
	csw.d_type = D_DISK;
}

/* ---- GPT ------------------------------------------------------------------------ */
static uint32_t crc32(const uint8_t *p, size_t n, uint32_t c)
{
	c = ~c;
	while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
	return ~c;
}

static int gpt_scan_unit(struct unit *u)
{
	uint32_t bs = u->d->bs;
	uint8_t *hdr = IOMallocZero(bs * 2);
	if (!hdr) return -1;
	if (disk_read(u, 1, 1, hdr)) { IOFree(hdr, bs * 2); return -1; }
	if (!memeq(hdr, "EFI PART", 8)) { IOFree(hdr, bs * 2); return 0; }
	uint32_t hsz = le32(hdr + 12), hcrc = le32(hdr + 16);
	uint64_t ent_lba = le64(hdr + 72); uint32_t nent = le32(hdr + 80), esz = le32(hdr + 84), ecrc = le32(hdr + 88);
	uint8_t save[4]; memcpy(save, hdr + 16, 4); bzero(hdr + 16, 4);
	uint32_t c = (hsz <= bs) ? crc32(hdr, hsz, 0) : 0; memcpy(hdr + 16, save, 4);
	if (c != hcrc) { LOG("%s GPT hdr CRC BAD", u->d->name); IOFree(hdr, bs * 2); return -1; }
	uint32_t total = nent * esz, nblk = (total + bs - 1) / bs;
	if (!esz || total > 128 * 1024) { IOFree(hdr, bs * 2); return -1; }
	uint8_t *ents = IOMallocZero(nblk * bs);
	if (!ents || disk_read(u, ent_lba, nblk, ents)) { if (ents) IOFree(ents, nblk * bs); IOFree(hdr, bs * 2); return -1; }
	if (crc32(ents, total, 0) != ecrc) LOG("%s GPT entries CRC BAD (continuing)", u->d->name);
	int np = 0;
	for (uint32_t i = 0; i < nent && np < MAX_PARTS; i++) {
		uint8_t *e = ents + i * esz; int used = 0;
		for (int k = 0; k < 16; k++) used |= e[k];
		if (!used) continue;
		struct part *p = &u->parts[np];
		memcpy(p->type, e, 16);
		p->first = le64(e + 32); p->last = le64(e + 40); p->used = 1;
		int k; for (k = 0; k < 23 && (e[56 + 2 * k] | e[57 + 2 * k]); k++)
			p->name[k] = (e[57 + 2 * k] || e[56 + 2 * k] > 0x7e) ? '?' : (char)e[56 + 2 * k];
		p->name[k] = 0;
		LOG("  %ss%d \"%s\" LBA %llu..%llu (%llu MiB)", u->d->name, np + 1, p->name,
		    p->first, p->last, ((p->last - p->first + 1) * bs) >> 20);
		np++;
	}
	u->nparts = np;
	IOFree(ents, nblk * bs); IOFree(hdr, bs * 2);
	return np;
}

/* ---- root locator (LAPISFS1) ---------------------------------------------------- */
static struct unit *root_u;
static uint64_t root_lba, root_nbl;

static void *blk_lock;

static int blkops_read(uint64_t off, uint64_t len, void *va)
{
	if (!root_u || !len) return -1;
	uint32_t bs = root_u->d->bs;
	uint64_t end = root_nbl * bs;
	if (off >= end) { bzero(va, (size_t)len); return 0; }
	if (off + len > end) { bzero((uint8_t *)va + (end - off), (size_t)(len - (end - off))); len = end - off; }
	uint64_t lba = root_lba + off / bs;
	uint32_t skip = (uint32_t)(off % bs);
	uint32_t n = (uint32_t)((skip + len + bs - 1) / bs);
	/* Always bounce: caller buffers (e.g. IOMallocZero) must not be assumed DMA-aligned,
	 * and UFS xfer is single-threaded — serialize with blk_lock. */
	if (!blk_lock) blk_lock = IOLockAlloc();
	IOLockLock(blk_lock);
	uint8_t *tmp = IOMallocZero((size_t)n * bs);
	int r = -1;
	if (tmp) {
		r = disk_read(root_u, lba, n, tmp);
		if (!r) memcpy(va, tmp + skip, (size_t)len);
		IOFree(tmp, (size_t)n * bs);
	}
	IOLockUnlock(blk_lock);
	return r;
}

static void publish_root(struct unit *u, int part, uint64_t lba, uint64_t nbl, const char *nm)
{
	root_u = u; root_lba = lba; root_nbl = nbl;
	lapis_blkops.read = blkops_read;
	lapis_blkops.root_size = nbl * u->d->bs;
	int i; for (i = 0; i < 31 && nm[i]; i++) lapis_blkops.root_name[i] = nm[i];
	lapis_blkops.root_name[i] = 0;
	lapis_blkops.magic = LAPIS_BLKOPS_MAGIC;
	lapis_blkops.state = 1;
	LOG("ROOT = %s (LBA %llu, %llu blocks)", nm, lba, nbl);
}

static int peek_lfs(struct unit *u, uint64_t lba, uint8_t *hdr64)
{
	uint32_t bs = u->d->bs;
	uint8_t *tmp = IOMallocZero(bs);   /* not IOMallocAligned: IOFree != IOFreeAligned */
	if (!tmp) return -1;
	int r = disk_read(u, lba, 1, tmp);
	if (!r) memcpy(hdr64, tmp, 64);
	IOFree(tmp, bs);
	return r;
}

static void consider_root(struct unit *u)
{
	uint8_t hdr[64];
	for (int i = 0; i < u->nparts; i++) {
		struct part *p = &u->parts[i];
		if (!p->used) continue;
		int named = !strcmp(p->name, "lapis-root") || !strcmp(p->name, "LAPIS-ROOT");
		LOG("consider %ss%d \"%s\" named=%d", u->d->name, i+1, p->name, named);
		if (peek_lfs(u, p->first, hdr) || !memeq(hdr, "LAPISFS1", 8)) {
			if (named) LOG("%ss%d named lapis-root but no LAPISFS1 magic", u->d->name, i + 1);
			continue;
		}
		char nm[32]; ksnprintf(nm, sizeof(nm), "disk%ds%d", (int)(u - U), i + 1);
		if (lapis_blkops.state != 1) publish_root(u, i + 1, p->first, p->last - p->first + 1, nm);
		return;
	}
	if (!peek_lfs(u, 0, hdr) && memeq(hdr, "LAPISFS1", 8)) {
		char nm[32]; ksnprintf(nm, sizeof(nm), "disk%d", (int)(u - U));
		if (lapis_blkops.state != 1) publish_root(u, 0, 0, u->d->nblocks, nm);
	}
}

/* ---- registration ---------------------------------------------------------------- */
static void make_nodes(struct unit *u, int unit)
{
	if (!k_devfs_ready) {
		LOG("/dev nodes deferred (devfs not ready yet); ROOT locator still active");
		return;
	}
	ksnprintf(u->names[0], sizeof(u->names[0]), "disk%d", unit);
	devfs_make_node(makedev(u->major_b, unit << UNIT_SHIFT), DEVFS_BLOCK, 0, 0, 0600, u->names[0]);
	char rn[16]; ksnprintf(rn, sizeof(rn), "rdisk%d", unit);
	devfs_make_node(makedev(u->major_c, unit << UNIT_SHIFT), DEVFS_CHAR, 0, 0, 0600, rn);
	for (int i = 0; i < u->nparts; i++) {
		ksnprintf(u->names[i + 1], sizeof(u->names[i + 1]), "disk%ds%d", unit, i + 1);
		devfs_make_node(makedev(u->major_b, (unit << UNIT_SHIFT) | (i + 1)), DEVFS_BLOCK, 0, 0, 0600, u->names[i + 1]);
		char r[16]; ksnprintf(r, sizeof(r), "rdisk%ds%d", unit, i + 1);
		devfs_make_node(makedev(u->major_c, (unit << UNIT_SHIFT) | (i + 1)), DEVFS_CHAR, 0, 0, 0600, r);
	}
	LOG("published /dev/disk%d (+ %d slices)", unit, u->nparts);
}

int blk_register(struct lapis_disk *d)
{
	if (!d || !d->xfer || !d->bs || !d->nblocks) return -1;
	if (!lock) lock = IOLockAlloc();
	IOLockLock(lock);
	if (n_units >= MAX_DISKS) { IOLockUnlock(lock); LOG("too many disks"); return -1; }
	int unit = n_units++;
	struct unit *u = &U[unit];
	u->d = d;
	/* one shared major pair for all units (bdevsw_add(-1) allocates) */
	if (!majors_ready) {
		fill_switches();
		maj_b = bdevsw_add(-1, &bsw);
		maj_c = cdevsw_add_with_bdev(-1, &csw, maj_b);
		if (maj_b < 0 || maj_c < 0) {
			LOG("bdevsw/cdevsw add failed (%d/%d)", maj_b, maj_c);
			n_units--; IOLockUnlock(lock); return -1;
		}
		majors_ready = 1;
		LOG("majors block=%d char=%d", maj_b, maj_c);
	}
	u->major_b = maj_b; u->major_c = maj_c;
	IOLockUnlock(lock);
	LOG("disk%d = %s: %llu blocks x %u = %llu MiB%s", unit, d->name, d->nblocks, d->bs,
	    (d->nblocks * d->bs) >> 20, d->writable ? "" : " (ro)");
	gpt_scan_unit(u);
	consider_root(u);   /* before make_nodes: mountroot may race */
	make_nodes(u, unit);
	return unit;
}

void blk_pending_add(int delta)
{
	if (!lock) lock = IOLockAlloc();
	IOLockLock(lock);
	pending += delta;
	if (pending <= 0 && started && lapis_blkops.state == 0) {
		lapis_blkops.state = -1;   /* no root found */
		LOG("no LAPISFS1 root on any disk (md0 remains the fallback)");
	}
	IOLockUnlock(lock);
}

static void nodes_thread(void *arg, int wr)
{
	(void)arg; (void)wr;
	/* bsd_init mounts /dev ~40s into boot; prior 20s timeout was too short. */
	for (int t = 0; t < 12000 && !k_devfs_ready; t++) IOSleep(10);
	if (!k_devfs_ready) {
		LOG("devfs still not ready after 120s; will keep polling");
		for (int t = 0; t < 1200 && !k_devfs_ready; t++) IOSleep(100);
	}
	if (!k_devfs_ready) { LOG("devfs never became ready"); return; }
	LOG("devfs ready — publishing /dev/disk*");
	for (int i = 0; i < n_units; i++) make_nodes(&U[i], i);
}

void blk_start(void)
{
	if (!lock) lock = IOLockAlloc();
	if (!U) U = IOMallocZero(sizeof(struct unit) * MAX_DISKS);
	lapis_blkops.magic = LAPIS_BLKOPS_MAGIC;
	lapis_blkops.state = 0;
	lapis_blkops.read = NULL;
	started = 1;
	drv_thread(nodes_thread, NULL);
	LOG("block layer ready (waiting for disk drivers)");
}
