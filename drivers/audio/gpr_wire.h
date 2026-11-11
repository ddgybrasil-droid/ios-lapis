#pragma once
/* GPR header: version 0 in bits 3:0, header size 6 words in bits 7:4,
 * packet size in bits 31:8. Domain word is destination then source.
 * An APM command adds the 16-byte apm_cmd_header. GRAPH_OPEN with an empty
 * payload is rejected: that opcode's payload is the graph. Nothing here
 * transmits the packet. Caller supplies uint8_t and uint32_t. */
#define LAPIS_GPR_DOMAIN_APPS 3u
#define LAPIS_GPR_DOMAIN_ADSP 2u
#define LAPIS_APM_CMD_HDR_SIZE 16u
#define LAPIS_APM_GRAPH_OPEN 0x01001000u
struct lapis_gpr_header { uint8_t bytes[24]; };

static inline void lapis_gpr_le32(uint8_t *p, uint32_t v)
{
	unsigned i;
	for (i = 0; i < 4u; i++) p[i] = (uint8_t)(v >> (8u * i));
}

static inline int lapis_gpr_header_init(struct lapis_gpr_header *h,
	uint32_t packet_size, uint8_t src_domain, uint8_t dst_domain,
	uint32_t src_port, uint32_t dst_port, uint32_t token, uint32_t opcode)
{
	if (!h || packet_size < sizeof(*h) || packet_size > 0xffffffu) return -1;
	lapis_gpr_le32(h->bytes + 0, (packet_size << 8) | (6u << 4));
	lapis_gpr_le32(h->bytes + 4, dst_domain | ((uint32_t)src_domain << 8));
	lapis_gpr_le32(h->bytes + 8, src_port);
	lapis_gpr_le32(h->bytes + 12, dst_port);
	lapis_gpr_le32(h->bytes + 16, token);
	lapis_gpr_le32(h->bytes + 20, opcode);
	return 0;
}

static inline int lapis_apm_packet(uint8_t *buf, uint32_t cap, uint32_t opcode,
	uint32_t token, uint32_t src_port, uint32_t dst_port,
	const void *payload, uint32_t payload_size, uint32_t *written)
{
	uint32_t total, i;
	struct lapis_gpr_header *h;
	uint8_t *cmd;
	const uint8_t *src;
	if (!buf || !written) return -1;
	if (opcode == LAPIS_APM_GRAPH_OPEN && !payload_size) return -1;
	if (payload_size && !payload) return -1;
	if (payload_size > 0x100000u) return -1;
	total = 24u + LAPIS_APM_CMD_HDR_SIZE + payload_size;
	if (total < payload_size || total > cap) return -1;
	h = (struct lapis_gpr_header *)buf;
	if (lapis_gpr_header_init(h, total, LAPIS_GPR_DOMAIN_APPS, LAPIS_GPR_DOMAIN_ADSP,
	                          src_port, dst_port, token, opcode))
		return -1;
	cmd = buf + 24;
	for (i = 0; i < LAPIS_APM_CMD_HDR_SIZE; i++) cmd[i] = 0;
	lapis_gpr_le32(cmd + 12, payload_size);
	src = payload;
	for (i = 0; i < payload_size; i++) cmd[LAPIS_APM_CMD_HDR_SIZE + i] = src[i];
	*written = total;
	return 0;
}
