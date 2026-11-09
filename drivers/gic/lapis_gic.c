/*
 * LapisGICv3 - built-in IOKit interrupt controller for the GICv3 on SM8650 (TB520FU,
 * EL1 under Gunyah) and QEMU virt (emulator profile).
 *
 * What it is: a real IOInterruptController instance (OSMetaClass IOInterruptController,
 * IOService::init'ed, attached to the platform expert, registerService'd) whose vtable
 * is a copy of IOInterruptController's with the "simplified subclass" hooks overridden
 * (handleInterrupt, initVector, getVectorType, enable/disableVectorHard, causeVector,
 * vectorCanBeShared, getInterruptHandlerAddress) - i.e. exactly what a C++ subclass
 * would compile to.  It registers under IODTInterruptControllerName(/arm-io/gic)
 * ("IOInterruptController%08X" of the gic AAPL,phandle), so any ADT node with
 * interrupt-parent=<gic> + interrupts=<intid flags> gets working
 * IOService::registerInterrupt/enableInterrupt.
 *
 * CPU side: the CI kernel already runs the GIC CPU interface (its pe_init_fiq() sets
 * ICC_SRE/PMR/BPR1/IGRPEN1, GICD_CTLR |= 0x13; its sleh_irq() reads ICC_IAR1_EL1,
 * handles the vtimer INTID and SGIs (<16 -> cpu_signal_handler) itself and passes every
 * other INTID as `source` to cpu_data->interrupt_handler, then writes ICC_EOIR1_EL1).
 * So we install our dispatcher with ml_install_interrupt_handler() and must NOT ack/EOI.
 *
 * Specifier convention (our ADT, 2 cells): <INTID flags>, flags = Linux GIC binding
 * trigger bits (1 = edge rising, 4 = level high).  Vector number = INTID.
 */
#include "../include/kern.h"
#include "gicv3.h"
#include "gic_priv.h"

 /* .bss lives in the zero-filled tail of the kernel's __DATA segment (see drv.ld) */
static struct gic G;
struct gic *lapis_gic_state(void) { return G.ctrl ? &G : NULL; }

#define LOG(fmt, ...) klog("LapisGICv3: " fmt "\n", ##__VA_ARGS__)

static int gicd_wait_rwp(void)
{
	for (int i = 0; i < 100000; i++)
		if (!(rd32(G.gicd + GICD_CTLR) & GICD_CTLR_RWP)) return 1;
	LOG("GICD_CTLR.RWP stuck");
	return 0;
}
static int gicr_wait_rwp(void)
{
	for (int i = 0; i < 100000; i++)
		if (!(rd32(G.gicr_cpu + GICR_CTLR) & GICR_CTLR_RWP)) return 1;
	LOG("GICR_CTLR.RWP stuck");
	return 0;
}

static void bit_set(vm_offset_t reg_base, uint32_t n) { wr32(reg_base + 4 * (n / 32), 1u << (n % 32)); }
static void rmw32(vm_offset_t a, uint32_t clr, uint32_t set) { wr32(a, (rd32(a) & ~clr) | set); }

