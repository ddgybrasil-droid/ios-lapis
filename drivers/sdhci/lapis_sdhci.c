/*
 * LapisSDHCI — SD card host at 0x8804000 (qcom,sdhci-msm-v5).
 *
 * Boot-args:
 *   lapis_sdhci=1          start driver
 *   lapis_sdhci_mode=0     probe / card-detect only (default)
 *   lapis_sdhci_mode=1     CMD init sequence (CMD0/8/ACMD41/2/3/9/7)
 *   lapis_sdhci_mode=2     + single-block PIO READ (CMD17) and publish LapisBlk disk
 */
#include "../include/kern.h"
#include "../include/blk.h"
#define LOG(fmt, ...) klog("LapisSDHCI: " fmt "\n", ##__VA_ARGS__)
#define SDHCI_PA 0x08804000ull
#define SDHC_PRESENT 0x24
#define SDHC_HOSTVER 0xfe
#define SDHC_CAPS 0x40
#define SDHC_SWRST 0x2f
#define SDHC_PWRCTL 0x29
#define SDHC_CLKCTL 0x2c
#define SDHC_ARG 0x08
#define SDHC_CMD 0x0e
#define SDHC_TMODE 0x0c
#define SDHC_BSIZE 0x04
#define SDHC_BCOUNT 0x06
#define SDHC_RESP0 0x10
#define SDHC_NINT 0x30
#define SDHC_EINT 0x32
#define SDHC_BUF 0x20
#define PRESENT_CARD (1u<<16)

static vm_offset_t hc;
static uint32_t rca, block_count;
static uint32_t card_ocr;
static inline uint32_t R32(uint32_t o){return rd32(hc+o);}
static inline void W32(uint32_t o,uint32_t v){wr32(hc+o,v);}
static inline uint16_t R16(uint32_t o){uint32_t a=o&~3u,s=(o&3u)*8;return (uint16_t)(rd32(hc+a)>>s);}
static inline void W16(uint32_t o,uint16_t v){uint32_t a=o&~3u,s=(o&3u)*8;uint32_t x=rd32(hc+a);wr32(hc+a,(x&~(0xffffu<<s))|((uint32_t)v<<s));}
static inline uint8_t R8(uint32_t o){uint32_t a=o&~3u,s=(o&3u)*8;return (uint8_t)(rd32(hc+a)>>s);}
static inline void W8(uint32_t o,uint8_t v){uint32_t a=o&~3u,s=(o&3u)*8;uint32_t x=rd32(hc+a);wr32(hc+a,(x&~(0xffu<<s))|((uint32_t)v<<s));}

static int sdhci_cmd(uint16_t idx_flags, uint32_t arg, uint32_t *resp)
{
	for (int i = 0; i < 1000; i++) { if (!(R32(SDHC_PRESENT) & 3)) break; IODelay(10); }
	W32(SDHC_ARG, arg);
	W16(SDHC_CMD, idx_flags);
	for (int i = 0; i < 10000; i++) {
		uint16_t n = R16(SDHC_NINT), e = R16(SDHC_EINT);
		if (e) { W16(SDHC_EINT, e); LOG("CMD err %#x", e); return -1; }
		if (n & 1) { W16(SDHC_NINT, 1); break; }
		IODelay(10);
	}
	if (resp) {
		resp[0] = R32(SDHC_RESP0); resp[1] = R32(SDHC_RESP0+4);
		resp[2] = R32(SDHC_RESP0+8); resp[3] = R32(SDHC_RESP0+12);
	}
	return 0;
}

/* PIO single-block read (CMD17). */
static int sdhci_read_block(uint32_t lba, void *dst)
{
	W16(SDHC_BSIZE, 512);
	W16(SDHC_BCOUNT, 1);
	W16(SDHC_TMODE, 0x0010); /* read, no DMA, single */
	if (sdhci_cmd(0x113a, lba, NULL)) return -1; /* CMD17, data present */
	uint8_t *p = dst;
	for (int i = 0; i < 10000; i++) {
		uint16_t n = R16(SDHC_NINT), e = R16(SDHC_EINT);
		if (e) { W16(SDHC_EINT, e); return -1; }
		if (n & 0x20) { /* Buffer Read Ready */
			W16(SDHC_NINT, 0x20);
			for (int w = 0; w < 512 / 4; w++) {
				uint32_t d = R32(SDHC_BUF);
				p[0] = d; p[1] = d >> 8; p[2] = d >> 16; p[3] = d >> 24; p += 4;
			}
			break;
		}
		IODelay(10);
	}
	for (int i = 0; i < 10000; i++) {
		uint16_t n = R16(SDHC_NINT);
		if (n & 0x02) { W16(SDHC_NINT, 0x02); return 0; } /* Transfer Complete */
		IODelay(10);
	}
	return -1;
}

