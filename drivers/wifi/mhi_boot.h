#pragma once
/* BHI status values from Linux drivers/bus/mhi/host/internal.h.
 * RESET is not a completed firmware download. The image descriptor is the
 * register set programmed by mhi_fw_load_bhi: address, size, then a non-zero
 * session id in IMGTXDB. A zero session id is not written. */
#define BHI_STATUS_RESET   0u
#define BHI_STATUS_SUCCESS 2u
#define BHI_STATUS_ERROR   3u
#define LAPIS_BHI_IMAGE_MAX (16u * 1024u * 1024u)

struct lapis_bhi_image {
	uint32_t addr_lo, addr_hi, size, session;
};

static inline int lapis_bhi_window_valid(uint32_t offset, uint32_t bar_size)
{
	/* The BHI block runs through the last defined register at 0xc4. */
	return offset && !(offset & 3u) && offset <= bar_size &&
	       bar_size - offset >= 0xc8u;
}

static inline int lapis_bhi_image(uint64_t pa, uint32_t size, uint32_t session,
                                  struct lapis_bhi_image *out)
{
	if (!out || !pa || !size || !session || size > LAPIS_BHI_IMAGE_MAX) return -1;
	if ((uint64_t)size > ~pa) return -1;
	out->addr_lo = (uint32_t)pa;
	out->addr_hi = (uint32_t)(pa >> 32);
	out->size = size;
	out->session = session;
	return 0;
}