/* ---- hardware vector ops ------------------------------------------------------- */
static void hw_config(uint32_t n, int edge)
{
	uint32_t sh = 8 * (n % 4), cf = 2 * (n % 16) + 1;
	if (n < 16) return;                       /* SGIs belong to the kernel's IPI path */
	if (n < 32) {                             /* PPI: boot CPU redistributor SGI_base */
		wr32(G.gicr_cpu + GICR_ICENABLER0, 1u << n); gicr_wait_rwp();
		rmw32(G.gicr_cpu + GICR_IGROUPR0, 0, 1u << n);          /* Group 1 */
		rmw32(G.gicr_cpu + GICR_IGRPMODR0, 1u << n, 0);         /* ... non-secure */
		rmw32(G.gicr_cpu + GICR_IPRIORITYR + 4 * (n / 4), 0xffu << sh, 0xa0u << sh);
		rmw32(G.gicr_cpu + GICR_ICFGR1, 1u << (2 * (n - 16) + 1), edge ? 1u << (2 * (n - 16) + 1) : 0);
		return;
	}
	bit_set(G.gicd + GICD_ICENABLER, n); gicd_wait_rwp();
	rmw32(G.gicd + GICD_IGROUPR + 4 * (n / 32), 0, 1u << (n % 32));
	rmw32(G.gicd + GICD_IGRPMODR + 4 * (n / 32), 1u << (n % 32), 0);
	rmw32(G.gicd + GICD_IPRIORITYR + 4 * (n / 4), 0xffu << sh, 0xa0u << sh);
	rmw32(G.gicd + GICD_ICFGR + 4 * (n / 16), 1u << cf, edge ? 1u << cf : 0);
	/* IROUTER: Aff3[39:32] Aff2[23:16] Aff1[15:8] Aff0[7:0], IRM=0 -> boot CPU.
	 * Two 32-bit halves (spec allows 32-bit access; friendlier to trapping hypervisors). */
	uint64_t aff = G.mpidr & 0xff00ffffffull;
	wr32(G.gicd + GICD_IROUTER + 8 * n, (uint32_t)aff);
	wr32(G.gicd + GICD_IROUTER + 8 * n + 4, (uint32_t)(aff >> 32));
}
static void hw_enable(uint32_t n)
{
	if (n < 16) return;
	if (n < 32) wr32(G.gicr_cpu + GICR_ISENABLER0, 1u << n);
	else bit_set(G.gicd + GICD_ISENABLER, n);
}
static void hw_disable(uint32_t n)
{
	if (n < 16) return;
	if (n < 32) { wr32(G.gicr_cpu + GICR_ICENABLER0, 1u << n); gicr_wait_rwp(); }
	else { bit_set(G.gicd + GICD_ICENABLER, n); gicd_wait_rwp(); }
}
static void hw_pend(uint32_t n)
{
	if (n < 16) {   /* SGI to self: ICC_SGI1R_EL1 TargetList=1<<Aff0, Aff1/2/3, INTID[27:24] */
		uint64_t m = G.mpidr, v = (1ull << (m & 0xf)) | (((m >> 8) & 0xff) << 16) |
		    ((uint64_t)n << 24) | (((m >> 16) & 0xff) << 32) | (((m >> 32) & 0xff) << 48);
		__asm__ volatile("msr " ICC_SGI1R_EL1 ", %0; isb" :: "r"(v));
	} else if (n < 32) wr32(G.gicr_cpu + GICR_ISPENDR0, 1u << n);
	else bit_set(G.gicd + GICD_ISPENDR, n);
}

/* ---- IOInterruptController overrides (this == G.ctrl) ---------------------------- */
static void dispatch(uint32_t n)
{
	IOInterruptVector *v;
	if (n >= G.nirq) { G.unhandled++; return; }
	v = &G.vectors[n];
	G.count[n]++;
	v->interruptActive = 1;
	__asm__ volatile("dmb ish" ::: "memory");
	if (!v->interruptDisabledSoft) {
		if (v->interruptRegistered && v->handler)
			v->handler(v->target, v->refCon, v->nub, v->source);
		else
			G.unhandled++;
		if (v->interruptDisabledSoft) {     /* handler (e.g. IOInterruptEventSource) asked to mask */
			v->interruptDisabledHard = 1;
			hw_disable(n);
		}
	} else {                                /* soft-disabled: mask in hardware now (lazy disable) */
		v->interruptDisabledHard = 1;
		hw_disable(n);
	}
	v->interruptActive = 0;
}

static IOReturn m_handleInterrupt(void *self, void *refCon, void *nub, int source)
{ (void)self; (void)refCon; (void)nub; dispatch((uint32_t)source); return kIOReturnSuccess; }

/* cpu_data->interrupt_handler signature: (target, refCon, nub, source = INTID from sleh_irq) */
static void cpu_irq(void *target, void *refCon, void *nub, int source)
{ (void)target; (void)refCon; (void)nub;
  if ((uint32_t)source >= GIC_MAX_INTID) { G.spurious++; return; }
  dispatch((uint32_t)source); }