static int sdhci_xfer(void *ctx, uint64_t lba, uint32_t n, void *va, int write)
{
	(void)ctx;
	if (write) return -1;
	uint8_t *p = va;
	while (n--) {
		if (sdhci_read_block((uint32_t)lba, p)) return -1;
		p += 512; lba++;
	}
	return 0;
}

static void sdhci_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_sdhci_mode", 0);
	hc = ml_io_map((vm_offset_t)SDHCI_PA, 0x1000);
	if (!hc) { LOG("map fail"); return; }
	uint32_t pst = R32(SDHC_PRESENT), caps = R32(SDHC_CAPS);
	LOG("HC @ %#llx ver %u caps %#x present %#x card=%u",
	    SDHCI_PA, R16(SDHC_HOSTVER), caps, pst, !!(pst & PRESENT_CARD));
	if (!(pst & PRESENT_CARD)) { LOG("no card"); return; }
	if (mode == 0) { LOG("probe-only"); return; }

	W8(SDHC_SWRST, 1);
	for (int i = 0; i < 100 && (R8(SDHC_SWRST) & 1); i++) IODelay(100);
	if (R8(SDHC_SWRST) & 1) { LOG("reset stuck"); return; }
	W8(SDHC_PWRCTL, 0x0f);
	W16(SDHC_CLKCTL, 0x0005);
	for (int i = 0; i < 100; i++) { if (R16(SDHC_CLKCTL) & 2) break; IODelay(100); }
	W16(SDHC_CLKCTL, R16(SDHC_CLKCTL) | 4);

	uint32_t resp[4] = {0};
	if (sdhci_cmd(0x0000, 0, NULL)) return;
	if (sdhci_cmd(0x081a, 0x1aa, resp)) return;
	LOG("CMD8 resp %#x", resp[0]);
	for (int i = 0; i < 100; i++) {
		if (sdhci_cmd(0x371a, 0, NULL)) return;
		if (sdhci_cmd(0x2902, 0x40300000, resp)) return;
		if (resp[0] & (1u << 31)) break;
		IOSleep(10);
	}
	card_ocr = resp[0];
	LOG("ACMD41 OCR %#x", card_ocr);
	if (sdhci_cmd(0x0209, 0, resp)) return;
	if (sdhci_cmd(0x031a, 0, resp)) return;
	rca = resp[0] & 0xffff0000;
	LOG("RCA %#x", rca);
	if (sdhci_cmd(0x0915, rca, resp)) return;
	/* CSD v2: c_size in bits 69:48 across resp words — rough */
	uint32_t c_size = ((resp[1] & 0x3f) << 16) | (resp[2] >> 16);
	block_count = (c_size + 1) * 1024; /* 512-byte blocks */
	LOG("CSD[0..3] %#x %#x %#x %#x -> ~%u blocks", resp[0], resp[1], resp[2], resp[3], block_count);
	if (sdhci_cmd(0x071b, rca, NULL)) return;
	LOG("card selected");

	if (mode < 2) { LOG("READ path: set lapis_sdhci_mode=2"); return; }

	uint8_t *probe = IOMallocZero(512);
	if (!probe || sdhci_read_block(0, probe)) {
		LOG("CMD17 LBA0 failed"); if (probe) IOFree(probe, 512); return;
	}
	LOG("LBA0 %02x%02x%02x%02x ...", probe[0], probe[1], probe[2], probe[3]);
	IOFree(probe, 512);

	static struct lapis_disk disk;
	ksnprintf(disk.name, sizeof(disk.name), "sd0");
	disk.bs = 512; disk.nblocks = block_count ? block_count : 1;
	disk.writable = 0; disk.xfer = sdhci_xfer; disk.ctx = NULL;
	disk.max_blocks = 1;
	blk_register(&disk);
	LOG("published sd0 via LapisBlk (%u blocks)", block_count);
}
void lapis_sdhci_start(void){drv_thread(sdhci_thread,NULL);}
