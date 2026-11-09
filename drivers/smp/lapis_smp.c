/*
 * LapisSMP - bring up secondary CPUs via PSCI CPU_ON (SMC #0).
 *
 * TB520FU (from dump): 8 CPUs, MPIDR 0x0..0x700 (4 clusters x 2), PSCI method=smc.
 * Under Gunyah at EL1, SMC may trap to the hypervisor; if Gunyah forwards PSCI
 * this works, otherwise we log and leave secondaries alone (HYPOTHESIS until hw log).
 *
 * Boot-arg lapis_smp=0 disables. QEMU virt: verify with -smp 8.
 */
#include "../include/kern.h"

#define LOG(fmt, ...) klog("LapisSMP: " fmt "\n", ##__VA_ARGS__)
#define PSCI_CPU_ON_64   0xC4000003ull
#define PSCI_VERSION     0x84000000ull

extern char LowResetVectorBase[];

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
/* TB520FU dump: method=smc. QEMU virt EL1: HVC. Boot-arg lapis_psci=0 HVC, =1 SMC (default 1). */
static uint64_t psci(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3)
{
	if (drv_bootarg("lapis_psci", 1)) return psci_smc(a0,a1,a2,a3);
	return psci_hvc(a0,a1,a2,a3);
}

static void smp_thread(void *arg, int wr)
{
	(void)arg; (void)wr;
	uint32_t want = (uint32_t)drv_bootarg("cpus", 8);
	if (want < 1) want = 1;
	if (want > 8) want = 8;

	uint64_t ver = psci(PSCI_VERSION, 0, 0, 0);
	LOG("PSCI version %#llx (SMC), want %u CPUs", ver, want);
	if ((int64_t)ver < 0) {
		LOG("PSCI.VERSION failed (%lld) - Gunyah may not forward SMC PSCI; NOT bringing up secondaries",
		    (int64_t)ver);
		return;
	}
	pmap_paddr_t entry = kvtophys((vm_offset_t)LowResetVectorBase);
	LOG("secondary entry PA %#llx (LowResetVectorBase)", entry);
	if (!entry) { LOG("cannot translate LowResetVectorBase"); return; }

	static const uint64_t mpidr[8] = {
		0x000, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600, 0x700
	};
	unsigned ok = 1;
	for (unsigned i = 1; i < want; i++) {
		uint64_t r = psci(PSCI_CPU_ON_64, mpidr[i], entry, 0);
		LOG("CPU_ON mpidr %#llx -> %#llx%s", mpidr[i], r, r ? " (FAILED)" : " OK");
		if (r == 0 || (int64_t)r == -4) ok++;
	}
	LOG("secondaries requested %u, on-line (best-effort) %u / %u", want - 1, ok - 1, want);
}

void lapis_smp_start(void) { drv_thread(smp_thread, NULL); }
