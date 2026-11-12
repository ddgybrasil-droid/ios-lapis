#include <stdio.h>
#include <string.h>
#include "../drivers/touch/nvt_nt36532.h"

static int fail;

static void expect(int cond, const char *msg)
{
	if (!cond) { fprintf(stderr, "FAIL %s\n", msg); fail = 1; }
}

static void checksum(unsigned char *data)
{
	unsigned sum = 0, i;
	for (i = 0; i < NVT_POINT_BYTES - 1u; i++) sum += data[i];
	data[NVT_POINT_BYTES - 1u] = (unsigned char)((~sum + 1u) & 0xffu);
}

int main(void)
{
	unsigned char page[3], tx[80], payload[NVT_POINT_BYTES], rx[80];
	unsigned xfer = 0, n = 0, i;
	struct nvt_finger fingers[4];

	expect(lapis_geni_m_cmd(LAPIS_GENI_SPI_TX_ONLY | LAPIS_GENI_SPI_RX_ONLY) == (3u << 27),
	       "duplex opcode");
	expect((lapis_geni_m_cmd(3u) & 0x07ffffffu) == 0, "length is not packed into M_CMD0");
	expect(nvt_tx_fifo_depth_words(0x00400000u) == 0x40u, "fifo depth bits 23:16");
	expect(nvt_build_page(NVT_EVENT_BUF, page) == 0, "page");
	expect(page[0] == 0xff && page[1] == 0x24 && page[2] == 0xb0, "event page bytes");
	expect(nvt_build_read(NVT_EVENT_BUF, NVT_POINT_BYTES, tx, sizeof(tx), &xfer) == 0, "read");
	expect(tx[0] == 0 && xfer == NVT_POINT_BYTES + 2u, "read framing");

	memset(payload, 0, sizeof(payload));
	payload[0] = (unsigned char)((1u << 3) | 1u);
	payload[1] = 0xb7;
	payload[2] = 0x3e;
	payload[3] = 0xc8;
	payload[4] = 5;
	checksum(payload);
	memset(rx, 0, sizeof(rx));
	rx[2] = 0x11;
	expect(nvt_take_payload(rx, xfer, payload, NVT_POINT_BYTES) == 0, "take");
	/* take overwrote the crafted packet; rebuild after a clean copy check */
	expect(payload[0] == 0x11, "payload starts at rx byte 2");

	memset(payload, 0, sizeof(payload));
	payload[0] = (unsigned char)((1u << 3) | 1u);
	payload[1] = 0xb7;
	payload[2] = 0x3e;
	payload[3] = 0xc8;
	payload[4] = 5;
	payload[6] = (unsigned char)((2u << 3) | 2u);
	payload[7] = 0x10;
	payload[8] = 0x20;
	payload[9] = 0x30;
	payload[10] = 0;
	checksum(payload);
	expect(nvt_parse_fingers(payload, NVT_POINT_BYTES, fingers, 4, &n) == 0, "parse");
	expect(n == 2 && fingers[0].id == 1 && fingers[0].x == 2940 && fingers[0].y == 1000 &&
	       fingers[0].width == 5, "finger 1");
	expect(fingers[1].id == 2 && fingers[1].x == 0x103 && fingers[1].y == 0x200 &&
	       fingers[1].width == 1, "finger 2 zero width becomes 1");
	payload[NVT_POINT_BYTES - 1u] ^= 1u;
	expect(nvt_parse_fingers(payload, NVT_POINT_BYTES, fingers, 4, &n) != 0, "bad checksum");
	for (i = 0; i < NVT_POINT_BYTES; i++) payload[i] = 0;
	checksum(payload);
	expect(nvt_parse_fingers(payload, NVT_POINT_BYTES, fingers, 4, &n) == 0 && n == 0,
	       "empty packet");
	if (fail) return 1;
	printf("PASS nvt\n");
	return 0;
}