static IOInterruptAction m_getInterruptHandlerAddress(void *self) { (void)self; return (IOInterruptAction)(void *)m_handleInterrupt; }
static int  m_vectorCanBeShared(void *self, int n, IOInterruptVector *v) { (void)self; (void)n; (void)v; return 1; }
static int  m_getVectorType(void *self, int n, IOInterruptVector *v) { (void)self; (void)v; return G.edge[n] ? kIOInterruptTypeEdge : kIOInterruptTypeLevel; }
static void m_disableVectorHard(void *self, int n, IOInterruptVector *v) { (void)self; (void)v; hw_disable(n); }
static void m_enableVector(void *self, int n, IOInterruptVector *v) { (void)self; (void)v; hw_enable(n); }
static void m_causeVector(void *self, int n, IOInterruptVector *v) { (void)self; (void)v; hw_pend(n); }
static void m_initVector(void *self, int n, IOInterruptVector *v)
{
	(void)self;
	/* trigger from the nub's specifier word 1 (if present) */
	int edge = 0;
	if (v->nub) {
		IOInterruptSource *src = *(IOInterruptSource **)((char *)v->nub + IOSERVICE_OFF_INTSOURCES);
		if (src && src[v->source].vectorData && k_OSData_getLength(src[v->source].vectorData) >= 8)
			edge = (((const uint32_t *)k_OSData_getBytesNoCopy(src[v->source].vectorData))[1] & 0x3) != 0;
	}
	G.edge[n] = (uint8_t)edge;
	hw_config(n, edge);
}

/* ---- probe ------------------------------------------------------------------------ */
static int find_redist(void)
{
	uint64_t m = G.mpidr;
	uint32_t want = (uint32_t)(((m >> 32) & 0xff) << 24 | (m & 0xffffff));
	for (uint64_t off = 0; off + 0x20000 <= G.gicr_size; off += G.stride) {
		uint64_t typer = rd64(G.gicr_base + off + GICR_TYPER);
		if ((uint32_t)(typer >> 32) == want) { G.gicr_cpu = G.gicr_base + off; return 1; }
		if (typer & GICR_TYPER_LAST) break;
	}
	return 0;
}

