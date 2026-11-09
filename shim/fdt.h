
#pragma once
#include <stdint.h>

/* Read a u32/u64 (cell-size agnostic) property of /chosen. Returns 0 if absent. */
int fdt_chosen_u64(uintptr_t fdt, const char *prop, uint64_t *out);


/* s8: DRAM map from the ABL DTB (x0). */
#define FDT_MAX_MEM 24
#define FDT_MAX_RSV 96
struct fdt_range { uint64_t base, size; };
struct fdt_rsv { uint64_t base, size; char name[28]; uint8_t nomap, has_reg; };
struct fdt_memmap {
	int nmem, nrsv, nrsvhdr, rsv_trunc, mem_trunc;
	uint32_t root_ac, root_sc, rsv_ac, rsv_sc;
	struct fdt_range mem[FDT_MAX_MEM];
	struct fdt_range rsvhdr[16];		/* /memreserve/ block */
	struct fdt_rsv rsv[FDT_MAX_RSV];
};
int fdt_memmap(uintptr_t fdt, struct fdt_memmap *m);
