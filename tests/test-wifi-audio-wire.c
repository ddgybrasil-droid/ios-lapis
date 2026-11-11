#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../drivers/wifi/mhi_boot.h"
#include "../drivers/wifi/qmi_wlfw.h"
#include "../drivers/wifi/qrtr_wire.h"
#include "../drivers/audio/gpr_wire.h"

static int fail;
static void expect(int cond, const char *msg)
{
	if (!cond) { fprintf(stderr, "FAIL %s\n", msg); fail = 1; }
}

int main(void)
{
	struct lapis_bhi_image img;
	struct lapis_qrtr_packet pkt, out;
	unsigned char qmi[16], body[4] = {1, 2, 3, 4}, frame[64], graph[4] = {0x11, 0x22, 0x33, 0x44};
	unsigned char resp[14];
	uint8_t apm[64];
	uint32_t n = 0;
	size_t written = 0;

	expect(lapis_bhi_window_valid(0, 0x10000) == 0, "bhi offset 0");
	expect(lapis_bhi_window_valid(0x100, 0x10000) == 1, "bhi window");
	expect(lapis_bhi_image(0, 128, 1, &img) != 0, "bhi pa");
	expect(lapis_bhi_image(0x1000, 128, 0, &img) != 0, "bhi session");
	expect(lapis_bhi_image(0x1000, LAPIS_BHI_IMAGE_MAX + 1u, 7, &img) != 0, "bhi size");
	expect(lapis_bhi_image(0xabc000, 7458880u, 0x51u, &img) == 0, "bhi image");
	expect(img.addr_lo == 0xabc000u && img.addr_hi == 0 && img.size == 7458880u &&
	       img.session == 0x51u, "bhi fields");

	expect(lapis_wlfw_host_cap_body_ok(0) != 0, "empty host-cap");
	expect(lapis_wlfw_host_cap_body_ok(8) == 0, "non-empty host-cap length");
	expect(lapis_qmi_header(qmi, LAPIS_QMI_RESPONSE, 1, LAPIS_WLFW_HOST_CAP, 7) == 0, "qmi hdr");
	memset(resp, 0, sizeof(resp));
	memcpy(resp, qmi, 7);
	resp[7] = LAPIS_QMI_RESULT_TLV;
	resp[8] = 4;
	resp[9] = 0;
	expect(lapis_qmi_succeeded(resp, sizeof(resp)) == 0, "qmi success");
	resp[10] = 1;
	expect(lapis_qmi_succeeded(resp, sizeof(resp)) != 0, "qmi failure");
	resp[0] = LAPIS_QMI_REQUEST;
	resp[10] = 0;
	expect(lapis_qmi_succeeded(resp, sizeof(resp)) != 0, "qmi request is not success");
	expect(lapis_qmi_succeeded(resp, 6) != 0, "qmi short");

	memset(&pkt, 0, sizeof(pkt));
	pkt.type = LAPIS_QRTR_DATA;
	pkt.src_node = 1;
	pkt.src_port = 2;
	pkt.dst_node = 3;
	pkt.dst_port = 4;
	pkt.payload = body;
	pkt.payload_len = 4;
	expect(lapis_qrtr_encode_v1(frame, sizeof(frame), &pkt, &written) == 0 && written == 36,
	       "qrtr encode");
	expect(lapis_qrtr_decode(frame, written, &out) == 0, "qrtr decode");
	expect(out.type == 1 && out.src_port == 2 && out.dst_port == 4 && out.payload_len == 4 &&
	       out.payload[3] == 4, "qrtr fields");
	frame[0] = 3;
	frame[1] = 1;
	frame[2] = 0;
	frame[3] = 0;
	frame[4] = 4;
	frame[5] = frame[6] = frame[7] = 0;
	frame[8] = 1; frame[9] = 0; frame[10] = 2; frame[11] = 0;
	frame[12] = 3; frame[13] = 0; frame[14] = 0xfe; frame[15] = 0xff;
	memcpy(frame + 16, body, 4);
	expect(lapis_qrtr_decode(frame, 20, &out) == 0 && out.dst_port == LAPIS_QRTR_CTRL_PORT,
	       "qrtr v2 control port");

	expect(lapis_apm_packet(apm, sizeof(apm), LAPIS_APM_GRAPH_OPEN, 1, 2, 1, NULL, 0, &n) != 0,
	       "empty graph");
	expect(lapis_apm_packet(apm, sizeof(apm), LAPIS_APM_GRAPH_OPEN, 9, 2, 1, graph, 4, &n) == 0 &&
	       n == 44, "graph packet size");
	expect(apm[0] == 0x60 && apm[1] == 0x2c && apm[4] == LAPIS_GPR_DOMAIN_ADSP &&
	       apm[5] == LAPIS_GPR_DOMAIN_APPS, "gpr words");
	expect(apm[20] == 0x00 && apm[21] == 0x10 && apm[22] == 0x00 && apm[23] == 0x01, "opcode");
	expect(apm[36] == 4 && apm[40] == 0x11 && apm[43] == 0x44, "apm payload");
	if (fail) return 1;
	printf("PASS wifi-audio wire\n");
	return 0;
}
