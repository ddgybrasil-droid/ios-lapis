/*
 * LapisUFS - UFSHCI (JESD223) host controller core for the lapis kernel.
 *
 *  ADT /ufs  compatible "lapis,ufshci-qemu-pci"  -> QEMU `-device ufs` behind the virt
 *            PCIe host (ECAM probe + BAR0 assign), INTx through LapisGICv3.
 *            compatible "qcom,ufshc"             -> SM8650 UFS HC at reg (ABL-initialised).
 *  lapis,mode "full"  : HCE reset/enable, DME_LINKSTARTUP, UTRL, NOP, fDeviceInit,
 *                       INQUIRY / TEST UNIT READY / READ CAPACITY(10) / READ(10), GPT.
 *             "probe" : read-only register dump (no writes, no DMA)   [hw default]
 *             "reuse" : keep ABL's link (no HCE reset, no PHY init), only re-point UTRL
 *                       and issue commands (boot-arg lapis_ufs_mode=2; NOT TESTED on hw).
 * Single doorbell slot 0, legacy (non-MCQ) transfer request list, polled completion with
 * the UTRCS interrupt counted through IOService::registerInterrupt when available.
 */
#include "../include/kern.h"
#include "ufshci.h"
#include "../include/blk.h"

#define LOG(fmt, ...) klog("LapisUFS: " fmt "\n", ##__VA_ARGS__)
#define PG 0x4000u

static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static inline void put_be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }
static inline uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint64_t le64(const uint8_t *p) { return le32(p) | (uint64_t)le32(p + 4) << 32; }

struct ufs {
	vm_offset_t hc;            /* UFSHCI registers */
	void       *nub;
	uint8_t    *dma;           /* one 16K page: UTRL @0, UTMRL @0x400, UCD @0x1000 */
	pmap_paddr_t dma_pa;
	uint8_t    *buf;           /* 16K data page */
	pmap_paddr_t buf_pa;
	uint32_t    cap, lun, bs;
	uint64_t    nblocks;
	uint8_t     tag;
	volatile uint32_t irqs, last_is;
	int         irq_ok;
};
static struct ufs U;
static void publish_ufs_disk(void);
static int ufs_write_blocks(uint64_t, uint32_t, const void *);
int ufs_read_blocks(uint64_t, uint32_t, void *);

static uint32_t R(uint32_t o) { return rd32(U.hc + o); }
static void W(uint32_t o, uint32_t v) { wr32(U.hc + o, v); }

static void dcache_clean_inval(void *va, size_t n)
{
	for (uintptr_t a = (uintptr_t)va & ~63ul; a < (uintptr_t)va + n; a += 64)
		__asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
	dsb_sy();
}

static int wait_bits(uint32_t off, uint32_t mask, uint32_t want, unsigned ms)
{
	for (unsigned i = 0; i <= ms * 10; i++) {
		if ((R(off) & mask) == want) return 1;
		IODelay(100);
	}
	return 0;
}

/* ---- interrupt (UTRCS etc.) -------------------------------------------------------- */
static void ufs_isr(void *target, void *refCon, void *nub, int source)
{
	(void)target; (void)refCon; (void)nub; (void)source;
	uint32_t is = R(UFS_IS);
	W(UFS_IS, is);            /* W1C; deasserts the level line */
	U.last_is = is;
	U.irqs++;
}

/* ---- UIC ------------------------------------------------------------------------- */
static int uic_cmd(uint32_t cmd, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t *res3)
{
	if (!wait_bits(UFS_HCS, HCS_UCRDY, HCS_UCRDY, 100)) { LOG("UIC not ready (HCS %#x)", R(UFS_HCS)); return -1; }
	W(UFS_IS, IS_UCCS);
	W(UFS_UCMDARG1, a1); W(UFS_UCMDARG2, a2); W(UFS_UCMDARG3, a3);
	W(UFS_UICCMD, cmd);
	if (!wait_bits(UFS_IS, IS_UCCS, IS_UCCS, 500)) { LOG("UIC cmd %#x timeout", cmd); return -1; }
	W(UFS_IS, IS_UCCS);
	if (res3) *res3 = R(UFS_UCMDARG3);
	return (int)(R(UFS_UCMDARG2) & 0xff);   /* ConfigResultCode, 0 = success */
}

