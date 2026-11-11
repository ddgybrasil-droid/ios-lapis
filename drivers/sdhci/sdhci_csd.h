/*
 * SD CSD capacity from the four SDHCI RESPONSE registers.
 *
 * RESPONSE+0 is resp_regs[0]. A 136-bit response is reordered to the SDHCI
 * high word first, then shifted by 8 bits so the CRC no longer occupies the
 * low byte. CSD v2 capacity is (C_SIZE+1)*512 KiB. CSD v1 capacity is
 * (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN. The result is a count of
 * 512-byte blocks. An unknown structure or a size that does not divide into
 * those blocks is rejected.
 */
#ifndef LAPIS_SDHCI_CSD_H
#define LAPIS_SDHCI_CSD_H

static inline unsigned lapis_csd_bits(const unsigned csd[4], unsigned start, unsigned size)
{
	unsigned mask = size >= 32u ? 0xffffffffu : ((1u << size) - 1u);
	unsigned off = 3u - start / 32u;
	unsigned sh = start & 31u;
	unsigned res = csd[off] >> sh;
	if (size + sh > 32u)
		res |= csd[off - 1u] << (32u - sh);
	return res & mask;
}

static inline int lapis_sd_blocks_from_csd(const unsigned csd[4], unsigned *blocks)
{
	unsigned structure, c_size, bytes_hi, bytes_lo;
	if (!csd || !blocks) return -1;
	structure = lapis_csd_bits(csd, 126, 2);
	if (structure == 1u) {
		c_size = lapis_csd_bits(csd, 48, 22);
		if (c_size == 0x3fffffu) return -1;
		*blocks = (c_size + 1u) << 10;
		return 0;
	}
	if (structure == 0u) {
		unsigned read_bl = lapis_csd_bits(csd, 80, 4);
		unsigned mult = lapis_csd_bits(csd, 47, 3);
		unsigned blocknr_shift;
		c_size = lapis_csd_bits(csd, 62, 12);
		if (read_bl < 9u || read_bl > 11u) return -1;
		blocknr_shift = mult + 2u;
		/* bytes = (c_size+1) << (blocknr_shift + read_bl); blocks = bytes/512. */
		if (blocknr_shift + read_bl < 9u) return -1;
		if (c_size == 0xfffu) return -1;
		bytes_hi = 0;
		bytes_lo = (c_size + 1u) << (blocknr_shift + read_bl - 9u);
		if (bytes_lo == 0u || (bytes_lo >> (blocknr_shift + read_bl - 9u)) != c_size + 1u)
			return -1;
		(void)bytes_hi;
		*blocks = bytes_lo;
		return 0;
	}
	return -1;
}

static inline int lapis_sd_csd_blocks(const unsigned resp_regs[4], unsigned *blocks)
{
	unsigned raw[4], csd[4];
	unsigned i;
	if (!resp_regs) return -1;
	for (i = 0; i < 4u; i++) raw[i] = resp_regs[3u - i];
	for (i = 0; i < 4u; i++) {
		csd[i] = raw[i] << 8;
		if (i != 3u) csd[i] |= raw[i + 1u] >> 24;
	}
	return lapis_sd_blocks_from_csd(csd, blocks);
}
#endif
