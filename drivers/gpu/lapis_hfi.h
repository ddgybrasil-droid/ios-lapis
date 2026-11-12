#pragma once
/* GMU HFI header shared by Linux a6xx_hfi.h and the KGSL adreno_hfi.h used
 * for Adreno 7xx. Bits 7:0 are the id, 15:8 the size in dwords, 19:16 the
 * type, 31:20 the sequence number. A successful return only describes the
 * dword. It does not allocate a queue or mean the GMU acknowledged it.
 * The caller supplies unsigned. */
#define LAPIS_HFI_CMD 0u
#define LAPIS_HFI_ACK 1u
#define LAPIS_HFI_INIT 0u
#define LAPIS_HFI_INIT_DWORDS 5u
#define LAPIS_HFI_F2H_ERROR 100u

static inline int lapis_hfi_header(unsigned id, unsigned dwords, unsigned type,
                                   unsigned seq, unsigned *out)
{
	if (!out || !dwords || dwords > 0xffu || id > 0xffu || type > 0xfu || seq > 0xfffu)
		return -1;
	*out = id | (dwords << 8) | (type << 16) | (seq << 20);
	return 0;
}

/* Non-legacy GMU pads the write index up to a multiple of 4 dwords. The
 * reservation includes that padding, because the published write loop stores
 * the pad after its free-space check. size is in dwords and must be a power
 * of two. One slot stays empty, matching CIRC_SPACE. */
static inline int lapis_hfi_reserve(unsigned write, unsigned read, unsigned size,
                                    unsigned dwords)
{
	unsigned need, space;
	if (!size || (size & (size - 1u)) || write >= size || read >= size ||
	    !dwords || dwords > 0xffu)
		return -1;
	need = (dwords + 3u) & ~3u;
	space = (read - (write + 1u)) & (size - 1u);
	return space >= need ? 0 : -1;
}

/* Response layout: header, the command header being acked, then the error
 * dword. Id 100 is a firmware error. A zero error with a different sequence
 * number is not an ack of this command. */
static inline int lapis_hfi_ack_ok(const unsigned *msg, unsigned dwords, unsigned expect_seq)
{
	unsigned size;
	if (!msg || dwords < 3u || expect_seq > 0xfffu) return -1;
	size = (msg[0] >> 8) & 0xffu;
	if (!size || size < 3u || size > dwords) return -1;
	if ((msg[0] & 0xffu) == LAPIS_HFI_F2H_ERROR) return -1;
	if (((msg[1] >> 20) & 0xfffu) != expect_seq || msg[2] != 0u) return -1;
	return 0;
}
