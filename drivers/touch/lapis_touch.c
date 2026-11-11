/*
 * LapisTouch — Novatek NT36532 over QUPv3 GENI SPI.
 *
 * The serial engine is spi@a90000. Reset is TLMM gpio 161 and the interrupt
 * is gpio 162; this file does not program TLMM, so a transfer can time out
 * until the pins and clocks are already up. Coordinates use the 12-bit
 * Novatek record and are logged only after the packet checksum matches.
 * IOHID is not fed.
 *
 *   lapis_touch_mode=0  read GENI status (default)
 *   lapis_touch_mode=1  read the trim-id bytes at 0x1FB104
 *   lapis_touch_mode=2  one event-buffer read and a checksummed finger parse
 *   lapis_touch_se=<pa> override the SE base (default 0xa90000)
 */
#include "../include/kern.h"
#include "nvt_nt36532.h"
#define LOG(fmt, ...) klog("LapisTouch: " fmt "\n", ##__VA_ARGS__)
#define QUPV3_SE_BASE 0x00a90000ull

static vm_offset_t se;
static uint32_t R(uint32_t o){return rd32(se+o);}
static void W(uint32_t o,uint32_t v){wr32(se+o,v);}

static void geni_push_word(const unsigned char *tx, unsigned n, unsigned *sent)
{
	uint32_t w = 0;
	unsigned b;
	for (b = 0; b < 4 && *sent + b < n; b++)
		w |= (uint32_t)tx[*sent + b] << (8 * b);
	W(LAPIS_GENI_TX_FIFO, w);
	*sent += b;
}

static void geni_pull_word(unsigned char *rx, unsigned n, unsigned *got)
{
	uint32_t w = R(LAPIS_GENI_RX_FIFO);
	unsigned b;
	for (b = 0; b < 4 && *got + b < n; b++)
		rx[*got + b] = (unsigned char)(w >> (8 * b));
	*got += b;
}

static int geni_spi_xfer(const unsigned char *tx, unsigned char *rx, unsigned n, int duplex)
{
	unsigned depth = nvt_tx_fifo_depth_words(R(LAPIS_GENI_HW_PARAM_0));
	unsigned sent = 0, got = 0;
	unsigned opcode = duplex ? (LAPIS_GENI_SPI_TX_ONLY | LAPIS_GENI_SPI_RX_ONLY)
	                         : LAPIS_GENI_SPI_TX_ONLY;
	int i;
	if (!tx || !n || n > 160u || !depth) return -1;
	if (duplex && !rx) return -1;
	for (i = 0; i < 1000; i++) {
		if (!(R(LAPIS_GENI_STATUS) & LAPIS_GENI_M_CMD_ACTIVE)) break;
		IODelay(10);
	}
	if (R(LAPIS_GENI_STATUS) & LAPIS_GENI_M_CMD_ACTIVE) return -1;
	W(LAPIS_GENI_M_IRQ_CLEAR, 0xffffffffu);
	W(LAPIS_GENI_TX_TRANS_LEN, n);
	if (duplex) W(LAPIS_GENI_RX_TRANS_LEN, n);
	while (sent < n) {
		unsigned wc = R(LAPIS_GENI_TX_FIFO_STATUS) & 0x0fffffffu;
		if (wc + 1u >= depth) break;
		geni_push_word(tx, n, &sent);
	}
	W(LAPIS_GENI_M_CMD0, lapis_geni_m_cmd(opcode));
	for (i = 0; i < 20000; i++) {
		uint32_t st = R(LAPIS_GENI_M_IRQ_STATUS);
		unsigned tx_wc, rx_wc;
		if (st & (LAPIS_GENI_M_CMD_ILLEGAL | LAPIS_GENI_M_CMD_FAILURE)) {
			W(LAPIS_GENI_M_IRQ_CLEAR, st);
			return -1;
		}
		if (sent < n) {
			tx_wc = R(LAPIS_GENI_TX_FIFO_STATUS) & 0x0fffffffu;
			if (tx_wc + 1u < depth) geni_push_word(tx, n, &sent);
		}
		if (duplex && got < n) {
			rx_wc = R(LAPIS_GENI_RX_FIFO_STATUS) & 0x01ffffffu;
			if (rx_wc) geni_pull_word(rx, n, &got);
		}
		if ((st & LAPIS_GENI_M_CMD_DONE) && sent >= n && (!duplex || got >= n)) {
			W(LAPIS_GENI_M_IRQ_CLEAR, st);
			return 0;
		}
		IODelay(10);
	}
	return -1;
}

static int nvt_page(unsigned addr)
{
	unsigned char tx[NVT_PAGE_LEN];
	if (nvt_build_page(addr, tx)) return -1;
	return geni_spi_xfer(tx, NULL, NVT_PAGE_LEN, 0);
}

static int nvt_read(unsigned addr, unsigned char *payload, unsigned n)
{
	unsigned char tx[160], rx[160];
	unsigned xfer = 0;
	if (n > 128u) return -1;
	if (nvt_build_read(addr, n, tx, sizeof(tx), &xfer)) return -1;
	if (geni_spi_xfer(tx, rx, xfer, 1)) return -1;
	return nvt_take_payload(rx, xfer, payload, n);
}

static void touch_thread(void *a, int w)
{
	(void)a;(void)w;
	int mode = drv_bootarg("lapis_touch_mode", 0);
	uint64_t pa = QUPV3_SE_BASE;
	PE_parse_boot_argn("lapis_touch_se", &pa, sizeof(pa));
	se = ml_io_map((vm_offset_t)pa, 0x4000);
	if (!se) { LOG("QUP SE map fail"); return; }
	LOG("QUPv3 SE @ %#llx STATUS %#x HW_PARAM_0 %#x", pa,
	    R(LAPIS_GENI_STATUS), R(LAPIS_GENI_HW_PARAM_0));
	if (mode == 0) { LOG("probe-only"); return; }

	if (mode >= 1) {
		unsigned char id[4];
		if (nvt_page(NVT_CHIP_TRIM) || nvt_read(NVT_CHIP_TRIM, id, 4))
			LOG("trim read failed (GCC clock or TLMM CS/reset may be down)");
		else
			LOG("trim @ %#x raw %02x %02x %02x %02x", NVT_CHIP_TRIM,
			    id[0], id[1], id[2], id[3]);
	}
	if (mode >= 2) {
		unsigned char pkt[NVT_POINT_BYTES];
		struct nvt_finger fingers[NVT_FINGERS];
		unsigned n = 0;
		if (nvt_page(NVT_EVENT_BUF) || nvt_read(NVT_EVENT_BUF, pkt, NVT_POINT_BYTES))
			LOG("event read failed");
		else if (nvt_parse_fingers(pkt, NVT_POINT_BYTES, fingers, NVT_FINGERS, &n))
			LOG("event checksum mismatch; coordinates not used");
		else {
			unsigned f;
			LOG("12-bit records, checksum ok, fingers %u", n);
			for (f = 0; f < n && f < NVT_FINGERS; f++)
				LOG("finger id %u x %u y %u width %u",
				    fingers[f].id, fingers[f].x, fingers[f].y, fingers[f].width);
		}
	}
}
void lapis_touch_start(void){drv_thread(touch_thread,NULL);}