int lapis_gic_start(void *platform)
{
	void *dt = drv_dt_entry("/arm-io/gic");
	uint64_t r[4];
	if (!dt) { LOG("no /arm-io/gic in ADT"); return 0; }
	for (int i = 0; i < 4; i++)
		if (!drv_dt_u64(dt, "reg", i, &r[i])) { LOG("bad gic reg"); return 0; }
	vm_offset_t soc = pe_arm_get_soc_base_phys();
	G.gicd = ml_io_map(soc + r[0], r[1]);
	G.gicr_base = ml_io_map(soc + r[2], r[3]);
	G.gicr_size = r[3];
	__asm__ volatile("mrs %0, mpidr_el1" : "=r"(G.mpidr));
	uint32_t typer = rd32(G.gicd + GICD_TYPER), ctlr = rd32(G.gicd + GICD_CTLR);
	uint32_t iidr = rd32(G.gicd + GICD_IIDR), pidr2 = rd32(G.gicd + GICD_PIDR2);
	G.nirq = 32 * ((typer & 0x1f) + 1);
	if (G.nirq > GIC_MAX_INTID) G.nirq = GIC_MAX_INTID;
	uint64_t rt0 = rd64(G.gicr_base + GICR_TYPER);
	if (!drv_dt_u64(dt, "redistributor-stride", 0, &G.stride) || !G.stride)
		G.stride = (rt0 & GICR_TYPER_VLPIS) ? 0x40000 : 0x20000;
	LOG("GICD pa %#llx TYPER %#x (INTIDs<%u) IIDR %#x ArchRev %u CTLR %#x%s; GICR pa %#llx stride %#llx TYPER0 %#llx",
	    (uint64_t)(soc + r[0]), typer, G.nirq, iidr, (pidr2 >> 4) & 0xf, ctlr,
	    (ctlr & GICD_CTLR_DS) ? " DS" : "", (uint64_t)(soc + r[2]), G.stride, rt0);
	if (!find_redist()) { LOG("no redistributor for MPIDR %#llx", G.mpidr); return 0; }
	uint32_t ctl1; uint64_t pmr, igrp1;
	__asm__ volatile("mrs %0, " ICC_PMR_EL1 : "=r"(pmr));
	__asm__ volatile("mrs %0, " ICC_IGRPEN1_EL1 : "=r"(igrp1));
	ctl1 = rd32(G.gicr_cpu + GICR_IGROUPR0);
	LOG("boot CPU MPIDR %#llx -> GICR +%#lx WAKER %#x IGROUPR0 %#x ISENABLER0 %#x; ICC PMR %#llx IGRPEN1 %llu",
	    G.mpidr, G.gicr_cpu - G.gicr_base, rd32(G.gicr_cpu + GICR_WAKER), ctl1,
	    rd32(G.gicr_cpu + GICR_ISENABLER0), pmr, igrp1);

	/* IOInterruptController object (abstract class: operator new + ctor, like a subclass) */
	void *obj = OSObject_typed_operator_new(k_IOInterruptController_ktv, 160);
	if (!obj) return 0;
	k_IOInterruptController_ctor(obj, k_IOInterruptController_meta);
	void **vt = IOMallocZero(IOINTC_VTABLE_SLOTS * 8);
	memcpy(vt, k_IOInterruptController_vtable, IOINTC_VTABLE_SLOTS * 8);
	G.vtable = vt + 2;
	G.vtable[VT_getInterruptHandlerAddress / 8] = (void *)m_getInterruptHandlerAddress;
	G.vtable[VT_handleInterrupt / 8] = (void *)m_handleInterrupt;
	G.vtable[VT_vectorCanBeShared / 8] = (void *)m_vectorCanBeShared;
	G.vtable[VT_initVector / 8] = (void *)m_initVector;
	G.vtable[VT_getVectorType / 8] = (void *)m_getVectorType;
	G.vtable[VT_disableVectorHard / 8] = (void *)m_disableVectorHard;
	G.vtable[VT_enableVector / 8] = (void *)m_enableVector;
	G.vtable[VT_causeVector / 8] = (void *)m_causeVector;
	*(void ***)obj = G.vtable;
	if (!k_IOService_init(obj, NULL)) { LOG("init failed"); return 0; }
	G.vectors = IOMallocZero(G.nirq * sizeof(IOInterruptVector));
	G.edge = IOMallocZero(G.nirq);
	G.count = IOMallocZero(G.nirq * 4);
	for (uint32_t i = 0; i < G.nirq; i++) G.vectors[i].interruptLock = IOLockAlloc();
	*(IOInterruptVector **)((char *)obj + IOINTC_OFF_VECTORS) = G.vectors;
	k_IORE_setName(obj, "LapisGICv3", NULL);
	k_IORE_setPropertyNum(obj, "gicd-typer", typer, 32);
	k_IORE_setPropertyNum(obj, "interrupt-count", G.nirq, 32);
	G.ctrl = obj;

	void *pe = platform ? platform : k_IOService_getPlatform();
	k_IOService_attach(obj, pe);
	void *name = k_IODTInterruptControllerName(dt);
	k_IOPE_registerIntController(k_IOService_getPlatform(), name, obj);
	ml_install_interrupt_handler(obj, 0, obj, cpu_irq, &G);
	k_IOService_registerService(obj, 0);
	uint32_t ph = 0; drv_dt_u32(dt, "AAPL,phandle", &ph);
	LOG("registered as IOInterruptController%08X (%u vectors), cpu_data interrupt_handler installed", ph, G.nirq);

	void lapis_gic_selftest_start(void);
	lapis_gic_selftest_start();
	return 1;
}
