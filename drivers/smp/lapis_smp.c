/*
 * LapisSMP — bring secondary CPUs online via PSCI CPU_ON and XNU's
 * ml_processor_register → processor_boot → PE_cpu_start_internal (binpatched
 * to lapis_pe_cpu_start).
 *
 * TB520FU (dtb): 8 CPUs, MPIDR 0x0..0x700, PSCI method=smc.
 * QEMU virt: same MPIDR layout; method=hvc. Use -smp 8 and lapis_smp=1.
 *
 * Boot-args:
 *   lapis_smp=1           enable
 *   lapis_psci=0|1|2      0=HVC, 1=SMC, 2=auto (default)
 *   lapis_smp_n=<1..8>    clamp CPU count (default: ml_get_cpu_count())
 */
#include "../include/kern.h"

#define LOG(fmt, ...) klog("LapisSMP: " fmt "\n", ##__VA_ARGS__)
#define PSCI_CPU_ON_64       0xC4000003ull
#define PSCI_VERSION         0x84000000ull
#define PSCI_AFFINITY_INFO   0xC4000004ull
#define PSCI_SUCCESS         0
#define PSCI_ALREADY_ON      (int64_t)(-4)

extern void *ml_get_topology_info(void);
extern unsigned ml_get_cpu_count(void);
extern unsigned ml_get_max_cpu_number(void);
extern kern_return_t ml_processor_register(void *info, void **processor, void **ipi, void **pmi);
extern void ml_set_max_cpus(unsigned);
extern char LowResetVectorBase[];
extern volatile uint32_t real_ncpus;
extern void ml_cpu_up(void);
extern int ml_get_interrupts_enabled(void);

struct ml_processor_info {
	void *cpu_id;
	vm_offset_t start_paddr;
	int supports_nap;
	void *platform_cache_dispatch;
	void *time_base_enable;
	void *processor_idle;
	void **idle_tickle;
	void *idle_timer;
	void *idle_timer_refcon;
	vm_offset_t powergate_stub_addr;
	uint32_t powergate_stub_length;
	uint32_t powergate_latency;
	void *platform_error_handler;
	uint64_t regmap_paddr;
	uint32_t phys_id;
	uint32_t log_id;
	uint32_t l2_access_penalty;
	uint32_t cluster_id;
	uint32_t cluster_type;
	uint32_t l2_cache_id, l2_cache_size, l3_cache_id, l3_cache_size;
};

uint32_t lapis_cpu_mpidr[8];
static int use_hvc;
static unsigned brought_up;

static uint64_t psci_smc(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t r0 __asm__("x0") = a0;
	register uint64_t r1 __asm__("x1") = a1;
	register uint64_t r2 __asm__("x2") = a2;
	register uint64_t r3 __asm__("x3") = a3;
	__asm__ volatile("smc #0" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
	                 : : "x4","x5","x6","x7","memory");
	return r0;
}
static uint64_t psci_hvc(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t r0 __asm__("x0") = a0;
	register uint64_t r1 __asm__("x1") = a1;
	register uint64_t r2 __asm__("x2") = a2;
	register uint64_t r3 __asm__("x3") = a3;
	__asm__ volatile("hvc #0" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)
	                 : : "x4","x5","x6","x7","memory");
	return r0;
}
static uint64_t psci(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	return use_hvc ? psci_hvc(a0,a1,a2,a3) : psci_smc(a0,a1,a2,a3);
}

void lapis_pe_cpu_machine_init(void *target, int bootb)
{
	unsigned cpu = (unsigned)(uintptr_t)target;
	LOG("PE_cpu_machine_init cpu %u bootb=%d irq_en=%d", cpu, bootb, ml_get_interrupts_enabled());
	if (ml_get_interrupts_enabled())
		ml_cpu_up();
	brought_up++;
}

void lapis_pe_cpu_signal(void *source, void *target)
{
	(void)source;
	unsigned cpu = (unsigned)(uintptr_t)target;
	/* Soft IPI: poke GICD_ISPENDR for SGI targeting the destination.
	 * Full cross-call needs the redistributor online; log for now. */
	LOG("PE_cpu_signal -> cpu %u (SGI path relies on XNU cpu_signal fallback)", cpu);
}

void lapis_pe_cpu_start(void *target, vm_offset_t start_paddr, vm_offset_t arg_paddr)
{
	unsigned cpu = (unsigned)(uintptr_t)target;
	uint64_t mpidr = lapis_cpu_mpidr[cpu & 7];
	/* Prefer XNU-provided start_paddr when non-zero; else LowResetVectorBase. */
	pmap_paddr_t entry = start_paddr ? (pmap_paddr_t)start_paddr
	                                 : kvtophys((vm_offset_t)LowResetVectorBase);
	LOG("PE_cpu_start cpu %u mpidr %#llx entry %#llx arg %#lx",
	    cpu, mpidr, entry, (unsigned long)arg_paddr);
	if (cpu == 0) {
		LOG("  bsp — skip PSCI");
		return;
	}
	int64_t r = (int64_t)psci(PSCI_CPU_ON_64, mpidr, entry, arg_paddr ? (uint64_t)arg_paddr : 0);
	LOG("  PSCI CPU_ON -> %#llx%s", (uint64_t)r,
	    r == PSCI_SUCCESS ? " OK" : r == PSCI_ALREADY_ON ? " already-on" : " FAIL");
	/* Brief wait for secondary to clear AFFINITY_INFO to ON (0). */
	for (int i = 0; i < 100; i++) {
		int64_t a = (int64_t)psci(PSCI_AFFINITY_INFO, mpidr, 0, 0);
		if (a == 0) { LOG("  cpu %u ON (affinity)", cpu); break; }
		IODelay(1000);
	}
}

