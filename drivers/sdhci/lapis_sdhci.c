/*
 * LapisSDHCI - SD card host (sdhci@8804000) for the IPSW install path.
 *
 * TB520FU: qcom,sdhci-msm-v5 @ 0x8804000, IRQ SPI 207 / 223, cd-gpio, bus-width 4,
 * SMMU SID 0x540.  Default = probe-only (read version/caps/present-state).
 * Real init + data xfer behind lapis_sdhci_mode=1 (HYPOTHESIS until hw log: clocks
 * may already be on if Android left the controller powered; otherwise GCC/pinctrl
 * bring-up is needed and is NOT DONE here).
 *
 * IPSW procedure (see FINAL deliverable): dd the lapisfs image onto an SD card,
 * boot with this driver in mode=1; LapisBlk will publish /dev/diskN and lapisfs
 * will pick up LAPISFS1 automatically.
 */
#include "../include/kern.h"
#include "../include/blk.h"

#define LOG(fmt, ...) klog("LapisSDHCI: " fmt "\n", ##__VA_ARGS__)
#define SDHCI_PA   0x08804000ull
#define SDHCI_SZ   0x1000u

/* SDHCI registers (SD Host Controller Simplified Spec) */
#define SDHC_SDMA     0x00
#define SDHC_BSIZE    0x04
#define SDHC_BCOUNT   0x06
#define SDHC_ARG      0x08
#define SDHC_TMODE    0x0c
#define SDHC_CMD      0x0e
#define SDHC_RESP0    0x10
#define SDHC_BUFDAT   0x20
#define SDHC_PRESENT  0x24
#define SDHC_HOSTCTL  0x28
#define SDHC_PWRCTL   0x29
#define SDHC_CLKCTL   0x2c
#define SDHC_TIMEOUT  0x2e
#define SDHC_SWRST    0x2f
#define SDHC_NINT     0x30
#define SDHC_EINT     0x32
#define SDHC_NINTEN   0x34
#define SDHC_EINTEN   0x36
#define SDHC_CAPS     0x40
#define SDHC_CAPS2    0x44
#define SDHC_HOSTVER  0xfe

#define PRESENT_CARD  (1u << 16)
#define PRESENT_STABLE (1u << 17)
#define PRESENT_CMD_INH (1u << 0)
#define PRESENT_DAT_INH (1u << 1)

static vm_offset_t hc;
static inline uint32_t R32(uint32_t o) { return rd32(hc + o); }
static inline uint16_t R16(uint32_t o) { return (uint16_t)rd32(hc + (o & ~3u)) >> ((o & 3u) * 8); }
static inline uint8_t  R8(uint32_t o)  { return (uint8_t)(rd32(hc + (o & ~3u)) >> ((o & 3u) * 8)); }
static inline void W32(uint32_t o, uint32_t v) { wr32(hc + o, v); }
static inline void W8(uint32_t o, uint8_t v) {
	uint32_t a = o & ~3u, s = (o & 3u) * 8;
	uint32_t x = rd32(hc + a); x = (x & ~(0xffu << s)) | ((uint32_t)v << s); wr32(hc + a, x);
}
static inline void W16(uint32_t o, uint16_t v) {
	uint32_t a = o & ~3u, s = (o & 3u) * 8;
	uint32_t x = rd32(hc + a); x = (x & ~(0xffffu << s)) | ((uint32_t)v << s); wr32(hc + a, x);
}

static void sdhci_thread(void *arg, int wr)
{
	(void)arg; (void)wr;
	int mode = drv_bootarg("lapis_sdhci_mode", 0);   /* 0 = probe */
	hc = ml_io_map((vm_offset_t)SDHCI_PA, SDHCI_SZ);
	if (!hc) { LOG("ml_io_map failed"); return; }
	uint16_t ver = R16(SDHC_HOSTVER);
	uint32_t caps = R32(SDHC_CAPS), caps2 = R32(SDHC_CAPS2), pst = R32(SDHC_PRESENT);
	LOG("HC @ %#llx ver %u.%u caps %#x caps2 %#x present %#x card=%u",
	    SDHCI_PA, (ver >> 4) & 0xff, ver & 0xf, caps, caps2, pst, !!(pst & PRESENT_CARD));
	if (!(pst & PRESENT_CARD)) { LOG("no card (or CD not yet stable)"); return; }
	if (mode == 0) { LOG("probe-only (set lapis_sdhci_mode=1 for init+blk)"); return; }

	/* Soft reset + weakest bring-up that works when clocks are already on. */
	W8(SDHC_SWRST, 0x01);
	for (int i = 0; i < 100 && (R8(SDHC_SWRST) & 1); i++) IODelay(100);
	if (R8(SDHC_SWRST) & 1) { LOG("software reset stuck (clocks off?)"); return; }
	W8(SDHC_PWRCTL, 0x0f);            /* 3.3V on */
	W16(SDHC_CLKCTL, 0x0005);         /* int clk en + stable check later */
	for (int i = 0; i < 100; i++) { if (R16(SDHC_CLKCTL) & 2) break; IODelay(100); }
	W16(SDHC_CLKCTL, R16(SDHC_CLKCTL) | 0x0004);  /* SD clock enable */
	LOG("clocks/power programmed (HYPOTHESIS: GCC already enabled by ABL/Android)");
	LOG("full CMD/data path NOT DONE this build - card detected only");
	/* A real CMD8/ACMD41/CMD2/CMD3/CMD9/CMD7 + READ_MULTIPLE sequence belongs here.
	 * Left as NOT DONE rather than half-working DMA against an unknown SMMU state. */
}

void lapis_sdhci_start(void) { drv_thread(sdhci_thread, NULL); }
