/*
 * QMI header and the standard response TLV, plus the WLFW ids used by ath12k
 * for WCN7850. The header is 7 bytes: type, transaction, message id, TLV
 * length. TLV 0x02 carries a u16 result and a u16 error. Result 0 and error 0
 * are the only success. Message 0x0034 is HOST_CAP; this file does not invent
 * its capability TLVs, so an empty body is not a request that can be sent.
 * The caller supplies the integer types.
 */
#ifndef LAPIS_QMI_WLFW_H
#define LAPIS_QMI_WLFW_H

#define LAPIS_QMI_REQUEST  0u
#define LAPIS_QMI_RESPONSE 2u
#define LAPIS_QMI_RESULT_TLV 0x02u
#define LAPIS_QMI_HDR_LEN 7u
#define LAPIS_WLFW_SERVICE_ID 0x45u
#define LAPIS_WLFW_HOST_CAP 0x0034u

static inline int lapis_qmi_header(unsigned char *out, unsigned type,
                                   unsigned txn, unsigned msg, unsigned tlv_len)
{
	if (!out || type > 0xffu || txn > 0xffffu || msg > 0xffffu || tlv_len > 0xffffu)
		return -1;
	out[0] = (unsigned char)type;
	out[1] = (unsigned char)txn;
	out[2] = (unsigned char)(txn >> 8);
	out[3] = (unsigned char)msg;
	out[4] = (unsigned char)(msg >> 8);
	out[5] = (unsigned char)tlv_len;
	out[6] = (unsigned char)(tlv_len >> 8);
	return 0;
}

/* 0 only when the message is a response and TLV 0x02 says success/none. */
static inline int lapis_qmi_succeeded(const unsigned char *msg, unsigned len)
{
	unsigned tlv_len, off;
	int found = 0;
	unsigned result = 1, error = 1;
	if (!msg || len < LAPIS_QMI_HDR_LEN || msg[0] != LAPIS_QMI_RESPONSE) return -1;
	tlv_len = (unsigned)msg[5] | ((unsigned)msg[6] << 8);
	if (tlv_len != len - LAPIS_QMI_HDR_LEN) return -1;
	off = LAPIS_QMI_HDR_LEN;
	while (off + 3u <= len) {
		unsigned t = msg[off];
		unsigned n = (unsigned)msg[off + 1u] | ((unsigned)msg[off + 2u] << 8);
		off += 3u;
		if (n > len - off) return -1;
		if (t == LAPIS_QMI_RESULT_TLV) {
			if (n != 4u) return -1;
			result = (unsigned)msg[off] | ((unsigned)msg[off + 1u] << 8);
			error = (unsigned)msg[off + 2u] | ((unsigned)msg[off + 3u] << 8);
			found = 1;
		}
		off += n;
	}
	if (off != len || !found) return -1;
	return (result == 0u && error == 0u) ? 0 : -1;
}

static inline int lapis_wlfw_host_cap_body_ok(unsigned tlv_len)
{
	/* HOST_CAP without TLVs advertises nothing and must not be transmitted. */
	return tlv_len ? 0 : -1;
}
#endif
