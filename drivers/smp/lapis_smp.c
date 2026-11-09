/*
 * LapisSMP - register + boot all CPUs via ml_processor_register (which calls
 * processor_boot → cpu_start → PE_cpu_start_internal). We binpatch
 * PE_cpu_start_internal to PSCI CPU_ON.
 */
#include "../include/kern.h"

#define LOG(fmt, ...) klog("LapisSMP: " fmt "\n", ##__VA_ARGS__)
#define PSCI_CPU_ON_64 0xC4000003ull
#define PSCI_VERSION   0x84000000ull

struct ml_topology_cpu { uint32_t cpu_id, phys_id, cluster_id, die_id, cluster_type;
	uint32_t l2_cache_size, l2_cache_id, l3_cache_size, l3_cache_id; /* truncated; we only need first fields */
};
/* Real struct is larger — we only read cpu_id/phys_id via ml_get_topology_info. */
struct ml_topology_info {
	uint32_t version, num_cpus, num_clusters;
	uint32_t max_cpu_id, max_cluster_id;
	/* ... more fields; cpus[] follows at a known offset — use helpers instead */
};

extern void *ml_get_topology_info(void);
extern unsigned ml_get_cpu_count(void);
extern unsigned ml_get_max_cpu_number(void);
extern kern_return_t ml_processor_register(void *info, void **processor, void **ipi, void **pmi);
extern void ml_set_max_cpus(unsigned);
extern char LowResetVectorBase[];
extern volatile uint32_t real_ncpus;

/* ml_processor_info — keep in sync with machine_routines.h */
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

static uint64_t psci_smc(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t r0 __asm__("x0") = a0;
	register uint64_t r1 __asm__("x1") = a1;
	register uint64_t r2 __asm__("x2") = a2;
	register uint64_t r3 __asm__("x3") = a3;
	__asm__ volatile("smc #0" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : : "x4","x5","x6","x7","memory");
	return r0;
}
static uint64_t psci_hvc(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	register uint64_t r0 __asm__("x0") = a0;
	register uint64_t r1 __asm__("x1") = a1;
	register uint64_t r2 __asm__("x2") = a2;
	register uint64_t r3 __asm__("x3") = a3;
	__asm__ volatile("hvc #0" : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) : : "x4","x5","x6","x7","memory");
	return r0;
}
static int use_hvc;
static uint64_t psci(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	return use_hvc ? psci_hvc(a0,a1,a2,a3) : psci_smc(a0,a1,a2,a3);
}


extern void ml_cpu_up(void);
extern int ml_get_interrupts_enabled(void);
extern void ml_cpu_signal(unsigned cpu);  /* may not exist with this name */

void lapis_pe_cpu_machine_init(void *target, int bootb)
{
	unsigned cpu = (unsigned)(uintptr_t)target;
	LOG("PE_cpu_machine_init cpu %u bootb=%d irq_en=%d", cpu, bootb, ml_get_interrupts_enabled());
	/* Skip IOCPU vtable; just mark CPU up when interrupts are on (boot path). */
	if (ml_get_interrupts_enabled())
		ml_cpu_up();
}

void lapis_pe_cpu_signal(void *source, void *target)
{
	(void)source;
	unsigned cpu = (unsigned)(uintptr_t)target;
	/* Soft IPI via PSCI? Not available. Use GIC SGI through existing path:
	 * cpu_signal() in osfmk — fall back to no-op for bring-up; timers still work. */
	(void)cpu;
}

/* Replacement for PE_cpu_start_internal: cpu_id_t is our integer cast. */
void lapis_pe_cpu_start(void *target, vm_offset_t start_paddr, vm_offset_t arg_paddr)
{
	(void)start_paddr; (void)arg_paddr;
	unsigned cpu = (unsigned)(uintptr_t)target;
	/* phys_id (= MPIDR affinity) was stored in a side table at register time */
	extern uint32_t lapis_cpu_mpidr[];
	uint64_t mpidr = lapis_cpu_mpidr[cpu & 7];
	pmap_paddr_t entry = kvtophys((vm_offset_t)LowResetVectorBase);
	uint64_t r = psci(PSCI_CPU_ON_64, mpidr, entry, 0);
	LOG("PE_cpu_start cpu %u mpidr %#llx entry %#llx -> %#llx", cpu, mpidr, entry, r);
}

uint32_t lapis_cpu_mpidr[8];

static void smp_register_all(void)
{
	unsigned n = ml_get_cpu_count();
	unsigned maxn = ml_get_max_cpu_number();
	LOG("topology: cpu_count=%u max_cpu_number=%u real_ncpus=%u", n, maxn, real_ncpus);
	if (n < 1) n = 1;
	if (n > 8) n = 8;

	/* Probe PSCI */
	use_hvc = drv_bootarg("lapis_psci", 0) == 0; /* 0=HVC (QEMU), 1=SMC (hw default via boot-arg) */
	if (drv_bootarg("lapis_psci", 2) == 2) {
		/* auto: try SMC then HVC */
		int64_t v = (int64_t)psci_smc(PSCI_VERSION, 0, 0, 0);
		if (v < 0) { use_hvc = 1; v = (int64_t)psci_hvc(PSCI_VERSION, 0, 0, 0); }
		else use_hvc = 0;
		LOG("PSCI auto: %s version %#llx", use_hvc ? "HVC" : "SMC", v);
	} else {
		use_hvc = !drv_bootarg("lapis_psci", 1);
		LOG("PSCI via %s, version %#llx", use_hvc ? "HVC" : "SMC", psci(PSCI_VERSION,0,0,0));
	}

	/* Default MPIDR list (TB520FU / QEMU virt affinity) */
	for (unsigned i = 0; i < 8; i++) lapis_cpu_mpidr[i] = i << 8;

	for (unsigned cpu = 0; cpu < n; cpu++) {
		struct ml_processor_info info;
		bzero(&info, sizeof(info));
		info.cpu_id = (void *)(uintptr_t)cpu;   /* PE_cpu_start sees this */
		info.log_id = cpu;
		info.phys_id = lapis_cpu_mpidr[cpu];
		info.cluster_id = cpu / 2;
		info.cluster_type = 0;
		void *proc = NULL, *ipi = NULL, *pmi = NULL;
		kern_return_t kr = ml_processor_register(&info, &proc, &ipi, &pmi);
		LOG("ml_processor_register cpu %u -> %d proc=%p", cpu, kr, proc);
	}
	ml_set_max_cpus(n);   /* unblock waiters; ARM ignores the count */
	LOG("registered %u CPUs; real_ncpus now %u", n, real_ncpus);
}

/* Called synchronously from lapis_drivers_start so CPUs are up before bsd_init. */
void lapis_smp_start(void)
{
	if (!drv_bootarg("lapis_smp", 0)) { LOG("disabled (lapis_smp=0)"); return; }
	smp_register_all();
}