/* ---- transfer request (slot 0) ----------------------------------------------------- */
static struct utrd *utrd0(void) { return (struct utrd *)U.dma; }
static uint8_t *ucd(void) { return U.dma + 0x1000; }

/* Build UTRD for slot 0 and ring.  Command UPIU must already be at ucd()+UCD_CMD_OFF.
 * dlen>0: one PRDT entry -> U.buf.  Returns OCS (0 ok), -1 timeout. */
static int utr_issue(uint32_t dd, uint32_t dlen)
{
	struct utrd *d = utrd0();
	pmap_paddr_t ucd_pa = U.dma_pa + 0x1000;
	bzero(d, sizeof(*d));
	d->dw0 = UTRD_CT_UFS_STORAGE | dd | UTRD_INTR;
	d->dw2_ocs = OCS_INVALID;
	d->ucdba = (uint32_t)ucd_pa; d->ucdbau = (uint32_t)(ucd_pa >> 32);
	d->resp_off_dw = UCD_RSP_OFF / 4; d->resp_len_dw = 0x200 / 4;
	d->prdt_off_dw = UCD_PRDT_OFF / 4; d->prdt_len = dlen ? 1 : 0;
	if (dlen) {
		struct prdt *p = (struct prdt *)(ucd() + UCD_PRDT_OFF);
		p->addr_lo = (uint32_t)U.buf_pa; p->addr_hi = (uint32_t)(U.buf_pa >> 32); p->rsvd = 0;
		p->dbc = dlen - 1;
		dcache_clean_inval(U.buf, dlen);
	}
	bzero(ucd() + UCD_RSP_OFF, 0x200);
	dcache_clean_inval(U.dma, PG);
	W(UFS_UTRLDBR, 1);
	int ok = wait_bits(UFS_UTRLDBR, 1, 0, 2000);
	dcache_clean_inval(U.dma, PG);
	if (dlen) dcache_clean_inval(U.buf, dlen);
	if (!ok) { LOG("UTRL slot0 timeout: IS %#x HCS %#x DBR %#x", R(UFS_IS), R(UFS_HCS), R(UFS_UTRLDBR)); return -1; }
	if (!U.irq_ok) W(UFS_IS, R(UFS_IS) & (IS_UTRCS | IS_UE));
	return (int)(d->dw2_ocs & 0xff);
}

static uint8_t *upiu_hdr(uint8_t type, uint8_t flags, uint8_t lun, uint8_t func)
{
	uint8_t *u = ucd() + UCD_CMD_OFF;
	bzero(u, 0x200);
	u[0] = type; u[1] = flags; u[2] = lun; u[3] = ++U.tag; u[5] = func;
	return u;
}

static int nop_ping(void)
{
	upiu_hdr(UPIU_NOP_OUT, 0, 0, 0);
	int ocs = utr_issue(UTRD_DD_NONE, 0);
	uint8_t *r = ucd() + UCD_RSP_OFF;
	if (ocs) { LOG("NOP OUT: OCS %#x", ocs); return -1; }
	if (r[0] != UPIU_NOP_IN) { LOG("NOP OUT: got UPIU %#x", r[0]); return -1; }
	return 0;
}

/* Query: returns query response code (0 = success) or -1; *val = value/flag */
static int query(uint8_t func, uint8_t op, uint8_t idn, uint16_t len, uint32_t *val, uint8_t **data)
{
	uint8_t *u = upiu_hdr(UPIU_QUERY_REQ, 0, 0, func);
	u[12] = op; u[13] = idn; u[14] = 0; u[15] = 0; put_be16(u + 18, len);
	int ocs = utr_issue(UTRD_DD_NONE, 0);
	uint8_t *r = ucd() + UCD_RSP_OFF;
	if (ocs) { LOG("query op %u idn %u: OCS %#x", op, idn, ocs); return -1; }
	if (r[0] != UPIU_QUERY_RSP) { LOG("query: UPIU %#x", r[0]); return -1; }
	if (val) *val = be32(r + 20);
	if (data) *data = r + 32;
	return r[6];
}

