/* private state shared by lapis_gic.c and gic_selftest.c */
#pragma once
#include "../include/kern.h"
struct gic {
	vm_offset_t gicd, gicr_base, gicr_cpu;  /* gicr_cpu = RD_base frame of the boot CPU */
	uint64_t    gicr_size, stride, mpidr;
	uint32_t    nirq;                       /* INTIDs 0..nirq-1 implemented */
	void       *ctrl;                       /* the IOInterruptController object */
	IOInterruptVector *vectors;
	void      **vtable;                     /* address point of our vtable copy */
	uint8_t    *edge;                       /* per-INTID trigger (from specifier) */
	volatile uint32_t *count;               /* per-INTID dispatch count */
	volatile uint32_t spurious, unhandled;
};
struct gic *lapis_gic_state(void);
