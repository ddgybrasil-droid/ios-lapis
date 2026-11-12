/* SPDX-License-Identifier: BSD-2-Clause */
#pragma once
#ifdef LAPIS_KERNEL
#include "../include/kern.h"
#else
#include <stddef.h>
#include <stdint.h>
#endif
#define LAPIS_MDT_MAX_SEGMENTS 64
struct lapis_mdt_segment {
	uint32_t index,file_offset,file_size,memory_size;
	uint64_t destination; /* offset in the supplied physical carveout */
};
struct lapis_mdt_plan {
	struct lapis_mdt_segment segments[LAPIS_MDT_MAX_SEGMENTS];
	uint32_t count,header_size,hash_size,hash_offset,hash_index;
	uint64_t image_size;
	int split,relocate,hash_external;
};
int lapis_mdt_parse(const void *,size_t,uint64_t,uint64_t,struct lapis_mdt_plan *);