/* SCSI command via COMMAND UPIU; data (device->host) lands in U.buf. 0 = GOOD */
static int scsi(const uint8_t *cdb, unsigned cdblen, uint32_t dlen, uint8_t *sense_key)
{
	uint8_t *u = upiu_hdr(UPIU_COMMAND, dlen ? UPIU_FLAG_READ : 0, (uint8_t)U.lun, 0);
	u[4] = 0;                               /* command set: SCSI */
	put_be32(u + 12, dlen);
	memcpy(u + 16, cdb, cdblen);
	int ocs = utr_issue(dlen ? UTRD_DD_READ : UTRD_DD_NONE, dlen);
	uint8_t *r = ucd() + UCD_RSP_OFF;
	if (sense_key) *sense_key = 0;
	if (ocs) { LOG("SCSI %#x: OCS %#x", cdb[0], ocs); return -1; }
	if (r[0] != UPIU_RESPONSE) { LOG("SCSI %#x: UPIU %#x", cdb[0], r[0]); return -1; }
	if (r[6] || r[7]) {
		/* sense data: data segment = 2-byte length + fixed-format sense */
		if (sense_key && be16(r + 10) >= 4) *sense_key = r[32 + 2 + 2] & 0xf;
		return (r[6] << 8) | r[7];
	}
	return 0;
}

int ufs_read_blocks(uint64_t lba, uint32_t n, void *out)
{
	while (n) {
		uint32_t k = PG / U.bs; if (k > n) k = n;
		uint8_t c[10] = {0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba, 0, (uint8_t)(k >> 8), (uint8_t)k, 0};
		int s = scsi(c, 10, k * U.bs, NULL);
		if (s) { LOG("READ(10) lba %llu x%u -> %#x", lba, k, s); return -1; }
		memcpy(out, U.buf, k * U.bs);
		out = (uint8_t *)out + k * U.bs; lba += k; n -= k;
	}
	return 0;
}

static int ufs_write_blocks(uint64_t lba, uint32_t n, const void *in)
{
	while (n) {
		uint32_t k = PG / U.bs; if (k > n) k = n;
		memcpy(U.buf, in, k * U.bs);
		uint8_t c[10] = {0x2a, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba, 0, (uint8_t)(k >> 8), (uint8_t)k, 0};
		/* WRITE(10) needs UTRD_DD_WRITE */
		uint8_t *u = upiu_hdr(UPIU_COMMAND, UPIU_FLAG_WRITE, (uint8_t)U.lun, 0);
		u[4] = 0; put_be32(u + 12, k * U.bs); memcpy(u + 16, c, 10);
		int ocs = utr_issue(UTRD_DD_WRITE, k * U.bs);
		uint8_t *r = ucd() + UCD_RSP_OFF;
		if (ocs || r[0] != UPIU_RESPONSE || r[6] || r[7]) {
			LOG("WRITE(10) lba %llu x%u -> ocs %#x upiu %#x", lba, k, ocs, r[0]); return -1;
		}
		in = (const uint8_t *)in + k * U.bs; lba += k; n -= k;
	}
	return 0;
}

static int memcmp_(const void *a, const void *b, size_t n)
{ const uint8_t *x = a, *y = b; for (; n; n--, x++, y++) if (*x != *y) return *x - *y; return 0; }

/* ---- GPT (UEFI 2.x ch.5) ----------------------------------------------------------- */
static uint32_t crc32(const uint8_t *p, size_t n, uint32_t c)
{
	c = ~c;
	while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
	return ~c;
}

