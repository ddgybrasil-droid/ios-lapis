#pragma once
/* Packed XRGB8888 geometry. A zero return is an in-range byte offset.
 * It does not mean the physical address is the panel or that DSI is on. */
#ifdef LAPIS_KERNEL
#include "../include/kern.h"
#else
#include <stdint.h>
#endif

static inline int lapis_fb_geometry(unsigned width, unsigned height, unsigned stride,
                                    uint64_t *bytes)
{
	uint64_t total;
	if (!width || !height || width > 8192u || height > 8192u) return -1;
	if (stride < width * 4u || (stride & 3u)) return -1;
	if ((uint64_t)stride > ~0ull / height) return -1;
	total = (uint64_t)stride * height;
	if (bytes) *bytes = total;
	return 0;
}

static inline int lapis_fb_offset(unsigned x, unsigned y, unsigned width, unsigned height,
                                  unsigned stride, uint64_t *offset)
{
	if (!offset || lapis_fb_geometry(width, height, stride, 0) || x >= width || y >= height)
		return -1;
	*offset = (uint64_t)y * stride + (uint64_t)x * 4u;
	return 0;
}
