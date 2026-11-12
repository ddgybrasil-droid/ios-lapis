#pragma once
/* GICv3 fields from Arm IHI 0069. A zero return describes the register
 * value. It does not mean the distributor is enabled or that an interrupt
 * was delivered. */
#ifdef LAPIS_KERNEL
#include "../include/kern.h"
#else
#include <stdint.h>
#endif

#define LAPIS_GICR_STRIDE_V3 0x20000ull
#define LAPIS_GICR_STRIDE_V4 0x40000ull

/* GICD_TYPER.ITLinesNumber is bits [4:0]. Implemented INTIDs are 32*(N+1),
 * and 1020..1023 are the special IDs, not SPIs. */
static inline unsigned lapis_gic_intid_count(unsigned typer)
{
	unsigned n = 32u * ((typer & 0x1fu) + 1u);
	return n > 1020u ? 1020u : n;
}

/* GICD_PIDR2 bits [7:4]. 3 is GICv3, 4 is GICv4. */
static inline int lapis_gic_arch_ok(unsigned pidr2)
{
	unsigned rev = (pidr2 >> 4) & 0xfu;
	return rev == 3u || rev == 4u;
}

static inline int lapis_gic_stride_ok(uint64_t stride)
{
	return stride == LAPIS_GICR_STRIDE_V3 || stride == LAPIS_GICR_STRIDE_V4;
}

/* GICR_TYPER[63:32] is Aff3:Aff2:Aff1:Aff0. MPIDR_EL1 stores the same
 * fields in bits [39:32] and [23:0]. */
static inline uint32_t lapis_gic_affinity(uint64_t mpidr)
{
	return (uint32_t)(((mpidr >> 32) & 0xffu) << 24) | (uint32_t)(mpidr & 0xffffffu);
}

static inline int lapis_gic_typer_match(uint64_t typer, uint64_t mpidr)
{
	return (uint32_t)(typer >> 32) == lapis_gic_affinity(mpidr);
}

static inline int lapis_gic_typer_last(uint64_t typer)
{
	return (typer & (1ull << 4)) != 0;
}

/* Offset of the RD_base whose affinity matches. Last stops the walk.
 * A missing CPU is -1, not frame 0. */
static inline int64_t lapis_gic_redist_find(uint64_t mpidr, const uint64_t *typer,
                                            unsigned frames, uint64_t stride)
{
	unsigned i;
	if (!typer || !frames || !lapis_gic_stride_ok(stride)) return -1;
	for (i = 0; i < frames; i++) {
		if (lapis_gic_typer_match(typer[i], mpidr))
			return (int64_t)i * (int64_t)stride;
		if (lapis_gic_typer_last(typer[i])) return -1;
	}
	return -1;
}

/* Linux GIC binding bits [3:0]: 1 edge-rising, 2 edge-falling,
 * 4 level-high, 8 level-low. Any other value is not an edge. */
static inline int lapis_gic_edge(unsigned flags)
{
	unsigned trigger = flags & 0xfu;
	return trigger == 1u || trigger == 2u;
}

/* ICC_SGI1R_EL1. Aff0 above 15 needs the RS field, which this encoding
 * does not fill, so those targets are refused. */
static inline int lapis_gic_sgi1r(uint64_t mpidr, unsigned intid, uint64_t *out)
{
	unsigned aff0;
	if (!out || intid > 15u) return -1;
	aff0 = (unsigned)(mpidr & 0xffu);
	if (aff0 > 15u) return -1;
	*out = (1ull << aff0) |
	       (((mpidr >> 8) & 0xffull) << 16) |
	       ((uint64_t)intid << 24) |
	       (((mpidr >> 16) & 0xffull) << 32) |
	       (((mpidr >> 32) & 0xffull) << 48);
	return 0;
}

/* IROUTER with IRM clear: route to the affinity, not to every participating CPU. */
static inline uint64_t lapis_gic_irouter(uint64_t mpidr)
{
	return mpidr & 0xff00ffffffull;
}

static inline int lapis_gic_is_spurious(unsigned intid)
{
	return intid >= 1020u;
}

/* GICR_WAKER.ProcessorSleep is bit 1, ChildrenAsleep is bit 2. */
static inline int lapis_gic_redist_awake(unsigned waker)
{
	return (waker & 0x6u) == 0u;
}
