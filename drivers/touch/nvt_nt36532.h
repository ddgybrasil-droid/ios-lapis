/*
 * Novatek NT36532 SPI framing and 12-bit finger records.
 *
 * Page select and the indexed read match the public NT36532 driver
 * (nvt_set_page / CTP_SPI_READ): a 3-byte 0xFF page command, then a read whose
 * command byte is the low 7 bits of the address. The controller answers after
 * one dummy byte, so payload byte 0 is SPI RX byte 2.
 * Both published NT36532 maps use event buffer 0x125800. The 0x1FB register
 * block reads the trim id at 0x1FB104.
 * Finger records are the non-super-resolution layout: 6 bytes, 12-bit X/Y.
 * A 16-bit super-resolution build uses a different layout and is not parsed.
 * The caller supplies the integer types (kern.h in the driver, stdint.h on the host).
 */
#ifndef LAPIS_NVT_NT36532_H
#define LAPIS_NVT_NT36532_H

#define NVT_EVENT_BUF   0x125800u
#define NVT_CHIP_TRIM   0x1FB104u
#define NVT_PAGE_LEN    3u
#define NVT_DUMMY_BYTES 1u
#define NVT_POINT_BYTES 65u
#define NVT_FINGERS     10u
#define NVT_PANEL_X     2944u
#define NVT_PANEL_Y     1840u

#define LAPIS_GENI_STATUS          0x40u
#define LAPIS_GENI_M_CMD0          0x600u
#define LAPIS_GENI_M_IRQ_STATUS    0x610u
#define LAPIS_GENI_M_IRQ_CLEAR     0x618u
#define LAPIS_GENI_TX_FIFO         0x700u
#define LAPIS_GENI_RX_FIFO         0x780u
#define LAPIS_GENI_TX_FIFO_STATUS  0x800u
#define LAPIS_GENI_RX_FIFO_STATUS  0x804u
#define LAPIS_GENI_TX_TRANS_LEN    0x26cu
#define LAPIS_GENI_RX_TRANS_LEN    0x270u
#define LAPIS_GENI_HW_PARAM_0      0xe24u
#define LAPIS_GENI_SPI_TX_ONLY     1u
#define LAPIS_GENI_SPI_RX_ONLY     2u
#define LAPIS_GENI_M_OPCODE_SHIFT  27u
#define LAPIS_GENI_M_CMD_DONE      1u
#define LAPIS_GENI_M_CMD_ILLEGAL   4u
#define LAPIS_GENI_M_CMD_FAILURE   8u
#define LAPIS_GENI_M_CMD_ACTIVE    1u

struct nvt_finger {
	unsigned id;
	unsigned x;
	unsigned y;
	unsigned width;
};

static inline unsigned lapis_geni_m_cmd(unsigned opcode)
{
	return opcode << LAPIS_GENI_M_OPCODE_SHIFT;
}

static inline unsigned nvt_tx_fifo_depth_words(unsigned hw_param_0)
{
	/* QUPv3 on this SoC is the 256-byte depth encoding, bits 23:16. */
	return (hw_param_0 >> 16) & 0xffu;
}

static inline int nvt_build_page(unsigned addr, unsigned char out[NVT_PAGE_LEN])
{
	if (!out) return -1;
	out[0] = 0xffu;
	out[1] = (unsigned char)((addr >> 15) & 0xffu);
	out[2] = (unsigned char)((addr >> 7) & 0xffu);
	return 0;
}

/* tx holds the full-duplex buffer. xfer_len is payload + command + dummy. */
static inline int nvt_build_read(unsigned addr, unsigned payload,
                                 unsigned char *tx, unsigned tx_cap, unsigned *xfer_len)
{
	unsigned n, i;
	if (!tx || !xfer_len || !payload || payload > 128u) return -1;
	n = payload + 1u + NVT_DUMMY_BYTES;
	if (n > tx_cap) return -1;
	for (i = 0; i < n; i++) tx[i] = 0;
	tx[0] = (unsigned char)(addr & 0x7fu);
	*xfer_len = n;
	return 0;
}

static inline int nvt_take_payload(const unsigned char *rx, unsigned xfer_len,
                                   unsigned char *dst, unsigned payload)
{
	unsigned i;
	if (!rx || !dst || xfer_len < payload + 2u) return -1;
	for (i = 0; i < payload; i++) dst[i] = rx[2u + i];
	return 0;
}

static inline int nvt_point_checksum(const unsigned char *data)
{
	unsigned sum = 0;
	unsigned i;
	if (!data) return -1;
	for (i = 0; i < NVT_POINT_BYTES - 1u; i++) sum += data[i];
	sum = (~sum + 1u) & 0xffu;
	return sum == data[NVT_POINT_BYTES - 1u] ? 0 : -1;
}

/* data[0] is the first byte after the command echo. Checksum covers data[0..64]. */
static inline int nvt_parse_fingers(const unsigned char *data, unsigned len,
                                    struct nvt_finger *out, unsigned cap, unsigned *n)
{
	unsigned count = 0;
	unsigned i;
	if (!data || !n || len < NVT_POINT_BYTES) return -1;
	if (nvt_point_checksum(data)) return -1;
	for (i = 0; i < NVT_FINGERS; i++) {
		unsigned pos = 6u * i;
		unsigned id, ev, x, y, w;
		id = data[pos] >> 3;
		ev = data[pos] & 7u;
		if (!id || id > NVT_FINGERS) continue;
		if (ev != 1u && ev != 2u) continue;
		x = ((unsigned)data[pos + 1u] << 4) | (data[pos + 3u] >> 4);
		y = ((unsigned)data[pos + 2u] << 4) | (data[pos + 3u] & 0x0fu);
		w = data[pos + 4u];
		if (x >= NVT_PANEL_X || y >= NVT_PANEL_Y) continue;
		if (out && count < cap) {
			out[count].id = id;
			out[count].x = x;
			out[count].y = y;
			out[count].width = w ? w : 1u;
		}
		count++;
	}
	*n = count;
	return 0;
}
#endif