static void guid_str(const uint8_t *g, char *s)
{
	ksnprintf(s, 40, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X", le32(g), g[4] | g[5] << 8, g[6] | g[7] << 8,
	    g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

static void gpt_scan(void)
{
	uint8_t *hdr = IOMallocZero(PG), *ents = NULL;
	char gs[40], name[40], prop[64];
	uint32_t nblk = 0;
	if (ufs_read_blocks(1, 1, hdr)) goto out;
	if (memcmp_(hdr, "EFI PART", 8)) { LOG("LBA1: no GPT signature"); goto out; }
	uint32_t hsz = le32(hdr + 12), hcrc = le32(hdr + 16);
	uint64_t ent_lba = le64(hdr + 72); uint32_t nent = le32(hdr + 80), esz = le32(hdr + 84), ecrc = le32(hdr + 88);
	uint8_t save[4]; memcpy(save, hdr + 16, 4); bzero(hdr + 16, 4);
	uint32_t c = (hsz <= U.bs) ? crc32(hdr, hsz, 0) : 0; memcpy(hdr + 16, save, 4);
	guid_str(hdr + 56, gs);
	LOG("GPT: rev %#x hdr %u bytes crc %s, disk %s, usable LBA %llu..%llu, %u entries x %u @ LBA %llu",
	    le32(hdr + 8), hsz, c == hcrc ? "OK" : "BAD", gs, le64(hdr + 40), le64(hdr + 48), nent, esz, ent_lba);
	uint32_t total = nent * esz; nblk = (total + U.bs - 1) / U.bs;
	if (!esz || total > 64 * 1024) { LOG("GPT: entry table too large"); nblk = 0; goto out; }
	ents = IOMallocZero(nblk * U.bs);
	if (ufs_read_blocks(ent_lba, nblk, ents)) goto out;
	LOG("GPT: entry array crc %s", crc32(ents, total, 0) == ecrc ? "OK" : "BAD");
	int np = 0;
	for (uint32_t i = 0; i < nent; i++) {
		uint8_t *e = ents + i * esz; int used = 0;
		for (int k = 0; k < 16; k++) used |= e[k];
		if (!used) continue;
		int k; for (k = 0; k < 36 && (e[56 + 2 * k] | e[57 + 2 * k]); k++) name[k] = (e[57 + 2 * k] || e[56 + 2 * k] > 0x7e) ? '?' : (char)e[56 + 2 * k];
		name[k] = 0;
		guid_str(e, gs);
		uint64_t f = le64(e + 32), l = le64(e + 40);
		LOG("  disk0s%d \"%s\" LBA %llu..%llu (%llu MiB) type %s", ++np, name, f, l, ((l - f + 1) * U.bs) >> 20, gs);
		if (!ufs_read_blocks(f, 1, hdr)) {    /* first block of the partition */
			char t[33]; int j; for (j = 0; j < 32 && hdr[j] >= 0x20 && hdr[j] < 0x7f; j++) t[j] = (char)hdr[j];
			t[j] = 0; LOG("    block %llu: \"%s\"%s", f, t, j ? "" : " (not printable)");
		}
		ksnprintf(prop, sizeof(prop), "disk0s%d", np);
		char v[96]; ksnprintf(v, sizeof(v), "%s lba=%llu..%llu type=%s", name, f, l, gs);
		k_IORE_setPropertyStr(U.nub, prop, v);
	}
	k_IORE_setPropertyNum(U.nub, "gpt-partitions", np, 32);
out:
	if (ents) IOFree(ents, nblk * U.bs);
	IOFree(hdr, PG);
}


/* ---- bring-up ---------------------------------------------------------------------- */
static void dump_regs(const char *when)
{
	LOG("%s: CAP %#x VER %#x HCPID %#x HCMID %#x AHIT %#x IS %#x IE %#x HCS %#x HCE %#x "
	    "UTRLBA %#x:%08x RSR %u DBR %#x UTMRL RSR %u", when, R(UFS_CAP), R(UFS_VER), R(UFS_HCPID), R(UFS_HCMID),
	    R(UFS_AHIT), R(UFS_IS), R(UFS_IE), R(UFS_HCS), R(UFS_HCE), R(UFS_UTRLBAU), R(UFS_UTRLBA),
	    R(UFS_UTRLRSR), R(UFS_UTRLDBR), R(UFS_UTMRLRSR));
}

static int hc_enable_and_link(void)
{
	if (R(UFS_HCE) & HCE_ENABLE) {           /* UFSHCI 7.1.1: disable first */
		W(UFS_HCE, 0);
		if (!wait_bits(UFS_HCE, HCE_ENABLE, 0, 100)) { LOG("HCE stuck at 1"); return -1; }
	}
	W(UFS_HCE, HCE_ENABLE);
	if (!wait_bits(UFS_HCE, HCE_ENABLE, HCE_ENABLE, 100)) { LOG("HCE did not set"); return -1; }
	W(UFS_IE, 0);
	W(UFS_IS, 0xffffffff);
	int r = -1;
	for (int t = 0; t < 3 && r; t++) r = uic_cmd(UIC_DME_LINK_STARTUP, 0, 0, 0, NULL);
	if (r) { LOG("DME_LINKSTARTUP failed (%d)", r); return -1; }
	if (!(R(UFS_HCS) & HCS_DP)) { LOG("link up but no device (HCS %#x)", R(UFS_HCS)); return -1; }
	uint32_t v = 0;   /* PA_ConnectedTxDataLanes (0x1561) / PA_ConnectedRxDataLanes (0x1581) */
	int g1 = uic_cmd(UIC_DME_GET, 0x1561u << 16, 0, 0, &v);
	LOG("DME_LINKSTARTUP OK, HCS %#x%s", R(UFS_HCS), g1 == 0 ? "" : " (DME_GET unsupported)");
	if (g1 == 0) LOG("  PA_ConnectedTxDataLanes = %u", v);
	return 0;
}

static int lists_start(int keep_running)
{
	pmap_paddr_t utrl = U.dma_pa, utmrl = U.dma_pa + 0x400;
	if (keep_running && R(UFS_UTRLRSR)) {
		if (R(UFS_UTRLDBR)) { LOG("reuse: doorbell busy %#x, abort", R(UFS_UTRLDBR)); return -1; }
		W(UFS_UTRLRSR, 0);
	}
	if (!wait_bits(UFS_HCS, HCS_UTRLRDY, HCS_UTRLRDY, 100)) { LOG("UTRL not ready (HCS %#x)", R(UFS_HCS)); return -1; }
	W(UFS_UTRLBA, (uint32_t)utrl); W(UFS_UTRLBAU, (uint32_t)(utrl >> 32));
	W(UFS_UTMRLBA, (uint32_t)utmrl); W(UFS_UTMRLBAU, (uint32_t)(utmrl >> 32));
	W(UFS_UTRLRSR, 1);
	W(UFS_UTMRLRSR, 1);
	return 0;
}

static void ufs_irq_setup(void)
{
	if (!k_IORE_getProperty(U.nub, "IOInterruptControllers")) { LOG("no interrupt specifier: polled only"); return; }
	if (k_IOService_registerInterrupt(U.nub, 0, U.nub, ufs_isr, NULL) ||
	    k_IOService_enableInterrupt(U.nub, 0)) { LOG("registerInterrupt failed: polled only"); return; }
	U.irq_ok = 1;
	W(UFS_IS, 0xffffffff);
	W(UFS_IE, IS_UTRCS | IS_UE | IS_FATAL);
}

static int storage_bringup(void)
{
	uint8_t sk = 0; uint32_t flag = 1; int r;
	if (nop_ping()) return -1;
	LOG("NOP OUT -> NOP IN ok");
	if ((r = query(QUERY_FUNC_WRITE, QUERY_OP_SET_FLAG, QUERY_FLAG_FDEVICEINIT, 0, NULL, NULL))) { LOG("set fDeviceInit -> %d", r); return -1; }
	for (int i = 0; i < 1000 && flag; i++) {
		if (query(QUERY_FUNC_READ, QUERY_OP_READ_FLAG, QUERY_FLAG_FDEVICEINIT, 0, &flag, NULL)) return -1;
		flag &= 1; if (flag) IOSleep(1);
	}
	if (flag) { LOG("fDeviceInit never cleared"); return -1; }
	uint8_t *dd = NULL;
	if (!query(QUERY_FUNC_READ, QUERY_OP_READ_DESC, DESC_IDN_DEVICE, 0x40, NULL, &dd) && dd)
		LOG("fDeviceInit done; Device descriptor: len %u bNumberLU %u wSpecVersion %#x wManufacturerID %#x",
		    dd[0], dd[6], be16(dd + 0x10), be16(dd + 0x18));
	uint8_t inq[6] = {0x12, 0, 0, 0, 36, 0};
	if (!scsi(inq, 6, 36, NULL)) {
		char v[9], p[17]; memcpy(v, U.buf + 8, 8); v[8] = 0; memcpy(p, U.buf + 16, 16); p[16] = 0;
		LOG("LUN %u INQUIRY: type %#x vendor '%s' product '%s'", U.lun, U.buf[0] & 0x1f, v, p);
		k_IORE_setPropertyStr(U.nub, "lu0-product", p);
	}
	uint8_t tur[6] = {0};
	for (int i = 0; i < 5; i++) { r = scsi(tur, 6, 0, &sk); if (!r) break; IOSleep(10); }
	if (r) LOG("TEST UNIT READY -> %#x sense key %#x", r, sk);
	uint8_t rc[10] = {0x25};
	if ((r = scsi(rc, 10, 8, NULL))) { LOG("READ CAPACITY(10) -> %#x", r); return -1; }
	U.nblocks = (uint64_t)be32(U.buf) + 1; U.bs = be32(U.buf + 4);
	if (U.bs < 512 || U.bs > PG) { LOG("unsupported block size %u", U.bs); return -1; }
	LOG("LUN %u: %llu blocks x %u bytes = %llu MiB", U.lun, U.nblocks, U.bs, (U.nblocks * U.bs) >> 20);
	k_IORE_setPropertyNum(U.nub, "lu0-block-count", U.nblocks, 64);
	k_IORE_setPropertyNum(U.nub, "lu0-block-size", U.bs, 32);
	return 0;
}

/* QEMU virt PCIe: find the REDHAT UFS function (1b36:0013) and assign BAR0 */
static vm_offset_t qemu_pci_ufs(void *nub, uint32_t *intid)
{
	uint64_t ecam, ecsz, m32, m32sz; uint32_t spi_base = 3;
	if (!drv_dt_u64(nub, "pci-ecam", 0, &ecam) || !drv_dt_u64(nub, "pci-ecam", 1, &ecsz) ||
	    !drv_dt_u64(nub, "pci-mmio32", 0, &m32) || !drv_dt_u64(nub, "pci-mmio32", 1, &m32sz)) { LOG("bad pci props"); return 0; }
	drv_dt_u32(nub, "pci-intx-spi-base", &spi_base);
	vm_offset_t cfg = ml_io_map(ecam, 0x100000);    /* bus 0 */
	for (uint32_t dev = 0; dev < 32; dev++) {
		vm_offset_t f = cfg + (dev << 15);
		uint32_t id = rd32(f);
		if (id != 0x00131b36) continue;
		uint32_t cls = rd32(f + 8);
		wr32(f + 0x10, 0xffffffff);
		uint32_t sz = ~(rd32(f + 0x10) & ~0xfu) + 1;
		uint64_t bar = (m32 + sz - 1) & ~(uint64_t)(sz - 1);
		wr32(f + 0x10, (uint32_t)bar);
		wr32(f + 0x04, (rd32(f + 0x04) & ~(1u << 10)) | 0x6);   /* MEM + BUS MASTER, INTx enabled */
		uint32_t pin = (rd32(f + 0x3c) >> 8) & 0xff;
		*intid = pin ? 32 + spi_base + ((dev + pin - 1) % 4) : 0;
		LOG("PCI 00:%02x.0 %04x:%04x class %06x BAR0 %#llx size %#x INT%c -> INTID %u", dev, id & 0xffff, id >> 16,
		    cls >> 8, bar, sz, pin ? 'A' + pin - 1 : '-', *intid);
		return ml_io_map(bar, sz);
	}
	LOG("no QEMU UFS PCI function on bus 0");
	return 0;
}

static void ufs_thread(void *arg, int wr)
{
	(void)arg; (void)wr;
	unsigned len;
	int pending = 0;
	void *nub = drv_dt_service("/ufs", 3000);
	if (!nub) { LOG("no /ufs nub"); return; }
	U.nub = nub;
	const char *compat = drv_dt_prop(nub, "compatible", &len);
	const char *mode = drv_dt_prop(nub, "lapis,mode", &len);
	int m = mode && !strcmp(mode, "full") ? 1 : mode && !strcmp(mode, "reuse") ? 2 : 0;
	m = drv_bootarg("lapis_ufs_mode", (uint32_t)m);
	uint32_t intid = 0;
	if (compat && !strcmp(compat, "lapis,ufshci-qemu-pci")) {
		U.hc = qemu_pci_ufs(nub, &intid);
		const uint32_t *ip = drv_dt_prop(nub, "interrupts", &len);
		if (ip && intid && ip[0] != intid) LOG("ADT interrupts <%u> != PCI INTx INTID %u", ip[0], intid);
	} else {
		uint64_t pa, sz;
		if (!drv_dt_u64(nub, "reg", 0, &pa) || !drv_dt_u64(nub, "reg", 1, &sz)) { LOG("no reg"); return; }
		LOG("probing %s @ %#llx (mode %d) ...", compat ? compat : "?", pa, m);
		U.hc = ml_io_map(pa, sz);
	}
	if (!U.hc) return;
	U.cap = R(UFS_CAP);
	dump_regs("initial");
	LOG("NUTRS %u NUTMRS %u 64AS %u MCQS %u, UFSHCI %u.%u", (U.cap & 0x1f) + 1, ((U.cap >> 16) & 7) + 1,
	    (U.cap >> 24) & 1, (U.cap >> 30) & 1, (R(UFS_VER) >> 8) & 0xff, (R(UFS_VER) >> 4) & 0xf);
	/* TB520FU dtb: iommus = <apps_smmu 0x60 0>, dma-coherent. ABL leaves the
	 * controller live; reuse mode assumes Stage-2 / SMMU already permits DMA
	 * for stream 0x60 (stock Android boots from this same HC). If reuse DMA
	 * faults on hw, set lapis_ufs_mode=0 and capture the abort — do NOT poke
	 * the SMMU from this driver without an explicit boot-arg. */
	if (compat && !strcmp(compat, "qcom,ufshc")) {
		LOG("hw: SMMU stream 0x60 (dtb); MCQ regs @ 0x1da0000 present — using legacy UTRL path");
		if ((U.cap & CAP_MCQS) && m == 2)
			LOG("CAP.MCQS=1 but reuse keeps legacy UTRL (ABL path); MCQ switch needs lapis_ufs_mcq=1");
	}
	k_IORE_setPropertyNum(nub, "ufshci-cap", U.cap, 32);
	k_IORE_setPropertyNum(nub, "ufshci-ver", R(UFS_VER), 32);
	if (m == 0) { LOG("probe-only mode: done (no writes)"); return; }

	/* Signal LapisBlk that a root candidate may still appear. */
	blk_pending_add(+1);
	pending = 1;

	U.dma = IOMallocAligned(PG, PG); U.buf = IOMallocAligned(PG, PG);
	if (!U.dma || !U.buf) { LOG("no memory"); goto done; }
	bzero(U.dma, PG); bzero(U.buf, PG);
	U.dma_pa = kvtophys((vm_offset_t)U.dma); U.buf_pa = kvtophys((vm_offset_t)U.buf);
	if (((U.dma_pa | U.buf_pa) >> 32) && !(U.cap & CAP_64AS)) { LOG("DMA pages above 4G but no 64AS"); goto done; }
	LOG("DMA: lists/UCD page pa %#llx, data page pa %#llx", U.dma_pa, U.buf_pa);
	uint64_t t0 = now_ns();
	if (m == 1) { if (hc_enable_and_link()) goto done; }
	else { LOG("reuse mode: keeping ABL link (HCE %u HCS %#x)", R(UFS_HCE) & 1, R(UFS_HCS));
	       if (!(R(UFS_HCE) & 1) || !(R(UFS_HCS) & HCS_DP)) { LOG("reuse: HC not enabled / no device"); goto done; } }
	if (lists_start(m == 2)) goto done;
	ufs_irq_setup();
	if (storage_bringup()) { dump_regs("failed"); goto done; }
	LOG("bring-up %llu ms; UTRCS interrupts so far: %u (last IS %#x)%s", (now_ns() - t0) / 1000000, U.irqs, U.last_is,
	    U.irq_ok ? " via LapisGICv3" : "");
	k_IORE_setPropertyNum(nub, "ufs-irq-count", U.irqs, 32);
	gpt_scan();
	LOG("done (%u completion interrupts dispatched)", U.irqs);
	publish_ufs_disk();
done:
	if (pending) blk_pending_add(-1);
}

static int ufs_xfer(void *ctx, uint64_t lba, uint32_t n, void *va, int write)
{
	(void)ctx;
	return write ? ufs_write_blocks(lba, n, va) : ufs_read_blocks(lba, n, va);
}

static void publish_ufs_disk(void)
{
	static struct lapis_disk disk;
	ksnprintf(disk.name, sizeof(disk.name), "ufs-lun%u", U.lun);
	disk.bs = U.bs; disk.nblocks = U.nblocks;
	disk.writable = drv_bootarg("lapis_ufs_write", 0);
	disk.xfer = ufs_xfer; disk.ctx = NULL;
	disk.max_blocks = PG / U.bs;
	blk_register(&disk);
}


void lapis_ufs_start(void)
{
	drv_thread(ufs_thread, NULL);
}
