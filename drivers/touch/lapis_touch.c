/*
 * LapisTouch — Novatek NT36532 over QUPv3 GENI SPI.
 *
 * dtb VERIFIED: spi@a90000 compatible qcom,spi-geni; child novatek@0
 *   compatible "novatek,NVT-ts-spi"; spi-max-frequency 0x927c00 (9.6 MHz)
 *   reset-gpio / irq-gpio via tlmm (gpio 0xa1 / 0xa2 → 161/162)
 *
 * Boot-args:
 *   lapis_touch_mode=0  probe SE status (default)
 *   lapis_touch_mode=1  one SPI identify transfer (0x71)
 *   lapis_touch_mode=2  poll touch reports and log coordinates
 *   lapis_touch_se=<pa> override SE base (default 0xa90000)
 */
#include "../include/kern.h"
#define LOG(fmt, ...) klog("LapisTouch: " fmt "\n", ##__VA_ARGS__)
#define QUPV3_SE_BASE 0x00a90000ull   /* VERIFIED spi@a90000 */
#define GENI_STATUS       0x40
#define GENI_M_CMD0       0x600
#define GENI_M_IRQ_STATUS 0x610
#define GENI_M_IRQ_CLEAR  0x618
#define GENI_TX_FIFOn     0x700
#define GENI_RX_FIFOn     0x780
#define GENI_TX_WATERMARK 0x50c
#define GENI_RX_WATERMARK 0x510
#define GENI_FORCE_DEFAULT 0x14
#define GENI_OUTPUT_CTRL  0x24
#define GENI_CGC_CTRL     0x28

static vm_offset_t se;
static uint32_t R(uint32_t o){return rd32(se+o);}
static void W(uint32_t o,uint32_t v){wr32(se+o,v);}

static int geni_spi_xfer(const uint8_t *tx, uint8_t *rx, unsigned n)
{
	for (int i = 0; i < 1000; i++) { if (!(R(GENI_STATUS) & 1)) break; IODelay(10); }
	W(GENI_M_IRQ_CLEAR, 0xffffffff);
	for (unsigned i = 0; i < n; i += 4) {
		uint32_t w = 0;
		for (unsigned b = 0; b < 4 && i + b < n; b++) w |= (uint32_t)tx[i + b] << (8 * b);
		W(GENI_TX_FIFOn, w);
	}
	/* OPCODE TX_RX << 27 | length */
	W(GENI_M_CMD0, (0x1u << 27) | (n & 0xffffff));
	int ok = 0;
	for (int i = 0; i < 10000; i++) {
		uint32_t st = R(GENI_M_IRQ_STATUS);
		if (st & (1u << 0)) { W(GENI_M_IRQ_CLEAR, st); ok = 1; break; }
		IODelay(10);
	}
	if (!ok) { LOG("SPI xfer timeout STATUS %#x IRQ %#x", R(GENI_STATUS), R(GENI_M_IRQ_STATUS)); return -1; }
	for (unsigned i = 0; i < n; i += 4) {
		uint32_t w = R(GENI_RX_FIFOn);
		for (unsigned b = 0; b < 4 && i + b < n; b++) if (rx) rx[i + b] = (uint8_t)(w >> (8 * b));
	}
	return 0;
}

/* Novatek host SPI: write-then-read with 0x71 (read) / 0x70 (write) prefixes. */
static int nvt_read(uint16_t addr, uint8_t *out, unsigned n)
{
	uint8_t tx[8 + 64]; uint8_t rx[8 + 64];
	if (n > 64) n = 64;
	bzero(tx, sizeof(tx)); bzero(rx, sizeof(rx));
	tx[0] = 0x71; tx[1] = (uint8_t)(addr >> 8); tx[2] = (uint8_t)addr;
	if (geni_spi_xfer(tx, rx, 3 + n)) return -1;
	memcpy(out, rx + 3, n);
	return 0;
}

static void touch_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_touch_mode", 0);
	uint64_t pa = QUPV3_SE_BASE;
	PE_parse_boot_argn("lapis_touch_se", &pa, sizeof(pa));
	se = ml_io_map((vm_offset_t)pa, 0x4000);
	if (!se) { LOG("QUP SE map fail"); return; }
	LOG("QUPv3 SE @ %#llx STATUS %#x (Novatek NT36532 @ spi, irq gpio 162)", pa, R(GENI_STATUS));
	if (mode == 0) { LOG("probe-only"); return; }

	if (mode >= 1) {
		uint8_t id[6] = {0};
		if (nvt_read(0x0000, id, 6)) LOG("identify xfer fail (clocks/CS may need GCC+pinctrl)");
		else LOG("NT id bytes %02x %02x %02x %02x %02x %02x", id[0],id[1],id[2],id[3],id[4],id[5]);
	}
	if (mode >= 2) {
		LOG("polling touch reports (protocol: finger count @ +0x00, XY pairs follow)");
		for (int round = 0; round < 20; round++) {
			uint8_t rpt[32] = {0};
			if (!nvt_read(0x1000, rpt, 32)) {
				unsigned nf = rpt[0] & 0x0f;
				if (nf) {
					for (unsigned f = 0; f < nf && f < 5; f++) {
						unsigned o = 1 + f * 6;
						unsigned x = rpt[o] | ((rpt[o+1] & 0x0f) << 8);
						unsigned y = rpt[o+2] | ((rpt[o+3] & 0x0f) << 8);
						LOG("touch f%u x=%u y=%u (IOHID delivery: log-only)", f, x, y);
					}
				}
			}
			IOSleep(50);
		}
		LOG("touch poll done; full IRQ+IOHID path needs GIC SPI for gpio 162");
	}
}
void lapis_touch_start(void){drv_thread(touch_thread,NULL);}