static void pick_psci_conduit(void)
{
	int mode = drv_bootarg("lapis_psci", 2);
	if (mode == 2) {
		int64_t v = (int64_t)psci_smc(PSCI_VERSION, 0, 0, 0);
		if (v < 0 || (uint64_t)v > 0x20000) {
			use_hvc = 1;
			v = (int64_t)psci_hvc(PSCI_VERSION, 0, 0, 0);
		} else use_hvc = 0;
		LOG("PSCI auto: %s version %#llx", use_hvc ? "HVC" : "SMC", (uint64_t)v);
	} else {
		use_hvc = (mode == 0);
		LOG("PSCI via %s, version %#llx", use_hvc ? "HVC" : "SMC",
		    psci(PSCI_VERSION, 0, 0, 0));
	}
}

static unsigned probe_present(unsigned want)
{
	/* AFFINITY_INFO: 0=on, 1=off, 2=on pending. Missing CPUs return NOT_PRESENT (-2). */
	unsigned n = 1;
	for (unsigned i = 1; i < want; i++) {
		int64_t a = (int64_t)psci(PSCI_AFFINITY_INFO, lapis_cpu_mpidr[i], 0, 0);
		LOG("AFFINITY_INFO mpidr %#x -> %#llx", lapis_cpu_mpidr[i], (uint64_t)a);
		if (a < 0 && a != 0 && a != 1 && a != 2) break;
		n = i + 1;
	}
	return n;
}

static void smp_register_all(void)
{
	unsigned n = ml_get_cpu_count();
	unsigned maxn = ml_get_max_cpu_number();
	/* lapis_smp_n overrides ADT/cpus= topology so we can boot with cpus=1
	 * (avoid ml_wait_max_cpus hanging) then raise the count ourselves. */
	unsigned want = (unsigned)drv_bootarg("lapis_smp_n", 0);
	if (!want) want = n ? n : 1;
	LOG("topology: cpu_count=%u max_cpu_number=%u real_ncpus=%u want=%u",
	    n, maxn, real_ncpus, want);
	if (want < 1) want = 1;
	if (want > 8) want = 8;

	pick_psci_conduit();
	/* MPIDR layout: TB520FU dtb uses Aff1=cpu (0x0,0x100,..0x700).
	 * QEMU virt uses Aff0=cpu (0,1,2,..). Probe both; boot-arg lapis_mpidr_shift
	 * forces one (8=hw, 0=qemu). */
	{
		int shift = drv_bootarg("lapis_mpidr_shift", 255);
		if (shift == 255) {
			for (unsigned i = 0; i < 8; i++) lapis_cpu_mpidr[i] = i << 8;
			unsigned p8 = probe_present(want > 1 ? 2 : 1);
			if (p8 < 2 && want > 1) {
				for (unsigned i = 0; i < 8; i++) lapis_cpu_mpidr[i] = i;
				unsigned p0 = probe_present(want > 1 ? 2 : 1);
				LOG("MPIDR probe: shift8 present=%u shift0 present=%u → using %s",
				    p8, p0, p0 >= 2 ? "Aff0 (QEMU)" : "Aff1<<8 (hw)");
				if (p0 < 2) { /* fall back to hw layout anyway */
					for (unsigned i = 0; i < 8; i++) lapis_cpu_mpidr[i] = i << 8;
				}
			} else {
				LOG("MPIDR layout Aff1<<8 (hw/TB520FU)");
			}
		} else {
			for (unsigned i = 0; i < 8; i++)
				lapis_cpu_mpidr[i] = (shift ? (i << shift) : i);
			LOG("MPIDR layout forced shift=%d", shift);
		}
	}

	unsigned present = probe_present(want);
	if (present < want) {
		LOG("clamping %u -> %u (PSCI AFFINITY_INFO; use QEMU -smp %u)", want, present, want);
		want = present;
	}

	for (unsigned cpu = 0; cpu < want; cpu++) {
		struct ml_processor_info info;
		bzero(&info, sizeof(info));
		info.cpu_id = (void *)(uintptr_t)cpu;
		info.log_id = cpu;
		info.phys_id = lapis_cpu_mpidr[cpu];
		info.cluster_id = cpu / 2;
		info.cluster_type = 0;
		void *proc = NULL, *ipi = NULL, *pmi = NULL;
		LOG("registering cpu %u mpidr %#x ...", cpu, lapis_cpu_mpidr[cpu]);
		kern_return_t kr = ml_processor_register(&info, &proc, &ipi, &pmi);
		LOG("ml_processor_register cpu %u -> %d proc=%p real_ncpus=%u",
		    cpu, kr, proc, real_ncpus);
		if (kr) break;
		/* Raise the wait ceiling after each successful register so
		 * ml_wait_max_cpus observers see progress. */
		ml_set_max_cpus(cpu + 1);
	}
	LOG("done: want=%u real_ncpus=%u brought_up≈%u", want, real_ncpus, brought_up);
}

void lapis_smp_start(void)
{
	if (!drv_bootarg("lapis_smp", 0)) { LOG("disabled (lapis_smp=0)"); return; }
	smp_register_all();
}
